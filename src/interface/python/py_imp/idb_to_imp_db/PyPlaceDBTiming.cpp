#include "PyPlaceDB.h"
#include "idb_to_imp_db/CellSizeParser.hh"
#include "idb_to_imp_db/LibertyExportUtils.hh"
#include "idb_to_imp_db/OpenroadLibCellFamilyBuilder.hh"
// #include "ContestDriver.h"
#include <algorithm>
#include <boost/polygon/polygon.hpp>
#include <cassert>
#include <climits>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbPins.h"
#include "Lib.hh"
#include "Pin.hh"
#include "PowerEngine.hh"
#include "TimingEngine.hh"
#include "TimingIDBAdapter.hh"
#include "Type.hh"
#include "idm.h"
#include "netlist/Instance.hh"
#include "netlist/Pin.hh"
#include "sdc/SdcSetIODelay.hh"
#include "sdc/SdcSetInputTransition.hh"
#include "sdc/SdcSetLoad.hh"
#include "sta/StaData.hh"
#include "sta/StaLevelization.hh"
#include "sta/StaSlewPropagation.hh"
// #include "ContestDriver.h"
#include "PowerEngine.hh"
#include "sta/StaArc.hh"
// #include "Power.hh"
#include <boost/polygon/polygon.hpp>
#include <regex>
#include <vector>

namespace python_interface {

std::string get_priority(LibArc* arc);
bool arc_signature_less(LibArc* a, LibArc* b);

namespace {

double export_libcell_leakage_mw_for_python(LibCell* lib_cell)
{
  return lib_cell == nullptr ? 0.0
                             : python_interface::pydb_test::export_libcell_leakage_for_python_mw(*lib_cell);
}

std::string normalize_idb_term_name(const std::string& pin_name)
{
  auto bracket_pos = pin_name.find('[');
  if (bracket_pos != std::string::npos) {
    return pin_name.substr(0, bracket_pos);
  }
  return pin_name;
}

idb::IdbTerm* find_representative_term(idb::IdbCellMaster* cell_master, const std::string& raw_pin_name)
{
  if (cell_master == nullptr) {
    return nullptr;
  }

  std::string pin_name = normalize_idb_term_name(raw_pin_name);
  if (auto* term = cell_master->findTerm(pin_name); term != nullptr) {
    return term;
  }

  // LEF macros often materialize liberty bus pins only as bit terms such as
  // rd_out[0], rd_out[1], ... while liberty exposes the bus base name rd_out.
  if (auto* term = cell_master->findTerm(pin_name + "[0]"); term != nullptr) {
    return term;
  }

  const std::string bus_prefix = pin_name + "[";
  for (auto* term : cell_master->get_term_list()) {
    if (term != nullptr && term->get_name().rfind(bus_prefix, 0) == 0) {
      return term;
    }
  }

  return nullptr;
}

std::vector<LibArc*> collect_sorted_arcs(LibCell* lib_cell)
{
  std::vector<LibArc*> arcs;
  for (auto& arc_set : lib_cell->get_cell_arcs()) {
    for (auto& arc : arc_set->get_arcs()) {
      arcs.push_back(arc.get());
    }
  }
  std::sort(arcs.begin(), arcs.end(), arc_signature_less);
  return arcs;
}

bool has_consistent_arc_signature(const std::string& main_type, const std::vector<LibCell*>& lib_cells)
{
  if (lib_cells.empty()) {
    return true;
  }
  auto ref_arcs = collect_sorted_arcs(lib_cells.front());
  for (size_t j = 1; j < lib_cells.size(); ++j) {
    auto cmp_arcs = collect_sorted_arcs(lib_cells[j]);
    if (cmp_arcs.size() != ref_arcs.size()) {
      std::cerr << "Exclude sizing family due to arc count mismatch, main_type=" << main_type
                << " ref_cell=" << lib_cells.front()->get_cell_name()
                << " cmp_cell=" << lib_cells[j]->get_cell_name()
                << " ref_arc_count=" << ref_arcs.size()
                << " cmp_arc_count=" << cmp_arcs.size() << std::endl;
      return false;
    }
    for (size_t i = 0; i < ref_arcs.size(); ++i) {
      auto ref_priority = get_priority(ref_arcs[i]);
      auto cmp_priority = get_priority(cmp_arcs[i]);
      if (ref_priority != cmp_priority) {
        std::cerr << "Exclude sizing family due to arc signature mismatch, main_type=" << main_type
                  << " ref_cell=" << lib_cells.front()->get_cell_name()
                  << " cmp_cell=" << lib_cells[j]->get_cell_name()
                  << " arc_index=" << i
                  << " ref_arc=" << ref_priority
                  << " cmp_arc=" << cmp_priority << std::endl;
        return false;
      }
    }
  }
  return true;
}

struct ExportLibCellMember
{
  std::string cell_name;
  idb::IdbCellMaster* cell_master;
  LibCell* lib_cell;
};

struct ExportLibCellFamily
{
  std::string representative_name;
  std::vector<ExportLibCellMember> members;
};

std::vector<ExportLibCellFamily> build_export_libcell_families(
    idb::IdbCellMasterList* cell_master_list,
    ista::Sta* sta,
    const std::unordered_map<std::string, std::string>& liberty_cell_to_representative)
{
  std::vector<ExportLibCellFamily> families;
  std::unordered_map<std::string, size_t> family_key_to_index;

  if (cell_master_list == nullptr) {
    return families;
  }

  for (auto* cell_master : cell_master_list->get_cell_master()) {
    if (cell_master == nullptr) {
      continue;
    }

    const std::string cell_name = cell_master->get_name();
    auto* lib_cell = sta->findLibertyCell(cell_name.c_str());
    const bool force_singleton = cell_master->is_block() || (lib_cell == nullptr);
    std::string family_key = force_singleton ? "__singleton__:" + cell_name : cell_name;
    if (!force_singleton) {
      if (auto it = liberty_cell_to_representative.find(cell_name); it != liberty_cell_to_representative.end()) {
        family_key = it->second;
      }
    }

    auto [it, inserted] = family_key_to_index.emplace(family_key, families.size());
    if (inserted) {
      families.push_back(ExportLibCellFamily{cell_name, {}});
    }
    families[it->second].members.push_back(ExportLibCellMember{cell_name, cell_master, lib_cell});
  }

  return families;
}

double liberty_time_unit_to_ps_coeff(TimeUnit time_unit)
{
  // PyPlaceDB exports all liberty time quantities in ps so Python timing code
  // can stay on a single time base independent of each .lib's native unit.
  if (TimeUnit::kPS == time_unit) {
    return 1.0;
  }
  if (TimeUnit::kFS == time_unit) {
    return 1.0 / 1000.0;
  }
  if (TimeUnit::kNS == time_unit) {
    return 1000.0;
  }
  std::cerr << "Error: unsupported time unit: " << static_cast<int>(time_unit) << std::endl;
  exit(1);
}

std::optional<double> vertex_slew_ps(ista::StaVertex* sta_vertex,
                                     ista::AnalysisMode mode,
                                     ista::TransType trans_type)
{
  if (sta_vertex == nullptr) {
    return std::nullopt;
  }
  auto slew_ns = sta_vertex->getSlewNs(mode, trans_type);
  if (!slew_ns) {
    return std::nullopt;
  }
  // iSTA public APIs report slew in ns; PyPlaceDB normalizes them to ps.
  return NS_TO_PS(*slew_ns);
}

double liberty_cap_unit_to_pf_coeff(CapacitiveUnit cap_unit)
{
  // PyPlaceDB exports liberty capacitance/load quantities in pF so mixed-unit
  // libraries (for example fF stdcells plus pF macros) share one cap basis.
  if (CapacitiveUnit::kPF == cap_unit) {
    return 1.0;
  }
  if (CapacitiveUnit::kFF == cap_unit) {
    return 1.0 / 1000.0;
  }
  if (CapacitiveUnit::kF == cap_unit) {
    return 1.0e12;
  }
  std::cerr << "Error: unsupported cap unit: " << static_cast<int>(cap_unit) << std::endl;
  exit(1);
}

}  // namespace

// Helper function to extract VT type from cell name
// You can customize this logic based on your library naming convention
std::string PyPlaceDB::get_vt_type(const std::string& cell_name)
{
  // 1. Priority: User configuration
  for (const auto& [keyword, vt] : _vt_config) {
    if (cell_name.find(keyword) != std::string::npos) {
      return vt;
    }
  }

  // 2. Fallback: Default heuristics
  if (cell_name.find("HVT") != std::string::npos)
    return "HVT";
  if (cell_name.find("LVT") != std::string::npos)
    return "LVT";
  if (cell_name.find("RVT") != std::string::npos)
    return "RVT";
  if (cell_name.find("_H_") != std::string::npos)
    return "HVT";
  if (cell_name.find("_L_") != std::string::npos)
    return "LVT";

  // Default fallback: take the last character as a guess
  if (!cell_name.empty()) {
    return std::string(1, cell_name.back());
  }
  return "Unknown";
}

double PyPlaceDB::get_size_width(const std::string& cell_name)
{
  return cell_size::parse_cell_size_width(cell_name, _process_node);
}

void PyPlaceDB::set_vt_config(pybind11::list config)
{
  _vt_config.clear();
  for (auto item : config) {
    try {
      auto pair = item.cast<std::pair<std::string, std::string>>();
      _vt_config.push_back(pair);
    } catch (...) {
      std::cerr << "Warning: Invalid VT config item. Expected (keyword, vt_name)." << std::endl;
    }
  }
  _num_vt_types = _vt_config.size();
}

double intersectDistance(Box const& i1, Box const& i2, bool is_x)
{
  coordinate_type l;
  coordinate_type h;
  if (is_x) {
    l = std::max(i1.xl, i2.xl);
    h = std::min(i1.xh, i2.xh);
  } else {
    l = std::max(i1.yl, i2.yl);
    h = std::min(i1.yh, i2.yh);
  }
  return (l < h) ? (double) h - l : 0;
}

/// \return the intersection area of two boxes
double intersectArea(Box const& b1, Box const& b2)
{
  double dist[2] = {intersectDistance(b1, b2, true), intersectDistance(b1, b2, false)};
  return dist[0] * dist[1];
}

// /// \return true if a point is on boundary of the box
// inline bool onBoundary(Box const& b, Point const& p)
// {
//   return (onBoundary(b, p.x()) && contain(b, p.y())) || (onBoundary(b, p.y()) && contain(b.get(kX), p.x()));
// }

bool isInvailidNet(IdbNet* net)
{
  return net->is_ground() || net->is_power() || net->is_pdn() || net->is_clock()
         || net->get_instance_pin_list()->get_pin_list().size() == 0;
}

#if 1

using Graph = std::unordered_map<std::string, std::vector<std::string>>;

bool findOneCycleDFS(const std::string& u, const Graph& graph, std::unordered_map<std::string, int>& visit_status,
                     std::vector<std::string>& current_path_nodes, std::vector<std::string>& cycle_found_nodes)
{
  visit_status[u] = 1;  // Mark as visiting (in recursion stack for this path)
  current_path_nodes.push_back(u);

  if (graph.count(u)) {  // If node u has outgoing edges
    for (const std::string& v : graph.at(u)) {
      if (!graph.count(v) && visit_status.find(v) == visit_status.end()) {
        // Neighbor v is a sink node not explicitly in graph keys, ensure it's in visit_status
        // This case should ideally be handled by ensuring all_nodes are in visit_status initially
        // For robustness, if v is not in visit_status map, it means it's a new sink node.
        // It cannot cause a cycle by itself if it's a new sink.
        // However, for safety, one might initialize it.
        // visit_status[v] = 0; // Or handle as an error if all nodes aren't pre-initialized
      }

      if (visit_status.count(v)) {   // Make sure neighbor v is in the status map
        if (visit_status[v] == 1) {  // Cycle detected: back edge to a node in the current recursion stack
          cycle_found_nodes.clear();
          auto cycle_start_it = std::find(current_path_nodes.begin(), current_path_nodes.end(), v);
          if (cycle_start_it != current_path_nodes.end()) {
            // The cycle includes nodes from 'v' up to 'u' (which is current_path_nodes.back())
            for (auto it = cycle_start_it; it != current_path_nodes.end(); ++it) {
              cycle_found_nodes.push_back(*it);
            }
            cycle_found_nodes.push_back(v);  // Add 'v' again to close the cycle visually (v -> ... -> u -> v)
          } else {
            // This should not happen if v is in recursion_stack (status 1)
            // and current_path_nodes correctly reflects the recursion stack.
            // Fallback or error. For simplicity, we assume path is consistent.
            cycle_found_nodes.push_back(u);  // At least report current node
            cycle_found_nodes.push_back(v);  // and the problematic neighbor
          }
          return true;  // Cycle found
        }
        if (visit_status[v] == 0) {  // If neighbor is unvisited
          if (findOneCycleDFS(v, graph, visit_status, current_path_nodes, cycle_found_nodes)) {
            return true;  // Propagate cycle found signal
          }
        }
        // If visit_status[v] == 2, node v has been fully processed in a previous DFS path, no cycle through it from here.
      } else {
        // This case implies 'v' was a neighbor but not in 'all_nodes' initially, which
        // should be caught by graph consistency checks earlier or by ensuring all_nodes are in visit_status.
        // std::cerr << "Warning: Neighbor " << v << " of node " << u << " not found in visit_status map during DFS." << std::endl;
      }
    }
  }

  current_path_nodes.pop_back();  // Backtrack
  visit_status[u] = 2;            // Mark as fully visited (finished processing descendants)
  return false;                   // No cycle found along this path
}

/**
 * @brief Perform topological sorting and leveling on a given Directed Acyclic Graph (DAG).
 *
 * @param graph Adjacency list representation of the graph. Keys are node names, and values are lists of neighbors.
 * The caller is responsible for constructing this graph based on the design database.
 * @param start_nodes An optional list of nodes considered as starting points for the topological sort (typically primary inputs or
 * flip-flop outputs). If empty, the function will treat all nodes with in-degree 0 as starting points. These nodes will have their levels
 * initialized to 0.
 *
 * @return std::tuple containing:
 * - std::vector<std::string>: List of nodes in topological order. Empty if a cycle is detected.
 * - std::unordered_map<std::string, int>: Levels of each node (longest path length from starting nodes). Empty if a cycle is detected.
 * - bool: True if the graph is a DAG and the operation succeeds; false if a cycle is detected.
 */
std::tuple<std::vector<std::string>, std::unordered_map<std::string, int>, bool> topologicalSortAndLevelize(
    const std::unordered_set<std::string>& all_nodes, const Graph& graph, idm::DataManager* db,
    const std::vector<std::string>& start_nodes = {})
{
  // --- Initialization ---
  std::unordered_map<std::string, int> in_degree;  // Stores the in-degree of each node
  std::unordered_map<std::string, int> levels;     // Stores the level of each node

  // 1. Collect all nodes and calculate in-degrees (based on the provided graph)
  // Note: The caller must ensure that the graph structure is correct and reflects the dependencies between nodes.
  for (const auto& pair : graph) {
    for (const std::string& neighbor : pair.second) {
      in_degree[neighbor]++;
    }
  }
  // Ensure all nodes are initialized in in_degree and levels
  for (const std::string& node : all_nodes) {
    // Initialize levels to -1 or another marker value, indicating not yet visited/computed
    // Source nodes will be set to 0 in the next step
    levels[node] = -1;
    // Ensure nodes that only appear as targets (with no outgoing edges) are included in in_degree
    if (in_degree.find(node) == in_degree.end()) {
      in_degree[node] = 0;
    }
  }

  // 2. Initialize the queue
  std::queue<std::string> q;
  if (start_nodes.empty()) {
    // If no starting nodes are specified, add all nodes with in-degree 0 to the queue
    for (const std::string& node : all_nodes) {
      if (in_degree[node] == 0) {
        q.push(node);
        levels[node] = 0;  // Source nodes have level 0
      }
    }
  } else {
    // If starting nodes are specified (e.g., from start_points_str in your code snippet)
    // Add these specified starting nodes to the queue and set their levels to 0
    // Assume these start_nodes are valid starting points in the graph (in-degree 0 or logical starting points)
    for (const std::string& start_node : start_nodes) {
      if (all_nodes.count(start_node)) {  // Ensure the starting node exists in the graph
                                          // Typically, specified starting nodes should have in-degree 0 or be logical starting points
                                          // if (in_degree[start_node] == 0) { // Optionally add this check
        q.push(start_node);
        levels[start_node] = 0;
        // } else {
        //     std::cerr << "Warning: Specified starting node " << start_node << " has non-zero in-degree." << std::endl;
        // }
      } else {
        std::cerr << "Warning: Specified starting node " << start_node << " is not in the graph." << std::endl;
      }
    }
    // Initialize levels for other nodes (if not already set by starting nodes)
    for (const std::string& node : all_nodes) {
      if (levels[node] == -1) {  // Only initialize nodes not set as starting nodes
                                 // For non-starting nodes, keep -1 or set to 0 or another initial value as needed
                                 // If all nodes are expected to have non-negative levels, keep -1 and handle during updates
      }
    }
  }

  std::vector<std::string> topological_order;  // Stores the result of the topological sort

  // --- Process nodes (Core logic of Kahn's Algorithm) ---
  while (!q.empty()) {
    // Dequeue a node
    std::string u = q.front();
    q.pop();
    topological_order.push_back(u);

    // Check if node u exists in the graph keys (i.e., has outgoing edges)
    if (graph.count(u)) {
      // Process all neighbors v of node u
      for (const std::string& v : graph.at(u)) {
        // Check if neighbor v exists in all_nodes (ensure data consistency)
        if (all_nodes.count(v)) {
          // Update the level of neighbor v
          // Level[v] should be the maximum level of all its predecessors + 1
          // Only update if the current path provides a longer path
          if (levels[u] != -1) {  // Ensure the predecessor's level is computed
            levels[v] = std::max(levels[v], levels[u] + 1);
          }

          // Decrease the in-degree of neighbor v by 1
          in_degree[v]--;

          // If the in-degree of neighbor v becomes 0, add it to the queue
          if (in_degree[v] == 0) {
            q.push(v);
          }
        } else {
          std::cerr << "Error: Graph data inconsistency, neighbor " << v << " of node " << u << " not found in node list." << std::endl;
          // Handle the error as needed, e.g., return failure
        }
      }
    }
  }

  // --- Check for cycles ---
  if (topological_order.size() == all_nodes.size()) {
    // Topological sort succeeded, the graph is a DAG
    // Clean up levels of unreachable nodes (if they remain -1)
    for (auto it = levels.begin(); it != levels.end();) {
      if (it->second == -1) {
        // Handle unreachable nodes, e.g., remove or set to a specific value
        std::cerr << "Warning: Node " << it->first << " is unreachable in the topological sort, level not computed." << std::endl;
        it = levels.erase(it);  // For example, remove
      } else {
        ++it;
      }
    }
    return std::make_tuple(topological_order, levels, true);
  } else {
    // The number of nodes in the topological order is less than the total number of nodes,
    // indicating a cycle (or nodes unreachable due to a cycle).
    std::cerr << "Error: Kahn's algorithm detected a cycle or unreachable nodes! Topological sort failed. Count: "
              << topological_order.size() << "/" << all_nodes.size() << ". Attempting DFS to find a cycle." << std::endl;

    std::vector<std::string> cycle_path_report;
    std::unordered_map<std::string, int> dfs_visit_status;

    for (const std::string& node_name : all_nodes) {
      dfs_visit_status[node_name] = 0;
    }

    bool cycle_found_by_dfs = false;
    for (const std::string& node : all_nodes) {
      if (dfs_visit_status[node] == 0 && (in_degree.count(node) && in_degree.at(node) > 0)) {
        std::vector<std::string> current_dfs_path;
        if (findOneCycleDFS(node, graph, dfs_visit_status, current_dfs_path, cycle_path_report)) {
          cycle_found_by_dfs = true;
          std::cerr << "DFS发现一个环: " << std::endl;  // Newline after initial message
          int nodes_on_line = 0;
          std::string indent = "    ";  // Indentation for subsequent lines
          for (size_t i = 0; i < cycle_path_report.size(); ++i) {
            if (nodes_on_line == 0 && i > 0) {
              std::cerr << indent;
            }
            std::cerr << cycle_path_report[i] << "("
                      << db->get_idb_design()->get_instance_list()->find_instance(cycle_path_report[i])->get_cell_master()->get_name()
                      << ")";
            nodes_on_line++;

            if (i < cycle_path_report.size() - 1) {
              std::cerr << " -> ";
            }

            if (nodes_on_line >= 3 && i < cycle_path_report.size() - 1) {
              std::cerr << std::endl;
              nodes_on_line = 0;
            }
          }
          std::cerr << std::endl;  // Final newline for this cycle report
          break;
        }
      }
    }

    if (!cycle_found_by_dfs && cycle_path_report.empty()) {
      std::cerr << "Kahn算法指示有环，但DFS未从剩余入度 > 0 的节点中分离出特定路径。列出所有剩余入度 > 0 的节点:" << std::endl;
      int nodes_on_line = 0;
      std::string indent = "    ";
      bool first_node_printed = false;
      for (const std::string& n : all_nodes) {
        if (in_degree.count(n) && in_degree.at(n) > 0) {
          cycle_path_report.push_back(n);                  // Still populate for return
          if (nodes_on_line == 0 && first_node_printed) {  // Indent if not the first line of this section
            std::cerr << indent;
          } else if (nodes_on_line == 0 && !first_node_printed) {
            std::cerr << indent;  // Indent the very first line of nodes too
          }
          std::cerr << "- 节点: " << n << " (最终入度: " << in_degree.at(n) << ")  ";
          nodes_on_line++;
          first_node_printed = true;
          if (nodes_on_line >= 2) {  // Print 2 such verbose items per line
            std::cerr << std::endl;
            nodes_on_line = 0;
          }
        }
      }
      if (nodes_on_line > 0) {  // Ensure a final newline if the last line wasn't full
        std::cerr << std::endl;
      }

      if (cycle_path_report.empty() && topological_order.size() != all_nodes.size()) {
        std::cerr << "警告：Kahn算法失败，但没有入度 > 0 的节点且DFS未找到特定环。这可能表示未处理的断开组件。" << std::endl;
        std::cerr << "不在拓扑顺序中的节点:" << std::endl;
        nodes_on_line = 0;  // Reset for this section
        first_node_printed = false;
        for (const std::string& n : all_nodes) {
          if (std::find(topological_order.begin(), topological_order.end(), n) == topological_order.end()) {
            cycle_path_report.push_back(n);
            if (nodes_on_line == 0 && first_node_printed) {
              std::cerr << indent;
            } else if (nodes_on_line == 0 && !first_node_printed) {
              std::cerr << indent;
            }
            std::cerr << n << "  ";
            nodes_on_line++;
            first_node_printed = true;
            if (nodes_on_line >= 4) {  // 4 nodes per line for this list
              std::cerr << std::endl;
              nodes_on_line = 0;
            }
          }
        }
        if (nodes_on_line > 0) {
          std::cerr << std::endl;
        }
      }
    }

    return std::make_tuple(cycle_path_report, std::unordered_map<std::string, int>(), false);
  }
}

struct TempLibArc
{
  int _lib_arc_idx;
  int _lib_arc_offset;
  ista::LibArc* _lib_arc_ptr;
  explicit TempLibArc(int lib_arc_idx, int lib_arc_offset, ista::LibArc* lib_arc_ptr)
      : _lib_arc_idx(lib_arc_idx), _lib_arc_offset(lib_arc_offset), _lib_arc_ptr(lib_arc_ptr)
  {
  }
  int senseToInt() const
  {
    switch (_lib_arc_ptr->get_timing_sense()) {
      case ista::LibArc::TimingSense::kPositiveUnate:
        return 1;
      case ista::LibArc::TimingSense::kNonUnate:
        return 0;
      case ista::LibArc::TimingSense::kNegativeUnate:
        return -1;
      default:
        return 1;
    }
  }
  int typeToInt() const
  {
    switch (_lib_arc_ptr->get_timing_type()) {
      case ista::LibArc::TimingType::kRisingEdge:
        return 1;
      case ista::LibArc::TimingType::kFallingEdge:
        return -1;
      default:
        return 0;
    }
  }
};

bool is_supported_export_timing_type(ista::LibArc::TimingType timing_type)
{
  return timing_type == ista::LibArc::TimingType::kCombFall
         || timing_type == ista::LibArc::TimingType::kCombRise
         || timing_type == ista::LibArc::TimingType::kComb
         || timing_type == ista::LibArc::TimingType::kFallingEdge
         || timing_type == ista::LibArc::TimingType::kRisingEdge
         || timing_type == ista::LibArc::TimingType::kDefault
         || timing_type == ista::LibArc::TimingType::kSetupRising
         || timing_type == ista::LibArc::TimingType::kSetupFalling;
}

int lib_arc_timing_sense_to_int(ista::LibArc* arc)
{
  switch (arc->get_timing_sense()) {
    case ista::LibArc::TimingSense::kPositiveUnate:
      return 1;
    case ista::LibArc::TimingSense::kNonUnate:
      return 0;
    case ista::LibArc::TimingSense::kNegativeUnate:
      return -1;
    default:
      return 1;
  }
}

int lib_arc_timing_type_to_int(ista::LibArc* arc)
{
  switch (arc->get_timing_type()) {
    case ista::LibArc::TimingType::kRisingEdge:
      return 1;
    case ista::LibArc::TimingType::kFallingEdge:
      return -1;
    default:
      return 0;
  }
}

std::string get_priority(LibArc* arc)
{
  return std::string(arc->get_src_port()) + std::string(arc->get_snk_port()) + ista::LibArc::timingTypeToString(arc->get_timing_type())
         + std::to_string(static_cast<int>(arc->get_timing_sense()));
}

namespace {

double get_table_sort_value(LibTable* table)
{
  if (table == nullptr) {
    return DBL_MAX;
  }
  auto& table_values = table->get_table_values();
  if (table_values.empty() || table_values.front() == nullptr) {
    return DBL_MAX;
  }
  return table_values.front()->getFloatValue();
}

double get_arc_delay_sort_value(LibArc* arc)
{
  if (arc == nullptr) {
    return DBL_MAX;
  }

  if (auto* delay_model = dynamic_cast<LibDelayTableModel*>(arc->get_table_model()); delay_model != nullptr) {
    auto rise_delay = get_table_sort_value(delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kCellRise)));
    auto fall_delay = get_table_sort_value(delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kCellFall)));
    auto rise_trans = get_table_sort_value(delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kRiseTransition)));
    auto fall_trans = get_table_sort_value(delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kFallTransition)));
    return std::min(std::min(rise_delay, fall_delay), std::min(rise_trans, fall_trans));
  }

  if (auto* check_model = dynamic_cast<LibCheckTableModel*>(arc->get_table_model()); check_model != nullptr) {
    auto rise_constrain = get_table_sort_value(check_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kRiseConstrain)));
    auto fall_constrain = get_table_sort_value(check_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kFallConstrain)));
    return std::min(rise_constrain, fall_constrain);
  }

  return DBL_MAX;
}

}  // namespace

bool arc_signature_less(LibArc* a, LibArc* b)
{
  auto a_priority = get_priority(a);
  auto b_priority = get_priority(b);
  if (a_priority != b_priority) {
    return a_priority < b_priority;
  }

  auto a_delay = get_arc_delay_sort_value(a);
  auto b_delay = get_arc_delay_sort_value(b);
  if (a_delay != b_delay) {
    return a_delay < b_delay;
  }

  return reinterpret_cast<std::uintptr_t>(a) < reinterpret_cast<std::uintptr_t>(b);
}

// 统一的LUT表格初始化函数
void PyPlaceDB::init_lut_table_unified(pybind11::list& flat_luts_values, pybind11::list& flat_luts_axis1_table,
                                       pybind11::list& flat_luts_axis2_table, pybind11::list& flat_luts_dim, ista::LibTable* table,
                                       ista::LibCell* lib_cell, bool is_constraint_table, std::string pin_name,
                                       std::string related_pin_name, std::string table_type, std::string main_type, double cell_size,
                                       std::string vt)
{
  pybind11::list luts_dim;
  pybind11::list luts_values;
  pybind11::list luts_axis1_table;
  pybind11::list luts_axis2_table;

  // Helper to write CSV line
  auto write_csv = [&](const std::vector<double>& axis1, const std::vector<double>& axis2, const std::vector<double>& values) {
    if (!_debug_dump_lut)
      return;
    if (pin_name.empty())
      return;
    static std::ofstream outfile("lut_data.csv");
    static bool header_written = false;
    if (!header_written) {
      outfile << "cell_name,cell_size,pin_name,related_pin_name,main_type,table_type,axis1,axis2,values\n";
      header_written = true;
    }
    outfile << lib_cell->get_cell_name() << "," << cell_size << "," << pin_name << "," << related_pin_name << "," << main_type << ","
            << table_type << ",";

    outfile << "\"";
    for (size_t i = 0; i < axis1.size(); ++i) {
      outfile << axis1[i] << (i == axis1.size() - 1 ? "" : ";");
    }
    outfile << "\",\"";
    for (size_t i = 0; i < axis2.size(); ++i) {
      outfile << axis2[i] << (i == axis2.size() - 1 ? "" : ";");
    }
    outfile << "\",\"";
    for (size_t i = 0; i < values.size(); ++i) {
      outfile << values[i] << (i == values.size() - 1 ? "" : ";");
    }
    outfile << "\"\n";
  };

  if (table == nullptr) {
    flat_luts_values.append(luts_values);
    flat_luts_axis1_table.append(luts_axis1_table);
    flat_luts_axis2_table.append(luts_axis2_table);
    luts_dim.append(0);
    luts_dim.append(0);
    flat_luts_dim.append(luts_dim);
    return;
  }
  auto& axes = table->get_axes();
  auto time_unit = lib_cell->get_owner_lib()->get_time_unit();
  // LUT axis/value time quantities are flattened to ps before crossing the
  // pybind boundary.
  double time_coeff = liberty_time_unit_to_ps_coeff(time_unit);

  double cap_coeff = 1.0;
  if (!is_constraint_table) {
    auto cap_unit = lib_cell->get_owner_lib()->get_cap_unit();
    // Delay/transition LUT load axes are flattened to pF.
    cap_coeff = liberty_cap_unit_to_pf_coeff(cap_unit);
  }
  // 根据表格类型确定轴的含义和顺序
  int AXIS1, AXIS2;
  int num_axis1, num_axis2;
  if (table->get_table_values().size() == 1) {
    // scalar constraint table
    luts_dim.append(1);
    luts_dim.append(1);
    double val = table->get_table_values().at(0)->getFloatValue() * time_coeff;
    luts_values.append(val);
    flat_luts_values.append(luts_values);
    flat_luts_axis1_table.append(luts_axis1_table);
    flat_luts_axis2_table.append(luts_axis2_table);
    flat_luts_dim.append(luts_dim);

    write_csv({}, {}, {val});
    return;
  }
  if (axes.size() == 1) {
    // 维度信息
    assert(!is_constraint_table);
    bool first_var_is_intrans
        = table->get_table_template()->get_template_variable1() == ista::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION;
    int size1 = table->get_axes().at(0)->get_axis_size();

    std::vector<double> axis1_vec, axis2_vec, values_vec;

    for (auto& vpt : table->getAxis(0).get_axis_values()) {
      auto val = vpt.get()->getFloatValue();
      val = pydb_test::export_one_dimensional_lut_axis_for_python(
          val, first_var_is_intrans, time_coeff, cap_coeff);
      if (first_var_is_intrans) {
        luts_axis1_table.append(val);
        axis1_vec.push_back(val);
      } else {
        luts_axis2_table.append(val);
        axis2_vec.push_back(val);
      }
    }
    // 填充一维表格的数据值
    for (auto& vptr : table->get_table_values()) {
      double val = vptr.get()->getFloatValue();
      val = pydb_test::export_lut_time_value_for_python_ps(val, time_coeff);
      luts_values.append(val);
      values_vec.push_back(val);
    }
    if (first_var_is_intrans) {
      luts_dim.append(size1);
      luts_dim.append(0);
    } else {
      luts_dim.append(0);
      luts_dim.append(size1);
    }

    flat_luts_values.append(luts_values);
    flat_luts_axis1_table.append(luts_axis1_table);
    flat_luts_axis2_table.append(luts_axis2_table);
    flat_luts_dim.append(luts_dim);

    write_csv(axis1_vec, axis2_vec, values_vec);
    return;
  }

  if (is_constraint_table) {
    // 约束表格：轴1是clk transition，轴2是data transition
    AXIS1 = table->get_table_template()->get_template_variable1() == ista::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION ? 0 : 1;
    AXIS2 = !AXIS1;
    num_axis1 = table->get_axes().at(AXIS1)->get_axis_size();  // clk transition
    num_axis2 = table->get_axes().at(AXIS2)->get_axis_size();  // data transition
  } else {
    // 延迟/转换表格：轴1是transition，轴2是capacitance
    AXIS1 = table->get_table_template()->get_template_variable1() == ista::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION ? 0 : 1;
    AXIS2 = !AXIS1;
    num_axis1 = table->get_axes().at(AXIS1)->get_axis_size();  // transition
    num_axis2 = table->get_axes().at(AXIS2)->get_axis_size();  // capacitance
  }

  luts_dim.append(num_axis1);
  luts_dim.append(num_axis2);

  std::vector<double> axis1_vec, axis2_vec, values_vec;

  // 处理轴1的值（transition 或 data_transition）
  for (auto& value : table->getAxis(AXIS1).get_axis_values()) {
    auto val = value.get()->getFloatValue() * time_coeff;
    luts_axis1_table.append(val);
    axis1_vec.push_back(val);
  }

  // 处理轴2的值（capacitance 或 clk_transition）
  for (auto& value : table->getAxis(AXIS2).get_axis_values()) {
    auto val = value.get()->getFloatValue();
    if (is_constraint_table) {
      val *= time_coeff;  // 约束表格两个轴都是时间单位
    } else {
      val *= cap_coeff;  // 延迟表格轴2是电容单位
    }
    luts_axis2_table.append(val);
    axis2_vec.push_back(val);
  }

  // 处理表格数据值，考虑转置
  std::vector<std::unique_ptr<LibAttrValue>>& table_values = table->get_table_values();
  bool need_transpose = (AXIS1 == 1);  // 如果轴1是第二个轴，需要转置

  if (need_transpose) {
    // 转置：原始数据按轴2主导存储，转换为按轴1主导存储
    for (int i = 0; i < num_axis1; i++) {
      for (int j = 0; j < num_axis2; j++) {
        int original_index = j * num_axis1 + i;  // 轴2主导的索引
        auto val = table_values[original_index].get()->getFloatValue() * time_coeff;
        luts_values.append(val);
        values_vec.push_back(val);
      }
    }
  } else {
    // 不需要转置，按原始顺序处理
    for (auto& value : table_values) {
      auto val = value.get()->getFloatValue() * time_coeff;
      luts_values.append(val);
      values_vec.push_back(val);
    }
  }

  flat_luts_values.append(luts_values);
  flat_luts_axis1_table.append(luts_axis1_table);
  flat_luts_axis2_table.append(luts_axis2_table);
  flat_luts_dim.append(luts_dim);

  write_csv(axis1_vec, axis2_vec, values_vec);
}

void PyPlaceDB::init_timing(idm::DataManager* db, std::unordered_map<std::string, int>& mClkPin2ID,
                            std::map<std::string, index_type>& mNodeName2ID, std::vector<IdbInstance*>& inst_resort_list,
                            int ext_blockage_num)
{
  /*************************************************************************/
  std::map<std::string, double> sdc_inrdelays;       //
  std::map<std::string, double> sdc_infdelays;       //
  std::map<std::string, double> sdc_inrtrans;        //
  std::map<std::string, double> sdc_inftrans;        //
  std::map<std::string, double> sdc_outcaps;         //
  std::map<std::string, double> sdc_endpoints_rRAT;  //
  std::map<std::string, double> sdc_endpoints_fRAT;  //
  std::map<std::string, double> name2clk_r_aat;      //
  std::map<std::string, double> name2clk_f_aat;      //
  std::map<std::string, double> name2clk_rtran;      //
  std::map<std::string, double> name2clk_ftran;      //
  std::map<std::string, std::string> name2clk_pin_names;
  /*--------------------------------sdc init------------------------------------------*/
  std::set<std::string> FFs_str;  // mNodeName2ID
  auto* db_deisgn = db->get_idb_design();
  auto timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto ista = timing_engine->get_ista();
  // auto* db_deisgn = db->get_idb_design();
  // set_load is expressed in STA/design cap units. Resolve that unit lazily and
  // convert to pF once before exporting SDC output loads.
  double design_cap_to_pf_coeff = 1.0;
  bool design_cap_to_pf_coeff_resolved = false;
  // Start-point slew export must observe a propagated STA state. Otherwise
  // TimingEngine::getSlew(...) on FF outputs tends to return near-default
  // values before full timing propagation, which collapses Python clk2q/net AAT.
  timing_engine->updateTiming();
  auto lookup_propagated_slew_ps = [](const std::string& pin_name, AnalysisMode mode, TransType trans_type)
      -> std::optional<double> {
    SlewKey key;
    key.pin_full_name = pin_name;
    key.mode = mode;
    key.trans = trans_type;
    const auto& slew_map = SlewDebugDataManager::getInstance().getData();
    auto it = slew_map.find(key);
    if (it == slew_map.end()) {
      return std::nullopt;
    }
    return NS_TO_PS(it->second);
  };
  auto lookup_clock_arrival_ps = [](ista::StaVertex* sta_vertex, AnalysisMode mode, TransType trans_type)
      -> std::optional<double> {
    if (sta_vertex == nullptr) {
      return std::nullopt;
    }
    auto clock_data_vec = sta_vertex->getClockData(mode, trans_type);
    std::optional<double> best_ps;
    for (auto* clock_data : clock_data_vec) {
      if (clock_data == nullptr || !clock_data->isClockData()) {
        continue;
      }
      double arrive_ps = FS_TO_PS(clock_data->get_arrive_time());
      if (!best_ps || arrive_ps > *best_ps) {
        best_ps = arrive_ps;
      }
    }
    return best_ps;
  };
  SdcConstrain* the_constrain = ista->getConstrain();
  auto& sdc_io_constraints = the_constrain->get_sdc_io_constraints();
  for (auto& io_constraint : sdc_io_constraints) {
    if (io_constraint->isSetInputDelay()) {
      auto set_io_delay = dynamic_cast<SdcSetIODelay*>(io_constraint.get());
      auto& objs = set_io_delay->get_objs();
      // SDC delay values are ns in STA, while exported endpoint/startpoint data
      // is ps in PyPlaceDB.
      double delay_value = NS_TO_PS(set_io_delay->get_delay_value());
      for (auto* obj : objs) {
        std::string pin_or_port_name = obj->getFullName();
        if (set_io_delay->isRise() && set_io_delay->isMax()) {
          sdc_inrdelays[pin_or_port_name] = delay_value;
        } else if (set_io_delay->isRise() && set_io_delay->isMin()) {
        } else if (set_io_delay->isFall() && set_io_delay->isMax()) {
          sdc_infdelays[pin_or_port_name] = delay_value;
        } else if (set_io_delay->isFall() && set_io_delay->isMin()) {
        }
      }
    }
    if (io_constraint->isSetInputTransition()) {
      auto* set_input_transition = dynamic_cast<SdcSetInputTransition*>(io_constraint.get());
      // Input transition constraints are normalized to ps for Python timing.
      double slew = NS_TO_PS(set_input_transition->get_transition_value());
      auto& objs = set_input_transition->get_objs();
      for (auto* obj : objs) {
        if (set_input_transition->isMax() && set_input_transition->isRise()) {
          sdc_inrtrans[obj->getFullName()] = slew;
        } else if (set_input_transition->isMax() && set_input_transition->isFall()) {
          sdc_inftrans[obj->getFullName()] = slew;
        } else if (set_input_transition->isMin() && set_input_transition->isRise()) {
          // inrtrans[obj->getFullName()].append(slew);
        } else if (set_input_transition->isMin() && set_input_transition->isFall()) {
          // inftrans[obj->getFullName()].append(slew);
        }
      }
    }
    if (io_constraint->isSetLoad()) {
      if (!design_cap_to_pf_coeff_resolved) {
        // SDC set_load values are expressed in the design cap unit, not any
        // particular liberty's native unit. Mixed-unit liberty bundles are
        // valid here (for example ASAP7 stdcells in fF plus SRAM macros in pF),
        // so derive the conversion from STA's design unit only when it is needed.
        design_cap_to_pf_coeff = ista->convertCapUnit(1.0);
        design_cap_to_pf_coeff_resolved = true;
      }
      auto* set_load = dynamic_cast<SdcSetLoad*>(io_constraint.get());
      double load = set_load->get_load_value() * design_cap_to_pf_coeff;
      auto& objs = set_load->get_objs();
      for (auto* obj : objs) {
        if (set_load->isMax() && set_load->isRise()) {
          sdc_outcaps[obj->getFullName()] = load;
        } else if (set_load->isMax() && set_load->isFall()) {
          sdc_outcaps[obj->getFullName()] = load;
        } else if (set_load->isMin() && set_load->isRise()) {
          // outcaps[obj->getFullName()].append(load);
        } else if (set_load->isMin() && set_load->isFall()) {
          // outcaps[obj->getFullName()].append(load);
        }
      }
    }

    if (io_constraint->isSetOutputDelay()) {
      auto* set_io_delay = dynamic_cast<SdcSetIODelay*>(io_constraint.get());
      // Output delay constraints are exported in ps on the same basis as RAT.
      double out_delay = NS_TO_PS(set_io_delay->get_delay_value());
      auto& objs = set_io_delay->get_objs();
      for (auto* obj : objs) {
        auto sta_clk_vertex = ista->findVertex(obj->getFullName().c_str());  // input can only find vertex
        double clock_period = DBL_MAX;
        if (sta_clk_vertex && sta_clk_vertex->getClockBucket().bucket_size()) {
          clock_period = NS_TO_PS(sta_clk_vertex->getPropClock()->getPeriodNs());
          // end_points_str.push_back(pin->get_pin_name());
          // sdc_endpoints_fRAT[pin->get_pin_name()] = clock_period;
          // sdc_endpoints_rRAT[pin->get_pin_name()] = clock_period;
          // sdc_outcaps[pin->get_pin_name()] = 0;  // FIXME
        }
        // TODO: Need to calc clock period, clock_uncertainty
        if (set_io_delay->isMax() && set_io_delay->isRise()) {
          sdc_endpoints_rRAT[obj->getFullName()] = clock_period - out_delay;  // clock_uncertainty
        } else if (set_io_delay->isMax() && set_io_delay->isFall()) {
          sdc_endpoints_fRAT[obj->getFullName()] = clock_period - out_delay;  // clock_uncertainty
        } else if (set_io_delay->isMin() && set_io_delay->isRise()) {
          // outcaps[obj->getFullName()].append(load);
        } else if (set_io_delay->isMin() && set_io_delay->isFall()) {
          // outcaps[obj->getFullName()].append(load);
        }
      }
    }
  }

  /*************************************************************************/
  /*--------------------------------timing init------------------------------------------*/
  auto& _sta = ista;

  if (mClkPin2ID.empty()) {
    int clk_pin_id = 0;
    for (auto instance : db_deisgn->get_instance_list()->get_instance_list()) {
      string instance_name = instance->get_name();
      auto* design_netlist = ista->get_netlist();
      auto* instance_temp = design_netlist->findInstance(instance_name.c_str());
      ista::Pin* sta_pin = nullptr;
      FOREACH_INSTANCE_PIN(instance_temp, sta_pin)
      {
        if (sta_pin->get_cell_port()->isClock()) {
          mClkPin2ID[instance_name + sta_pin->get_name()] = clk_pin_id++;
        }
      }
    }
  }
  /*--------------------------------topo init------------------------------------------*/
  /* topo*/

  // pybind11::list cells_by_level;          //
  // pybind11::list cells_by_reverse_level;  //

  std::vector<string> start_points_str;             // PIs and FFs' output pins
  std::vector<string> clock_pins_str;               // FFs' clock pins
  std::vector<string> end_points_str;               // POs and FFs' input pins
  std::vector<StaVertex*> level_bfs_queue;          // BFS queue for levelization
  std::vector<StaVertex*> level_bfs_queue_reverse;  // BFS queue for levelization
  int tot_unconstrained_io_pins = 0;
  for (auto pin : db_deisgn->get_io_pin_list()->get_pin_list()) {
    if (pin->get_net() == nullptr || pin->get_net()->is_ground() || pin->get_net()->is_power() || pin->get_net()->is_pdn()
        || pin->get_net()->is_clock()) {
      continue;
    }
    if (pin->is_primary_input()) {
      start_points_str.push_back(pin->get_pin_name());
      level_bfs_queue.push_back(ista->findVertex(pin->get_pin_name().c_str()));
      // DEBUG

      auto net = pin->get_net();
      if (net) {
        auto netname = net->get_net_name();
        std::cout << "Primary input pin found: " << pin->get_pin_name() << " on net: " << netname << std::endl;
      }
      if (sdc_infdelays.count(pin->get_pin_name()) == 0) {
        std::cout << "Warning: Primary input pin " << pin->get_pin_name() << " has no fall delay constraint., set to default -1e8"
                  << std::endl;
        sdc_infdelays[pin->get_pin_name()] = -1e8;
      }
      if (sdc_inrdelays.count(pin->get_pin_name()) == 0) {
        std::cout << "Warning: Primary input pin " << pin->get_pin_name() << " has no rise delay constraint., set to default -1e8"
                  << std::endl;
        sdc_inrdelays[pin->get_pin_name()] = -1e8;
      }
      auto instance = pin->get_instance();
      if (instance) {
        auto instname = instance->get_name();
        // std::cout << "Instance for primary input pin: " << instname << std::endl;
      }
      // DEBUG
    } else if (pin->is_primary_output()) {
      // auto sta_clk_vertex = ista->findVertex(pin->get_pin_name().c_str());  // input can only find vertex
      end_points_str.push_back(pin->get_pin_name());
      level_bfs_queue_reverse.push_back(ista->findVertex(pin->get_pin_name().c_str()));
      if (sdc_outcaps.count(pin->get_pin_name()) == 0) {
        sdc_outcaps[pin->get_pin_name()] = 0;
      }
      if (sdc_endpoints_fRAT.count(pin->get_pin_name()) == 0) {
        tot_unconstrained_io_pins += 1;
        printf("Error: Primary output pin %s has no fRAT constraint, set to default 9e7 ps\n", pin->get_pin_name().c_str());
        sdc_endpoints_fRAT[pin->get_pin_name()] = 9e7;  // NOTE: less than 1e8 in timing propagation
      }
      if (sdc_endpoints_rRAT.count(pin->get_pin_name()) == 0) {
        tot_unconstrained_io_pins += 1;
        printf("Error: Primary output pin %s has no rRAT constraint, set to default 9e7 ps\n", pin->get_pin_name().c_str());
        sdc_endpoints_rRAT[pin->get_pin_name()] = 9e7;
      }
      // if (sta_clk_vertex && sta_clk_vertex->getClockBucket().bucket_size()) {
      //
      //   sdc_endpoints_fRAT[pin->get_pin_name()] = NS_TO_PS(clock_period);
      //   sdc_endpoints_rRAT[pin->get_pin_name()] = NS_TO_PS(clock_period);
      // }
    }
  }
  printf("Total primary output pins without RAT constraints: %d\n", tot_unconstrained_io_pins);
  for (auto instance : db_deisgn->get_instance_list()->get_instance_list()) {
    auto lib_cell = _sta->findLibertyCell(instance->get_cell_master()->get_name().c_str());
    bool is_clock = false;
    string instance_name = instance->get_name();

    ista::Pin* clock_pin = nullptr;
    auto* design_netlist = ista->get_netlist();
    auto* instance_temp = design_netlist->findInstance(instance_name.c_str());
    auto& the_graph = ista->get_graph();
    ista::Pin* sta_pin = nullptr;
    if (instance_temp == nullptr) {
      std::cout << "Warning: Instance " << instance_name << " not found in iSTA netlist." << std::endl;
      continue;
    }
    FOREACH_INSTANCE_PIN(instance_temp, sta_pin)
    {
      if (sta_pin->get_cell_port()->isClock()) {
        // DEBUG
        auto curnet = sta_pin->get_net();
        FFs_str.insert(instance_name);
        if (curnet) {
          auto netname = curnet->getFullName();
          // std::cout << "Clock pin: " << sta_pin->getFullName() << " in net: " << netname << " isClock:" << curnet->isClockNet()
          //           << std::endl;

        } else {
          // std::cout << "Clock pin: " << sta_pin->getFullName() << " in net: NULL" << std::endl;
        }
        // DEBUG
        clock_pin = sta_pin;  // Found a clock pin
        clock_pins_str.push_back(string(sta_pin->get_own_instance()->get_name()) + sta_pin->get_name());
        auto sta_clk_vertex = the_graph.findVertex(clock_pin).value();  // input can only find vertex
        if (!sta_clk_vertex->getClockBucket().empty() && sta_clk_vertex->getPropClock()) {
          is_clock = true;
          break;
        } else {
          std::cout << "Warning: Clock pin " << clock_pin->getFullName() << " has no associated clock in STA." << std::endl;
        }
      }
    }

    if (is_clock) {
      auto sta_clk_vertex = the_graph.findVertex(clock_pin).value();  // input can only find vertex
      StaArc* setup_arc = nullptr;
      for (auto* snk_arc : sta_clk_vertex->get_src_arcs()) {
        if (snk_arc->isSetupArc()) {
          setup_arc = snk_arc;
          break;
        }
      }

      assert(setup_arc != nullptr);
      // double setup_time_rise = FS_TO_PS(setup_arc->get_arc_delay(AnalysisMode::kMax, TransType::kRise));
      // double setup_time_fall = FS_TO_PS(setup_arc->get_arc_delay(AnalysisMode::kMax, TransType::kFall));
      assert(sta_clk_vertex->getPropClock());

      // note: us ```PS``` in the db.
      double clock_period = NS_TO_PS(sta_clk_vertex->getPropClock()->getPeriodNs());
      for (auto pin : instance->get_pin_list()->get_pin_list()) {
        string pin_full_name = instance->get_name() + pin->get_pin_name();
        // DEBUG
        if (pin_full_name.find("CLK") != string::npos || pin_full_name.find("clk") != string::npos) {
          // clock pin
          // std::cout << "Clock pin found: " << pin_full_name << std::endl;
        }
        // DEBUG
        if (mPinName2PyPinID.count(pin_full_name)) {
          assert(clock_pin->get_name() != pin->get_pin_name());
          // driven pin
          if (pin->is_net_pin() && pin->get_term()->get_direction() == IdbConnectDirection::kOutput) {
            start_points_str.push_back(pin_full_name);
            string sta_pin_name = instance->get_name() + ":" + pin->get_pin_name();
            level_bfs_queue.push_back(ista->findVertex(sta_pin_name.c_str()));
            double r_start_aat_ps = NS_TO_PS(
                timing_engine->getAT(
                    sta_pin_name.c_str(), AnalysisMode::kMax, TransType::kRise)
                    .value_or(0.0));
            double f_start_aat_ps = NS_TO_PS(
                timing_engine->getAT(
                    sta_pin_name.c_str(), AnalysisMode::kMax, TransType::kFall)
                    .value_or(0.0));
            double r_start_slew_ps = lookup_propagated_slew_ps(
                                         sta_pin_name, AnalysisMode::kMax, TransType::kRise)
                                         .value_or(NS_TO_PS(timing_engine->getSlew(
                                             sta_pin_name.c_str(), AnalysisMode::kMax, TransType::kRise)));
            double f_start_slew_ps = lookup_propagated_slew_ps(
                                         sta_pin_name, AnalysisMode::kMax, TransType::kFall)
                                         .value_or(NS_TO_PS(timing_engine->getSlew(
                                             sta_pin_name.c_str(), AnalysisMode::kMax, TransType::kFall)));
            // FF start-points should carry live launch arrival, not a hard-coded
            // zero, otherwise Python timing drops the launch-side clk->q AAT.
            sdc_inrdelays[pin_full_name] = r_start_aat_ps;
            sdc_infdelays[pin_full_name] = f_start_aat_ps;
            sdc_inrtrans[pin_full_name] = r_start_slew_ps;   //
            sdc_inftrans[pin_full_name] = f_start_slew_ps;   //
          } else if (pin->is_net_pin() && pin->get_term()->get_direction() == IdbConnectDirection::kInput) {
            end_points_str.push_back(pin_full_name);
            string sta_pin_name = instance->get_name() + ":" + pin->get_pin_name();
            level_bfs_queue_reverse.push_back(ista->findVertex(sta_pin_name.c_str()));
            sdc_outcaps[pin_full_name] = 0;
            // NOTE: clk -> d arc has been considered in timing propagation
            sdc_endpoints_fRAT[pin_full_name] = clock_period;  // - setup time - clock_uncertainty
            sdc_endpoints_rRAT[pin_full_name] = clock_period;  // - setup time - clock_uncertainty
          }
        } else if (mClkPin2ID.count(pin_full_name)) {
          // clock pin
          if (pin->is_net_pin() && pin->get_term()->get_direction() == IdbConnectDirection::kInput) {
            string _sta_pin_name = instance->get_name() + ":" + pin->get_pin_name();
            auto* sta_clk_pin_vertex = the_graph.findVertex(clock_pin).value();
            double r_clk_slew_ps = vertex_slew_ps(
                                       sta_clk_pin_vertex, AnalysisMode::kMax, TransType::kRise)
                                       .value_or(lookup_propagated_slew_ps(
                                           _sta_pin_name, AnalysisMode::kMax, TransType::kRise)
                                                     .value_or(NS_TO_PS(timing_engine->getSlew(
                                                         _sta_pin_name.c_str(), AnalysisMode::kMax, TransType::kRise))));
            double f_clk_slew_ps = vertex_slew_ps(
                                       sta_clk_pin_vertex, AnalysisMode::kMax, TransType::kFall)
                                       .value_or(lookup_propagated_slew_ps(
                                           _sta_pin_name, AnalysisMode::kMax, TransType::kFall)
                                                     .value_or(NS_TO_PS(timing_engine->getSlew(
                                                         _sta_pin_name.c_str(), AnalysisMode::kMax, TransType::kFall))));
            double r_clk_aat_ps = lookup_clock_arrival_ps(
                                      sta_clk_pin_vertex, AnalysisMode::kMax, TransType::kRise)
                                      .value_or(0.0);
            double f_clk_aat_ps = lookup_clock_arrival_ps(
                                      sta_clk_pin_vertex, AnalysisMode::kMax, TransType::kFall)
                                      .value_or(0.0);
            name2clk_pin_names[pin_full_name] = _sta_pin_name;
            name2clk_r_aat[pin_full_name] = r_clk_aat_ps;
            name2clk_f_aat[pin_full_name] = f_clk_aat_ps;
            name2clk_rtran[pin_full_name] = r_clk_slew_ps;  // Note: clk in slew from iSTA
            name2clk_ftran[pin_full_name] = f_clk_slew_ps;
          }
        }
      }
    }
  }
  // assert(name2clk_rtran.size() == FFs_str.size() * 2);

  for (auto& ff_name : FFs_str) {
    if (mNodeName2ID.count(ff_name)) {
      FF_ids.append(mNodeName2ID[ff_name]);
    }
  }
  if (!name2clk_ftran.empty()) {
    int num_clk_pins = mClkPin2ID.size();
    std::vector<double> raat_vector(num_clk_pins);
    std::vector<double> faat_vector(num_clk_pins);
    std::vector<double> rtrans_vector(num_clk_pins);
    std::vector<double> ftrans_vector(num_clk_pins);
    std::vector<std::string> clk_pin_names_vector(num_clk_pins);
    for (const auto& [clk_pin_name, clk_pin_id] : mClkPin2ID) {
      if (clk_pin_id < num_clk_pins && name2clk_rtran.count(clk_pin_name)) {
        raat_vector[clk_pin_id] = name2clk_r_aat[clk_pin_name];
        faat_vector[clk_pin_id] = name2clk_f_aat[clk_pin_name];
        rtrans_vector[clk_pin_id] = name2clk_rtran[clk_pin_name];
        ftrans_vector[clk_pin_id] = name2clk_ftran[clk_pin_name];
        clk_pin_names_vector[clk_pin_id] = name2clk_pin_names[clk_pin_name];
      }
    }
    clk_pin_r_aat = pybind11::cast(raat_vector);
    clk_pin_f_aat = pybind11::cast(faat_vector);
    clk_pin_rtran = pybind11::cast(rtrans_vector);
    clk_pin_ftran = pybind11::cast(ftrans_vector);
    clk_pin_names = pybind11::cast(clk_pin_names_vector);
  }
  for (auto& pin_name : start_points_str) {
    if (mPinName2PyPinID.count(pin_name)) {
      start_points.append(mPinName2PyPinID[pin_name]);
      inrdelays.append(sdc_inrdelays[pin_name]);  //
      infdelays.append(sdc_infdelays[pin_name]);  //
      inrtrans.append(sdc_inrtrans[pin_name]);    //
      inftrans.append(sdc_inftrans[pin_name]);    //
    }
  }
  for (auto& pin_name : clock_pins_str) {
    if (mClkPin2ID.count(pin_name)) {
      clock_pins.append(mClkPin2ID[pin_name]);
    }
  }
  for (auto& pin_name : end_points_str) {
    if (mPinName2PyPinID.count(pin_name)) {
      end_points.append(mPinName2PyPinID[pin_name]);
      outcaps.append(sdc_outcaps[pin_name]);  //
      // printf("End point: %s, pin id: %d, sdc_endpoints_rRAT: %.3f\n", pin_name.c_str(), mPinName2PyPinID[pin_name],
      // sdc_endpoints_rRAT[pin_name]);
      endpoints_rRAT.append(sdc_endpoints_rRAT[pin_name]);  //
      endpoints_fRAT.append(sdc_endpoints_fRAT[pin_name]);  //
    }
  }

#if 0
  /*-------------------------------- levelize using ista's vertex ------------------------------------------*/
  Graph forward_graph;  //
  Graph reverse_graph;  //
  std::unordered_set<std::string> all_nodes;
  for (auto inst : db_deisgn->get_instance_list()->get_instance_list()) {
    string inst_name = inst->get_name();
    if (!FFs_str.count(inst_name) && mNodeName2ID.count(inst_name)) {
      all_nodes.insert(inst_name);
    }
  }
  for (auto net : db_deisgn->get_net_list()->get_net_list()) {
    assert(net->get_driving_pin());
    if (isInvailidNet(net)) {
      continue;
    }
    auto from_inst = net->get_driving_pin()->get_instance();
    if (from_inst == nullptr) {
      continue;
    }
    auto from_inst_name = net->get_driving_pin()->get_instance()->get_name();

    if (FFs_str.count(from_inst_name) || from_inst->is_flip_flop()) {
      continue;
    }
    for (auto pin : net->get_load_pins()) {
      // TODO: Fix clock gate cell.
      // FIXME: clock_instance may be the clock gate cell, which is not a flip flop.
      // pin->get_instance()->is_clock_instance()
      if (pin->get_instance() == nullptr || pin->get_instance()->is_flip_flop()) {
        continue;
      }
      string to_inst_name = pin->get_instance()->get_name();
      if (FFs_str.count(to_inst_name)) {
        continue;
      }
      // printf(" %s -> %s\n", from_inst_name.c_str(), to_inst_name.c_str());
      forward_graph[from_inst_name].push_back(to_inst_name);
      reverse_graph[to_inst_name].push_back(from_inst_name);
    }
  }
  auto [_t1, forward_node_levels, forward_is_dag] = topologicalSortAndLevelize(all_nodes, forward_graph, db);
  auto [_t2, reverse_node_levels, reverse_is_dag] = topologicalSortAndLevelize(all_nodes, reverse_graph, db);
  assert(forward_is_dag && reverse_is_dag);
  std::map<int, std::vector<std::string>> forward_level_to_nodes;
  std::map<int, std::vector<std::string>> reverse_level_to_nodes;
  for (const auto& pair : forward_node_levels) {
    forward_level_to_nodes[pair.second].push_back(pair.first);
  }
  for (const auto& pair : reverse_node_levels) {
    reverse_level_to_nodes[pair.second].push_back(pair.first);
  }

  // pybind11::list cells_by_level;          //
  // pybind11::list cells_by_reverse_level;  //
  int level_cells_idx = 0;
  for (auto& [level, inst_list] : forward_level_to_nodes) {
    flat_cells_by_level_start.append(level_cells_idx);
    for (auto& inst_name : inst_list) {
      if (mNodeName2ID.count(inst_name)) {
        flat_cells_by_level.append(mNodeName2ID[inst_name]);
        level_cells_idx++;
      }
    }
  }
  flat_cells_by_level_start.append(level_cells_idx);

  int reverse_level_cells_idx = 0;
  for (auto& [level, inst_list] : reverse_level_to_nodes) {
    flat_cells_by_reverse_level_start.append(reverse_level_cells_idx);
    for (auto& inst_name : inst_list) {
      if (mNodeName2ID.count(inst_name)) {
        flat_cells_by_reverse_level.append(mNodeName2ID[inst_name]);
        reverse_level_cells_idx++;
      }
    }
  }
  flat_cells_by_reverse_level_start.append(reverse_level_cells_idx);

#endif
  /*--------------------------------net arcs init------------------------------------------*/
  int net_arc_count = 0;
  for (auto net : db_deisgn->get_net_list()->get_net_list()) {
    if (isInvailidNet(net)) {
      continue;
    }
    net_flat_arcs_start.append(net_arc_count);  //
    string pin_full_name;
    if (net->get_driving_pin()->is_io_pin()) {
      pin_full_name = net->get_driving_pin()->get_pin_name();
    } else {
      pin_full_name = net->get_driving_pin()->get_instance()->get_name() + net->get_driving_pin()->get_pin_name();
    }
    assert(mPinName2PyPinID.count(pin_full_name));
    int from_pin_id = mPinName2PyPinID[pin_full_name];
    int arc_num = 0;  //
    net2driver_pin_map.append(from_pin_id);
    for (auto pin : net->get_load_pins()) {
      string pin_full_name;
      if (pin->is_io_pin()) {
        pin_full_name = pin->get_pin_name();
      } else {
        pin_full_name = pin->get_instance()->get_name() + pin->get_pin_name();
      }
      assert(mPinName2PyPinID.count(pin_full_name));
      int to_pin_id = mPinName2PyPinID[pin_full_name];
      pybind11::list arc;
      arc.append(from_pin_id);
      arc.append(to_pin_id);
      net_flat_arcs.append(arc);
      arc_num++;
    }
    net_arc_count += arc_num;
  }
  net_flat_arcs_start.append(net_arc_count);

  /*--------------------------------cell main type init------------------------------------------*/
  vector<LibLibrary*> equiv_libs;
  auto& all_libs = _sta->getAllLib();
  int debug_total_lib_cells = 0;
  for (auto& lib : all_libs) {
    equiv_libs.push_back(lib.get());
    if (lib) {
      debug_total_lib_cells += static_cast<int>(lib->get_cells().size());
    }
  }
  std::vector<std::string> debug_probe_cells = {
      "AND5x1_ASAP7_75t_R",
      "DFFLQNx1_ASAP7_75t_R",
      "OA331x1_ASAP7_75t_R",
      "sram_asap7_16x256_1rw",
      "DECAPx1_ASAP7_75t_R",
      "TAPCELL_ASAP7_75t_R",
  };
  std::cout << "LibcellExportDebug all_libs=" << all_libs.size()
            << " total_lib_cells=" << debug_total_lib_cells << std::endl;
  for (const auto& cell_name : debug_probe_cells) {
    auto* probe = _sta->findLibertyCell(cell_name.c_str());
    std::cout << "LibcellExportDebug probe cell=" << cell_name
              << " found=" << (probe != nullptr) << std::endl;
  }
  auto family_build_result = openroad_libcell_family_builder::build(equiv_libs);
  std::unordered_map<std::string, int> openroad_member_order;
  for (const auto& family : family_build_result.families) {
    for (size_t member_idx = 0; member_idx < family.members.size(); ++member_idx) {
      openroad_member_order[family.members[member_idx]->get_cell_name()] = static_cast<int>(member_idx);
    }
  }

  int cell_flat_arc_count = 0;
  auto* db_layout = db->get_idb_layout();
  auto* cell_master_list = db_layout ? db_layout->get_cell_master_list() : nullptr;
  std::vector<ExportLibCellFamily> main_type_libcells = build_export_libcell_families(
      cell_master_list,
      _sta,
      family_build_result.cell_name_to_representative);
  for (auto& family : main_type_libcells) {
    std::stable_sort(family.members.begin(), family.members.end(), [&](const ExportLibCellMember& lhs, const ExportLibCellMember& rhs) {
      auto lhs_it = openroad_member_order.find(lhs.cell_name);
      auto rhs_it = openroad_member_order.find(rhs.cell_name);
      const int lhs_rank = lhs_it == openroad_member_order.end() ? INT_MAX : lhs_it->second;
      const int rhs_rank = rhs_it == openroad_member_order.end() ? INT_MAX : rhs_it->second;
      return lhs_rank < rhs_rank;
    });
  }
  std::map<std::string, std::unordered_map<std::string, int>> main_type_with_width;
  std::map<std::string, std::unordered_map<std::string, std::pair<string, double>>> main_type_2_vt_and_size;
  std::unordered_map<std::string, std::string> cell_type2main_type;

  std::map<std::string, int> main_type2main_id;
  std::unordered_map<std::string, int> cell_type2cell_id;  // cell_id = libcell_start[main_id] + width
  int main_id_idx = 0;
  int cell_id_idx = 0;

  for (auto& family : main_type_libcells) {
    std::string main_type = family.representative_name;
    std::vector<LibCell*> export_order_lib_cells;
    export_order_lib_cells.reserve(family.members.size());
    bool contains_non_sizeable_member = false;
    for (const auto& member : family.members) {
      cell_type2main_type[member.cell_name] = main_type;
      if (member.lib_cell != nullptr) {
        export_order_lib_cells.push_back(member.lib_cell);
      }
      if (member.lib_cell == nullptr || member.cell_master == nullptr || member.cell_master->is_block()) {
        contains_non_sizeable_member = true;
      }
    }
    bool family_is_sizeable = !contains_non_sizeable_member && has_consistent_arc_signature(main_type, export_order_lib_cells);
    if (!family_is_sizeable) {
      std::cout << "Warning: mark family as unsizable due to inconsistent arcs: "
                << main_type << std::endl;
    }
    main_id_is_sizeable.append(family_is_sizeable);
    main_type2main_id[main_type] = main_id_idx;

    // pre-calc vt,
    std::map<string, vector<LibCell*>> t_vt_cells;
    std::vector<vector<LibCell*>> vt_cells_vec;
    std::vector<string> vt_types;
    for (size_t size_idx = 0; size_idx < export_order_lib_cells.size(); ++size_idx) {
      string vt_type = get_vt_type(export_order_lib_cells[size_idx]->get_cell_name());
      t_vt_cells[vt_type].push_back(export_order_lib_cells[size_idx]);
    }
    for (auto& [vt_type, cell_list] : t_vt_cells) {
      // clang-format off
      std::sort(cell_list.begin(), cell_list.end(), [this](LibCell* a, LibCell* b) {
        const auto lhs = cell_size::build_cell_sort_key(a->get_cell_name(), export_libcell_leakage_mw_for_python(a), _process_node);
        const auto rhs = cell_size::build_cell_sort_key(b->get_cell_name(), export_libcell_leakage_mw_for_python(b), _process_node);
        return lhs < rhs;
      });
      // clang-format on
      vt_cells_vec.push_back(cell_list);
      vt_types.push_back(vt_type);
    }

    for (int vt_idx = 0; vt_idx < vt_types.size(); ++vt_idx) {
      if (vt_cells_vec[vt_idx].empty()) {
        continue;
      }
      double ref_leakage_power = export_libcell_leakage_mw_for_python(vt_cells_vec[vt_idx].at(0));
      printf("Cell main type: %s , leakage scales: ", vt_cells_vec[vt_idx][0]->get_cell_name());
      for (auto lib_cell : vt_cells_vec[vt_idx]) {
        double leakage_power = export_libcell_leakage_mw_for_python(lib_cell);
        double leakage_scale = ref_leakage_power > 0.0 ? leakage_power / ref_leakage_power : 0.0;
        double size = get_size_width(string(lib_cell->get_cell_name()));
        main_type_2_vt_and_size[main_type][lib_cell->get_cell_name()] = std::make_pair(vt_types[vt_idx], size);
        printf("%f ,", size);
      }
      printf("\n");
    }

    int size_limit = 0;
    for (const auto& vec : vt_cells_vec) {
      size_limit = std::max(size_limit, (int)vec.size());
    }
    pybind11::list main_id_limit;
    main_id_limit.append(main_type);
    main_id_limit.append(size_limit);
    main_id_limit.append(vt_types.size());
    flat_libcell_main_id2size_vt_limit.append(main_id_limit);

    for (size_t size_idx = 0; size_idx < family.members.size(); ++size_idx) {
      const auto& member = family.members[size_idx];
      const auto& cell_name = member.cell_name;
      if (!main_type_2_vt_and_size[main_type].count(cell_name)) {
        main_type_2_vt_and_size[main_type][cell_name] = std::make_pair(get_vt_type(cell_name), get_size_width(cell_name));
      }
      main_type_with_width[main_type][cell_name] = size_idx;
      cell_type2cell_id[cell_name] = cell_id_idx;

      pybind11::list cell_info;
      cell_info.append(cell_name);
      cell_info.append(main_id_idx);
      cell_info.append(main_type_2_vt_and_size[main_type][cell_name].second);
      cell_info.append(main_type_2_vt_and_size[main_type][cell_name].first);
      flat_libcell_info.append(cell_info);
      auto* cell_master = cell_master_list == nullptr ? nullptr : cell_master_list->find_cell_master(cell_name);
      if (cell_master == nullptr) {
        std::cerr << "Error: missing cell master for libcell geometry export, cell=" << cell_name << std::endl;
        assert(false);
      }
      flat_libcell_width.append(cell_master->get_width());
      flat_libcell_height.append(cell_master->get_height());
      flat_libcell_leakage.append(export_libcell_leakage_mw_for_python(member.lib_cell));

      cell_id_idx++;
    }
    main_id_idx++;
  }

  /*--------------------------------lib cell arcs info------------------------------------------*/
  /*
    info2arc_idx:   [main_type, from_lib_pin, to_lib_pin] -> arc_idx

    flat_luts_values: torch.Tensor      # [ARC_NUM, MaxT, MaxC]
    flat_luts_trans_table : torch.Tensor  # [ARC_NUM, MaxT]
    flat_luts_cap_table : torch.Tensor   # [ARC_NUM, MaxC]
    flat_luts_dim: torch.Tensor        # [ARC_NUM, 3] - Actual dims [trans_dim]

    cells [main_type, size, lib_arc_idx] -> [, arc_idx]

    cell_type_arc_start :

    main_type -> cell_types -> arc_idx ->
  */
  std::unordered_map<std::string, std::vector<TempLibArc>> info2lib_arcs;
  int lib_cell_idx = 0;
  int arc_idx = 0;
  int lib_pin_idx = 0;
  std::unordered_map<std::string, int> libpin_name2libpin_offset;
  auto append_libpin_offset = [&](const std::string& libcell_name, const std::string& raw_libpin_name) {
    if (cell_master_list == nullptr) {
      std::cerr << "Error: idb layout/cell master list is missing while exporting lib pin offsets" << std::endl;
      assert(false);
    }
    auto* cell_master = cell_master_list->find_cell_master(libcell_name);
    if (cell_master == nullptr) {
      std::cerr << "Error: missing cell master for lib pin offset export, cell=" << libcell_name << std::endl;
      assert(false);
    }
    std::string libpin_name = normalize_idb_term_name(raw_libpin_name);
    auto* term = find_representative_term(cell_master, raw_libpin_name);
    if (term == nullptr) {
      std::cerr << "Error: missing term for lib pin offset export, cell=" << libcell_name
                << " pin=" << libpin_name
                << " ; looked for exact term and bus-bit fallbacks"
                << std::endl;
      assert(false);
    }
    flat_lib_pin_offset_x.append(term->get_average_position().get_x());
    flat_lib_pin_offset_y.append(term->get_average_position().get_y());
  };
  for (auto& family : main_type_libcells) {
    std::string main_type = family.representative_name;
    const auto& family_members = family.members;
    //
    main_id_2_cell_id_start.append(lib_cell_idx);
    pybind11::list flat_luts_values[4];       // Forward delay flat LUT values
    pybind11::list flat_luts_trans_table[4];  // Forward delay flat LUT transition table
    pybind11::list flat_luts_cap_table[4];    // Forward delay flat LUT capacitance table
    pybind11::list flat_luts_dim[4];

    // const std::map<std::string_view, LibLutTableTemplate::Variable> LibLutTableTemplate::_str2var
    // = {{"total_output_net_capacitance", LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE},
    //    {"input_net_transition", LibLutTableTemplate::Variable::INPUT_NET_TRANSITION},
    //    {"related_pin_transition", LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION},
    //    {"constrained_pin_transition", LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION},
    //    {"input_transition_time", LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME},
    //    {"time", LibLutTableTemplate::Variable::TIME},
    //    {"input_voltage", LibLutTableTemplate::Variable::INPUT_VOLTAGE},
    //    {"output_voltage", LibLutTableTemplate::Variable::OUTPUT_VOLTAGE},
    //    {"input_noise_height", LibLutTableTemplate::Variable::INPUT_NOISE_HEIGHT},
    //    {"input_noise_width", LibLutTableTemplate::Variable::INPUT_NOISE_WIDTH},
    //    {"normalized_voltage", LibLutTableTemplate::Variable::NORMALIZED_VOLTAGE}};

    double default_cap = 0;
    int cell_idx_in_group = 0;
    for (const auto& member : family_members) {
      cell_id_2_arc_id_start.append(arc_idx);
      cell_id_2_libpin_id_start.append(lib_pin_idx);
      string cell_type_name = member.cell_name;
      double cell_size = main_type_2_vt_and_size[main_type][cell_type_name].second;
      auto* lib_cell = member.lib_cell;
      if (lib_cell == nullptr) {
        cell_idx_in_group++;
        continue;
      }

      std::vector<LibArc*> tmp_arc_lists;
      for (auto& arc_set : lib_cell->get_cell_arcs()) {
        for (auto& arc : arc_set->get_arcs()) {
          tmp_arc_lists.push_back(arc.get());
        }
      }

      std::sort(tmp_arc_lists.begin(), tmp_arc_lists.end(), arc_signature_less);
      int export_offset = 0;
      for (auto* arc : tmp_arc_lists) {
        if (!is_supported_export_timing_type(arc->get_timing_type())) {
          continue;
        }
        auto from_lib_pin = arc->get_src_port();
        auto to_lib_pin = arc->get_snk_port();

        pybind11::list arc_info;
        arc_info.append(from_lib_pin);
        arc_info.append(to_lib_pin);
        arc_info.append(lib_cell_idx + cell_idx_in_group);
        arc_info.append(export_offset);
        arc_info.append(lib_arc_timing_sense_to_int(arc));
        arc_info.append(lib_arc_timing_type_to_int(arc));
        flat_libarc_info.append(arc_info);

        string info = cell_type_name + "_" + from_lib_pin + "_" + to_lib_pin;
        if (arc->get_timing_type() == ista::LibArc::TimingType::kCombFall
            || arc->get_timing_type() == ista::LibArc::TimingType::kCombRise
            || arc->get_timing_type() == ista::LibArc::TimingType::kComb
            || arc->get_timing_type() == ista::LibArc::TimingType::kFallingEdge
            || arc->get_timing_type() == ista::LibArc::TimingType::kRisingEdge
            || arc->get_timing_type() == ista::LibArc::TimingType::kDefault) {

          // Group the current arc by its 'info' key.
          auto [iter, inserted] = info2lib_arcs.try_emplace(info, std::vector<TempLibArc>{});
          iter->second.emplace_back(arc_idx++, export_offset, arc);
          if (cell_type_name == "MUX2D0BWP40P140UHVT" && from_lib_pin == "S" && to_lib_pin == "Z") {
            std::cout << "find MUX2D0BWP40P140UHVT S->Z arc" << std::endl;
          }
          auto* lib_delay_model = dynamic_cast<LibDelayTableModel*>(arc->get_table_model());
          auto fall_delay_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kCellFall));
          auto rise_delay_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kCellRise));
          auto fall_trans_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kFallTransition));
          auto rise_trans_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kRiseTransition));

          // assert(fall_delay_table != nullptr);
          // assert(rise_delay_table != nullptr);

          std::string vt = get_vt_type(cell_type_name);
          init_lut_table_unified(f_delay_flat_luts_values, f_delay_flat_luts_trans_table, f_delay_flat_luts_cap_table,
                                 f_delay_flat_luts_dim, fall_delay_table, lib_cell, false, to_lib_pin, from_lib_pin, "cell_fall", main_type,
                                 cell_size, vt);
          init_lut_table_unified(r_delay_flat_luts_values, r_delay_flat_luts_trans_table, r_delay_flat_luts_cap_table,
                                 r_delay_flat_luts_dim, rise_delay_table, lib_cell, false, to_lib_pin, from_lib_pin, "cell_rise", main_type,
                                 cell_size, vt);
          init_lut_table_unified(f_trans_flat_luts_values, f_trans_flat_luts_trans_table, f_trans_flat_luts_cap_table,
                                 f_trans_flat_luts_dim, fall_trans_table, lib_cell, false, to_lib_pin, from_lib_pin, "fall_transition",
                                 main_type, cell_size, vt);
          init_lut_table_unified(r_trans_flat_luts_values, r_trans_flat_luts_trans_table, r_trans_flat_luts_cap_table,
                                 r_trans_flat_luts_dim, rise_trans_table, lib_cell, false, to_lib_pin, from_lib_pin, "rise_transition",
                                 main_type, cell_size, vt);
        } else if (arc->get_timing_type() == ista::LibArc::TimingType::kSetupRising
                   || arc->get_timing_type() == ista::LibArc::TimingType::kSetupFalling) {
          // 把rise setup 存到 r_delay

          // ----- 坐标轴 -----
          // 把clk trans 存到 trans
          // 把data trans 存到 cap

          // Group the current arc by its 'info' key.
          auto [iter, inserted] = info2lib_arcs.try_emplace(info, std::vector<TempLibArc>{});
          iter->second.emplace_back(arc_idx++, export_offset, arc);

          auto* lib_check_model = dynamic_cast<LibCheckTableModel*>(arc->get_table_model());
          auto rise_setup_constraint_table = lib_check_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kRiseConstrain));
          auto fall_setup_constraint_table = lib_check_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kFallConstrain));
          auto rise_hold_constraint_table = nullptr;
          auto fall_hold_constraint_table = nullptr;
          // lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kFallTransition));
          // lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kRiseTransition));
          // assert(fall_delay_table != nullptr);
          // assert(rise_delay_table != nullptr);

          std::string vt = get_vt_type(cell_type_name);
          init_lut_table_unified(r_delay_flat_luts_values, r_delay_flat_luts_trans_table, r_delay_flat_luts_cap_table,
                                 r_delay_flat_luts_dim, rise_setup_constraint_table, lib_cell, true, to_lib_pin, from_lib_pin, "rise_setup",
                                 main_type, cell_size, vt);
          init_lut_table_unified(f_delay_flat_luts_values, f_delay_flat_luts_trans_table, f_delay_flat_luts_cap_table,
                                 f_delay_flat_luts_dim, fall_setup_constraint_table, lib_cell, true, to_lib_pin, from_lib_pin, "fall_setup",
                                 main_type, cell_size, vt);
          init_lut_table_unified(r_trans_flat_luts_values, r_trans_flat_luts_trans_table, r_trans_flat_luts_cap_table,
                                 r_trans_flat_luts_dim, rise_hold_constraint_table, lib_cell, true, to_lib_pin, from_lib_pin, "rise_hold",
                                 main_type, cell_size, vt);
          init_lut_table_unified(f_trans_flat_luts_values, f_trans_flat_luts_trans_table, f_trans_flat_luts_cap_table,
                                 f_trans_flat_luts_dim, fall_hold_constraint_table, lib_cell, true, to_lib_pin, from_lib_pin, "fall_hold",
                                 main_type, cell_size, vt);
        }
        ++export_offset;
      }

      int pin_offset = 0;
      // Per-libcell pin timing constraints are flattened to {pF, ps} regardless
      // of the original liberty units.
      std::optional<double> default_slew_ns;
      if (auto* owner_lib = lib_cell->get_owner_lib(); owner_lib != nullptr) {
        default_slew_ns = owner_lib->get_default_max_transition();
      }
      for (auto& libpin : lib_cell->get_cell_ports()) {
        string libcell_name = lib_cell->get_cell_name();
        string libpin_name = libpin->get_port_name();
        string info = libcell_name + "_" + libpin_name;
        libpin_name2libpin_offset[info] = pin_offset++;
        lib_pin_idx++;
        append_libpin_offset(libcell_name, libpin_name);
        if (libpin->isInput()) {
          auto cap = pydb_test::export_lib_pin_cap_for_python_pf(libpin->get_port_cap());
          auto rcap = libpin->get_port_cap(AnalysisMode::kMaxMin, TransType::kRise);
          auto fcap = libpin->get_port_cap(AnalysisMode::kMaxMin, TransType::kFall);
          flat_lib_pin_cap.append(cap);
          if (rcap.has_value() && fcap.has_value()) {
            flat_lib_pin_rcap.append(pydb_test::export_lib_pin_cap_for_python_pf(rcap.value()));
            flat_lib_pin_fcap.append(pydb_test::export_lib_pin_cap_for_python_pf(fcap.value()));
          } else {
            flat_lib_pin_rcap.append(cap);
            flat_lib_pin_fcap.append(cap);
          }
        } else {
          flat_lib_pin_cap.append(0);
          flat_lib_pin_rcap.append(0);
          flat_lib_pin_fcap.append(0);
        }
        double cap_limit = pydb_test::export_lib_pin_cap_limit_for_python_pf(
            libpin->get_port_cap_limit(AnalysisMode::kMax), default_cap);
        double slew_limit = pydb_test::resolve_lib_pin_slew_limit_for_python_ps(
            libpin->get_port_slew_limit(AnalysisMode::kMax), default_slew_ns);
        // flat_lib_pin_cap* are exported in pF; flat_lib_pin_slew_limit in ps.
        flat_lib_pin_cap_limit.append(cap_limit);
        flat_lib_pin_slew_limit.append(slew_limit);
      }

      for (auto& libpin : lib_cell->get_cell_buses()) {
        // && lib_cell->get_cell_port_or_port_bus(const char *port_name)
        string libcell_name = lib_cell->get_cell_name();
        string libpin_name = libpin->get_port_name();
        string info = libcell_name + "_" + libpin_name;
        // if (libcell_name == "TS5N28HPCPLVTA256X64M2FW") {
        //   std::cout << info << std::endl;
        //   printf("HHH\n");
        // }
        libpin_name2libpin_offset[info] = pin_offset++;
        lib_pin_idx++;
        append_libpin_offset(libcell_name, libpin_name);
        if (libpin->isInput()) {
          auto cap = pydb_test::export_lib_pin_cap_for_python_pf(libpin->get_port_cap());
          auto rcap = libpin->get_port_cap(AnalysisMode::kMaxMin, TransType::kRise);
          auto fcap = libpin->get_port_cap(AnalysisMode::kMaxMin, TransType::kFall);
          flat_lib_pin_cap.append(cap);
          if (rcap.has_value() && fcap.has_value()) {
            flat_lib_pin_rcap.append(pydb_test::export_lib_pin_cap_for_python_pf(rcap.value()));
            flat_lib_pin_fcap.append(pydb_test::export_lib_pin_cap_for_python_pf(fcap.value()));
          } else {
            flat_lib_pin_rcap.append(cap);
            flat_lib_pin_fcap.append(cap);
          }
        } else {
          flat_lib_pin_cap.append(0);
          flat_lib_pin_rcap.append(0);
          flat_lib_pin_fcap.append(0);
        }
        double cap_limit = pydb_test::export_lib_pin_cap_limit_for_python_pf(
            libpin->get_port_cap_limit(AnalysisMode::kMax), default_cap);
        double slew_limit = pydb_test::resolve_lib_pin_slew_limit_for_python_ps(
            libpin->get_port_slew_limit(AnalysisMode::kMax), default_slew_ns);
        // Bus ports share the same exported unit convention as scalar ports:
        // capacitance in pF and slew limits in ps.
        flat_lib_pin_cap_limit.append(cap_limit);
        flat_lib_pin_slew_limit.append(slew_limit);
      }
      // arc_idx += num_arcs;
      cell_idx_in_group++;
    }
    lib_cell_idx += family_members.size();
  }
  main_id_2_cell_id_start.append(lib_cell_idx);
  cell_id_2_arc_id_start.append(arc_idx);
  cell_id_2_libpin_id_start.append(lib_pin_idx);

  /*--------------------pin2libpin_offset-------------------------------*/
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    if (isInvailidNet(net)) {
      continue;
    }
    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      auto lib_cell = pin->get_instance()->get_cell_master();
      string libcell_name = lib_cell->get_name();
      string libpin_name = normalize_idb_term_name(pin->get_term_name());
      string info = libcell_name + "_" + libpin_name;  // sg13g2_o21ai_1
      if (!libpin_name2libpin_offset.count(info)) {
        std::cerr << "Error: missing libpin offset, cell=" << libcell_name
                  << " pin=" << libpin_name
                  << " ; static lib mapping must stay valid regardless of sizeable flag"
                  << std::endl;
        assert(false);
      }
      pin_2_libpin_offset.append(libpin_name2libpin_offset[info]);
      // Pin const& pin = db.pin(i);
    }

    for (IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      pin_2_libpin_offset.append(-1);
      // Pin const& pin = db.pin(i);
    }
  }
  /*---------------------------------------------------*/
  auto append_arc = [&](int from_pin_id, int to_pin_id, int lib_cell_id, int inst_id, const TempLibArc& lib_arc, pybind11::list& target_list) {
    pybind11::list arc;
    arc.append(from_pin_id);
    arc.append(to_pin_id);
    arc.append(lib_cell_id);
    arc.append(lib_arc._lib_arc_idx);
    arc.append(lib_arc.senseToInt());
    arc.append(lib_arc.typeToInt());
    arc.append(lib_arc._lib_arc_offset);
    target_list.append(arc);
  };

  /*-------------------------------- levelize using ista's vertex ------------------------------------------*/
  // \ TODO: Using ista's levelization directly

  auto& the_graph = ista->get_graph();
  StaLevelization tmp_levelization;
  tmp_levelization(&the_graph);
  the_graph.sortVertexByLevel();
  std::vector<std::vector<StaInstArc*>> level_inst_arcs;
  StaVertex* the_vertex;
  FOREACH_VERTEX(&the_graph, the_vertex)
  {
    if (the_vertex->get_level() > 0 && (the_vertex->get_level() % 2) == 1) {
      // only propagate the vertex has clock slew or is input port.
      if (the_vertex->is_slew_prop()) {
        for (auto* the_arc : the_vertex->get_src_arcs()) {
          if (!the_arc->isInstArc()) {
            continue;
          }
          auto* the_inst_arc = dynamic_cast<StaInstArc*>(the_arc);
          if (the_inst_arc == nullptr) {
            continue;
          }
          auto* to_vertex = the_inst_arc->get_snk();
          auto* lib_arc = the_inst_arc->get_lib_arc();
          if (lib_arc == nullptr) {
            continue;
          }
          if (!(lib_arc->get_timing_type() == ista::LibArc::TimingType::kCombFall
                || lib_arc->get_timing_type() == ista::LibArc::TimingType::kCombRise
                || lib_arc->get_timing_type() == ista::LibArc::TimingType::kComb
                || lib_arc->get_timing_type() == ista::LibArc::TimingType::kFallingEdge
                || lib_arc->get_timing_type() == ista::LibArc::TimingType::kRisingEdge
                || lib_arc->get_timing_type() == ista::LibArc::TimingType::kDefault
                || lib_arc->get_timing_type() == ista::LibArc::TimingType::kSetupRising
                || lib_arc->get_timing_type() == ista::LibArc::TimingType::kSetupFalling)) {
            continue;
          }
          if (to_vertex->is_const()) {
            continue;
          }
          if (the_vertex->get_level() == 1) {
            assert(the_vertex->is_clock());
          }
          assert(the_inst_arc != nullptr);
          int level = the_vertex->get_level() / 2;
          if (level_inst_arcs.size() <= level) {
            level_inst_arcs.push_back(std::vector<StaInstArc*>());
          }
          level_inst_arcs[level].push_back(the_inst_arc);
        }
      }
    }
  }
  for (auto& arcs : level_inst_arcs) {
    std::stable_sort(arcs.begin(), arcs.end(), [](StaInstArc* a, StaInstArc* b) { return a->get_lib_arc() < b->get_lib_arc(); });
  }

  std::map<std::string, std::vector<int>> name_2_inst_arcs;
  std::set<string> unqie_inst_arcs;
  int inst_flat_arcs_idx = 0;
  graph_local.resize(mPinName2PyPinID.size());
  reverse_graph_local.resize(mPinName2PyPinID.size());
  auto encode_pin_pair = [](int src, int dst) -> int64_t { return (static_cast<int64_t>(src) << 32) | static_cast<uint32_t>(dst); };
  std::unordered_map<int64_t, std::vector<int>> pin_pair2arc_indices;
  pin_pair_arc_keys.attr("clear")();
  flat_pin_pair_arc_start.attr("clear")();
  flat_pin_pair_arc_indices.attr("clear")();
  for (auto& arcs : level_inst_arcs) {
    flat_inst_arcs_by_level_start.append(flat_inst_arcs_by_level.size());

    for (auto* arc : arcs) {
      auto* src_vertex = arc->get_src();
      auto* snk_vertex = arc->get_snk();
      string src_pin_name = src_vertex->get_design_obj()->get_name();
      string snk_pin_name = snk_vertex->get_design_obj()->get_name();
      string src_libpin_name = getLibPinName(src_pin_name);
      string snk_libpin_name = getLibPinName(snk_pin_name);
      // assert(mPinName2PyPinID.count(src_pin_name) && mPinName2PyPinID.count(snk_pin_name));
      // int from_pin_id = mPinName2PyPinID[src_pin_name];
      // int to_pin_id = mPinName2PyPinID[snk_pin_name];
      string node_name = arc->get_inst()->get_name();
      string cell_type = arc->get_inst()->get_inst_cell()->get_cell_name();
      assert(cell_type2cell_id.count(cell_type));
      string unique_inst_arc = node_name + "_" + src_pin_name + "_" + snk_pin_name;
      int py_inst_id = mNode2PyNodeID[node_name];
      if (unqie_inst_arcs.count(unique_inst_arc) == 0) {
        unqie_inst_arcs.insert(unique_inst_arc);
      } else {
        continue;
      }

      string info = cell_type + "_" + src_libpin_name + "_" + snk_libpin_name;
      assert(info2lib_arcs.count(info));
      auto& lib_arcs = info2lib_arcs[info];
      assert(lib_arcs.size() > 0);

      int from_lib_pin_id = 0;
      if (src_vertex->is_clock()) {
        from_lib_pin_id = mClkPin2ID[node_name + src_pin_name];
      } else {
        if (mPinName2PyPinID.count(node_name + snk_pin_name) == 0) {
          std::cout << "Error: pin is floating id for " << node_name + snk_pin_name << std::endl;
          exit(0);
        }
        from_lib_pin_id = mPinName2PyPinID[node_name + src_pin_name];
      }

      assert(mPinName2PyPinID.count(node_name + snk_pin_name));
      int to_lib_pin_id = mPinName2PyPinID[node_name + snk_pin_name];
      int lib_cell_id = cell_type2cell_id[cell_type];
      if (!src_vertex->is_clock()) {
        // CODEXTODO:
        graph_local[from_lib_pin_id].push_back(to_lib_pin_id);
        reverse_graph_local[to_lib_pin_id].push_back(from_lib_pin_id);
      }
      for (const auto& lib_arc : info2lib_arcs[info]) {
        if (node_name == "U1831" && src_pin_name == "S" && snk_pin_name == "Z") {
          printf("%s %s->%s arc, from_pin_id: %d, to_pin_id: %d, lib_cell_id: %d, lib_arc_idx: %d\n", node_name.c_str(),
                 src_pin_name.c_str(), snk_pin_name.c_str(), from_lib_pin_id, to_lib_pin_id, lib_cell_id, lib_arc._lib_arc_idx);
          // std::cout << node_name<<  B1->ZN lib_arc_idx" << lib_arc._lib_arc_idx << std::endl;
        }

        name_2_inst_arcs[node_name].push_back(inst_flat_arcs_idx);
        append_arc(from_lib_pin_id, to_lib_pin_id, lib_cell_id, py_inst_id, lib_arc, flat_inst_arcs_by_level);
        pin_pair2arc_indices[encode_pin_pair(from_lib_pin_id, to_lib_pin_id)].push_back(inst_flat_arcs_idx);
        inst_flat_arcs_idx++;
      }
    }
  }
  flat_inst_arcs_by_level_start.append(flat_inst_arcs_by_level.size());

  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    if (isInvailidNet(net)) {
      continue;
    }
    IdbPin* driver = net->get_driving_pin();
    std::string driver_name;
    if (driver->get_instance() != nullptr) {  // Instance Pin
      driver_name = driver->get_instance()->get_name() + driver->get_pin_name();
    } else {  // IO Pin
      driver_name = driver->get_pin_name();
    }
    assert(mPinName2PyPinID.count(driver_name));
    int from_id = mPinName2PyPinID[driver_name];
    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      if (pin == driver) {
        continue;
      }
      string inst_name = pin->get_instance()->get_name();
      int to_id = mPinName2PyPinID[inst_name + pin->get_pin_name()];
      graph_local[from_id].push_back(to_id);
      reverse_graph_local[to_id].push_back(from_id);
    }

    for (IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      if (pin == driver) {
        continue;
      }
      int to_id = mPinName2PyPinID[pin->get_pin_name()];
      graph_local[from_id].push_back(to_id);
      reverse_graph_local[to_id].push_back(from_id);
    }
  }

  if (!pin_pair2arc_indices.empty()) {
    std::vector<std::pair<int64_t, std::vector<int>>> pin_pair_entries;
    pin_pair_entries.reserve(pin_pair2arc_indices.size());
    for (auto& kv : pin_pair2arc_indices) {
      pin_pair_entries.emplace_back(kv.first, std::move(kv.second));
    }
    std::sort(pin_pair_entries.begin(), pin_pair_entries.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    int current_offset = 0;
    for (const auto& entry : pin_pair_entries) {
      int src = static_cast<int>(entry.first >> 32);
      int dst = static_cast<int>(static_cast<uint32_t>(entry.first & 0xffffffff));
      pybind11::list key;
      key.append(src);
      key.append(dst);
      pin_pair_arc_keys.append(key);
      flat_pin_pair_arc_start.append(current_offset);
      for (int arc_idx : entry.second) {
        flat_pin_pair_arc_indices.append(arc_idx);
        ++current_offset;
      }
    }
    flat_pin_pair_arc_start.append(current_offset);
  } else {
    flat_pin_pair_arc_start.append(0);
  }

  for (int i = 0; i < graph_local.size(); i++) {
    for (int j = 0; j < graph_local[i].size(); j++) {
      flat_pin_to_graph.append(graph_local[i][j]);
    }
    flat_pin_to_graph_start.append(flat_pin_to_graph.size());
  }
  flat_pin_to_graph_start.append(flat_pin_to_graph.size());

  for (int i = 0; i < reverse_graph_local.size(); i++) {
    for (int j = 0; j < reverse_graph_local[i].size(); j++) {
      flat_pin_to_graph_reverse.append(reverse_graph_local[i][j]);
    }
    flat_pin_to_graph_start_reverse.append(flat_pin_to_graph_reverse.size());
  }
  flat_pin_to_graph_start_reverse.append(flat_pin_to_graph_reverse.size());

  /*--------------------------------cell arcs init------------------------------------------*/
  /*--------------------------------cell arcs ------------------------------------------*/
  /*
  inst_flat_arcs: [inpin, outpin, lib_cell_idx, lib_cell_arc_idx, arc_type]
                  arc_type: 0 for neg, 1 for postive
  inst_flat_arcs_start
   inst_main_id;  // [num_main_type, ] libcell_main_id + libcell_offset -> libcell_id
   inst_libcell_offset;  // [num_main_type, ] libcell_main_id + libcell_offset -> libcell_id
  */
  int flat_inst_arc_idx = 0;
  // hadle cell arcs
  for (int i = 0; i < mNodeName2ID.size() - num_terminal_NIs - ext_blockage_num; ++i) {
    auto node_name = node_names[i].cast<std::string>();
    IdbInstance* node = inst_resort_list[mNodeName2ID[node_name]];
    inst_flat_arcs_start.append(flat_inst_arc_idx);

    string cell_type = node->get_cell_master()->get_name();
    std::string main_type = cell_type2main_type[cell_type];
    inst_main_id.append(main_type2main_id[main_type]);
    inst_libcell_offset.append(main_type_with_width[main_type][cell_type]);
    std::vector<IdbPin*> input_pins;
    std::vector<IdbPin*> output_pins;
    for (auto pin : node->get_pin_list()->get_pin_list()) {
      if (pin->get_term()->get_direction() == IdbConnectDirection::kInput) {
        input_pins.push_back(pin);
      } else if (pin->get_term()->get_direction() == IdbConnectDirection::kOutput) {
        output_pins.push_back(pin);
      } else if (pin->get_term()->get_direction() == IdbConnectDirection::kInOut) {
        // FIXME: handle inout pins
        // Inout pins are treated as input and output
        // input_pins.push_back(pin);
        // output_pins.push_back(pin);
      }
    }

    bool isFF = FFs_str.count(node_name);
    int py_inst_id = mNode2PyNodeID[node_name];

    // handle clk->D arcs (constraint arcs)
    if (isFF) {
      string clock_pin_name = "";
      for (int i = 0; i < input_pins.size(); i++) {
        auto input_pin = input_pins[i];
        string sta_full_pin_name = node_name + ":" + input_pin->get_pin_name();
        auto pin_vertex = _sta->findVertex(sta_full_pin_name.c_str());  // ensure the pin is in the sta graph
        if (pin_vertex->is_clock()) {
          clock_pin_name = input_pin->get_pin_name();
        }
      }
      for (IdbPin* input_pin : input_pins) {
        string to_lib_pin_name = input_pin->get_pin_name();
        string info = cell_type + "_" + clock_pin_name + "_" + to_lib_pin_name;
        if (info2lib_arcs.count(info)) {
          for (const auto& lib_arc : info2lib_arcs[info]) {
            int from_lib_pin_id = mClkPin2ID[node_name + clock_pin_name];
            if (mPinName2PyPinID.count(node_name + to_lib_pin_name) == 0) {
              std::cout << "Warning: pin is floating id for " << node_name + to_lib_pin_name << std::endl;
              continue;
            }
            assert(mPinName2PyPinID.count(node_name + to_lib_pin_name));
            int to_lib_pin_id = mPinName2PyPinID[node_name + to_lib_pin_name];
            int lib_cell_id = cell_type2cell_id[cell_type];
            append_arc(from_lib_pin_id, to_lib_pin_id, lib_cell_id, py_inst_id, lib_arc, endpoints_constraint_arcs);
          }
        }
      }
    }

    {
      // name_2_inst_arcs maps node name -> vector<int>, so lookup the vector and iterate its ints
      auto it_name_arcs = name_2_inst_arcs.find(node_name);
      if (it_name_arcs != name_2_inst_arcs.end()) {
        for (int inst_arc_idx : it_name_arcs->second) {
          inst_flat_arcs.append(inst_arc_idx);
          flat_inst_arc_idx++;
        }
      }
    }

#if 0
    for (int i = 0; i < input_pins.size(); i++) {
      for (int j = 0; j < output_pins.size(); j++) {
        auto input_pin = input_pins[i];
        auto output_pin = output_pins[j];
        string from_lib_pin = input_pin->get_pin_name();
        string to_lib_pin = output_pin->get_pin_name();
        string info_from_lib_pin = getLibPinName(from_lib_pin);
        string info_to_lib_pin = getLibPinName(to_lib_pin);
        string info = cell_type + "_" + info_from_lib_pin + "_" + info_to_lib_pin;
        if (info2lib_arcs.count(info)) {
          for (const auto& lib_arc : info2lib_arcs[info]) {
            int from_lib_pin_id = 0;
            if (isFF) {  // Handle clk->q arcs
              if (!mClkPin2ID.count(node_name + from_lib_pin)) {
                // FIXME:This isn't a clock pin, skip to avoid double counting
                continue;
              }
              from_lib_pin_id = mClkPin2ID[node_name + from_lib_pin];
            } else  // Handle combinational arcs
            {
              from_lib_pin_id = mPinName2PyPinID[node_name + from_lib_pin];
            }
            if (mPinName2PyPinID.count(node_name + to_lib_pin) == 0) {
              std::cout << "Warning: pin is floating id for " << node_name + to_lib_pin << std::endl;
              continue;
            }
            assert(mPinName2PyPinID.count(node_name + to_lib_pin));
            int to_lib_pin_id = mPinName2PyPinID[node_name + to_lib_pin];
            int lib_cell_id = cell_type2cell_id[cell_type];

            if (node_name == "U1831" && from_lib_pin == "S" && to_lib_pin == "Z") {
              printf("%s %s->%s arc, from_pin_id: %d, to_pin_id: %d, lib_cell_id: %d, lib_arc_idx: %d\n", node_name.c_str(),
                     from_lib_pin.c_str(), to_lib_pin.c_str(), from_lib_pin_id, to_lib_pin_id, lib_cell_id, lib_arc._lib_arc_idx);
              // std::cout << node_name<<  B1->ZN lib_arc_idx" << lib_arc._lib_arc_idx << std::endl;
            }

            append_arc(from_lib_pin_id, to_lib_pin_id, lib_cell_id, lib_arc, inst_flat_arcs);
            // inst_arc_idx++;
          }
        }
      }
    }
#endif
  }

  // blockage
  for (int i = 0; i < ext_blockage_num; i++) {
    inst_flat_arcs_start.append(flat_inst_arc_idx);
    inst_main_id.append(-1);
    inst_libcell_offset.append(-1);
  }
  // IO PINS
  for (int i = mNodeName2ID.size() - num_terminal_NIs; i < mNodeName2ID.size(); ++i) {
    inst_flat_arcs_start.append(flat_inst_arc_idx);
    inst_main_id.append(-1);
    inst_libcell_offset.append(-1);
  }
  inst_flat_arcs_start.append(flat_inst_arc_idx);
  /*---------------------------RC ---------------------------*/

  IdbLayerRouting* routing_layer = dynamic_cast<IdbLayerRouting*>(db->get_idb_layout()->get_layers()->get_routing_layers().at(1));
  // LEF routing widths are stored in DBU, so convert width to microns before
  // deriving per-micron RC for the downstream placer.
  double segment_width = (double) routing_layer->get_width() / db->get_idb_layout()->get_units()->get_micron_dbu();
  double lef_capacitance = routing_layer->get_capacitance();
  double lef_edge_capacitance = routing_layer->get_edge_capacitance();
  if (lef_capacitance != -1) {
    // c_unit is exported as pF / um.
    c_unit = (lef_capacitance * segment_width) + (lef_edge_capacitance * 2);  // pF per micron
  } else {
    c_unit = 0.16e-3;
  }
  double lef_resistance = routing_layer->get_resistance();
  if (lef_resistance != -1) {
    // r_unit is exported as ohm / um.
    r_unit = lef_resistance / segment_width;  // ohm per micron
  } else {
    r_unit = 2.535;
  }
}
#endif
}  // namespace python_interface
   // namespace python_interface
