
#include "PyPlaceDB.h"
// #include "ContestDriver.h"
#include <algorithm>
#include <boost/polygon/polygon.hpp>
#include <cassert>
#include <cstdio>
#include <string>
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
#include "idm.h"
#include "sdc/SdcSetIODelay.hh"
#include "sdc/SdcSetInputTransition.hh"
#include "sdc/SdcSetLoad.hh"
// #include "ContestDriver.h"
#include "PowerEngine.hh"
// #include "Power.hh"
#include <boost/polygon/polygon.hpp>
#include <vector>

namespace python_interface {

#if 1

static std::string IdbOrientToString(IdbOrient orient)
{
  switch (orient) {
    case IdbOrient::kNone:
      return "None";
    case IdbOrient::kN_R0:
      return "N_R0";
    case IdbOrient::kW_R90:
      return "W_R90";
    case IdbOrient::kS_R180:
      return "S_R180";
    case IdbOrient::kE_R270:
      return "E_R270";
    case IdbOrient::kFN_MY:
      return "FN_MY";
    case IdbOrient::kFE_MY90:
      return "FE_MY90";
    case IdbOrient::kFS_MX:
      return "FS_MX";
    case IdbOrient::kFW_MX90:
      return "FW_MX90";
    case IdbOrient::kMax:
      return "Max";
    default:
      return "Unknown";
  }
}

struct Box
{
  coordinate_type xl, yl, xh, yh;
  Box(coordinate_type xl, coordinate_type yl, coordinate_type xh, coordinate_type yh) : xl(xl), yl(yl), xh(xh), yh(yh) {}
  coordinate_type width() const { return xh - xl; }
  coordinate_type height() const { return yh - yl; }
  int64_t area() const { return 1LL * width() * height(); }
};

// inline std::pair<Box, bool> intersection(Box const& b1, Box const& b2, bool consider_touch = true)
// {
//   std::pair<Interval, bool> ivl[2] = {intersection(b1, b2, consider_touch), intersection(b1, b2, consider_touch)};
//   return std::make_pair(Box(ivl[kX].first, ivl[kY].first), ivl[kX].second && ivl[kY].second);
// }
/// \return true if two boxes have intersection
inline bool intersects(Box const& b1, Box const& b2, bool consider_touch = true)
{
  return intersects(b1, b2, consider_touch) && intersects(b1, b2, consider_touch);
}
inline double intersectDistance(Box const& i1, Box const& i2, bool is_x)
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
void PyPlaceDB::init_routability(idm::DataManager* db, std::vector<IdbInstance*> inst_resort_list)
{
  // routebilty driven placement
  // routing information initialized
  num_routing_grids_x = 0;
  num_routing_grids_y = 0;
  routing_grid_xl = xl;
  routing_grid_yl = yl;
  routing_grid_xh = xh;
  routing_grid_yh = yh;
  // int pitch = db->get_idb_layout()->get_track_grid_list()->get_track_grid_list()[0]->get_track()->get_pitch();
  // double tarck_width = db->get_idb_layout()->get_track_grid_list()->get_track_grid_list()[0]->;
  // double tarck_width = db->get_idb_layout()->get_track_grid_list()->get_track_grid_list()[0]->get_track()->get_width();

  // congestion map opt
  num_routing_grids_x = 256;
  num_routing_grids_y = 256;
  double routing_grids_size_x = std::round((routing_grid_xh - routing_grid_xl) / num_routing_grids_x);
  double routing_grids_size_y = std::round((routing_grid_yh - routing_grid_yl) / num_routing_grids_y);
  num_routing_grids_x = std::floor((routing_grid_xh - routing_grid_xl) / routing_grids_size_x);
  num_routing_grids_y = std::floor((routing_grid_yh - routing_grid_yl) / routing_grids_size_y);
  routing_grid_xh = routing_grid_xl + num_routing_grids_x * routing_grids_size_x;
  routing_grid_yh = routing_grid_yl + num_routing_grids_y * routing_grids_size_y;

  int track_layer_id = db->get_idb_layout()->get_track_grid_list()->get_track_grid_list()[0]->get_layer_list()[0]->get_id();
  for (index_type layer_idx = 0; layer_idx < db->get_idb_layout()->get_layers()->get_routing_layers_number(); ++layer_idx) {
    auto idb_layer = db->get_idb_layout()->get_layers()->get_routing_layers().at(layer_idx);
    idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
    if (idb_routing_layer->get_track_grid_list().empty()) {
      continue;
    }
    double track_ratio = idb_routing_layer->get_track_grid_list().size();
    for (IdbTrackGrid* track_grid : idb_routing_layer->get_track_grid_list()) {
      auto idb_track_grid = track_grid->get_track();

      int track_start = static_cast<int32_t>(idb_track_grid->get_start());
      int track_pitch = static_cast<int32_t>(idb_track_grid->get_pitch());
      int track_num = track_grid->get_track_num();
      if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionX) {
        unit_vertical_capacities.append(track_num / routing_grids_size_x);
        // track_axis.get_x_grid_list().push_back(track_grid);
      } else if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionY) {
        unit_horizontal_capacities.append(track_num / routing_grids_size_y);
      }
    }
  }
  // this is slightly different from db.routingGridOrigin
  // to be consistent with global placement
  int all_layer_num = db->get_idb_layout()->get_layers()->get_routing_layers_number();
  double routing_grid_area = routing_grids_size_x * routing_grids_size_y;
  std::vector<int> initial_horizontal_routing_map(all_layer_num * num_routing_grids_x * num_routing_grids_y, 0);
  std::vector<int> initial_vertical_routing_map(initial_horizontal_routing_map.size(), 0);

  for (unsigned int i = 0; i < inst_resort_list.size(); ++i) {
    IdbInstance* node = inst_resort_list.at(i);
    // Macro const& macro = db.macro(db.macroId(node));
    // else if (macro.className() != "DREAMPlace.PlaceBlockage") // fixed cells are special cases, skip placement blockages (looks like
    // ISPD2015 benchmarks do not process placement blockages)

    if (node->get_status() == IdbPlacementStatus::kFixed) {
      // Macro const& macro = db.macro(db.macroId(node));
      // printf("PyPlaceDB detect fixed cell: ");
      for (auto obs : node->get_cell_master()->get_obs_list()) {
        Box box(node->get_coordinate()->get_x(), node->get_coordinate()->get_y(), node->get_bounding_box()->get_high_x(),
                node->get_bounding_box()->get_high_y());
        // Box box(node.xl + obs_box.xl, node.yl + obs_box.yl, node.xl + obs_box.xh, node.yl + obs_box.yh);
        index_type grid_index_xl = std::max(int((box.xl - routing_grid_xl) / routing_grids_size_x), 0);
        index_type grid_index_yl = std::max(int((box.yl - routing_grid_yl) / routing_grids_size_y), 0);
        index_type grid_index_xh = std::min(unsigned((box.xh - routing_grid_xl) / routing_grids_size_x) + 1, num_routing_grids_x);
        index_type grid_index_yh = std::min(unsigned((box.yh - routing_grid_yl) / routing_grids_size_y) + 1, num_routing_grids_y);
        for (index_type k = grid_index_xl; k < grid_index_xh; ++k) {
          coordinate_type grid_xl = routing_grid_xl + k * routing_grids_size_x;
          coordinate_type grid_xh = grid_xl + routing_grids_size_x;
          for (index_type h = grid_index_yl; h < grid_index_yh; ++h) {
            coordinate_type grid_yl = routing_grid_yl + h * routing_grids_size_y;
            coordinate_type grid_yh = grid_yl + routing_grids_size_y;
            Box grid_box(grid_xl, grid_yl, grid_xh, grid_yh);
            for (auto obs_layer : obs->get_obs_layer_list()) {
              int layer_idx = obs_layer->get_shape()->get_layer()->get_id();
              index_type index = layer_idx * num_routing_grids_x * num_routing_grids_y + (k * num_routing_grids_y + h);
              double intersect_ratio = intersectArea(box, grid_box) / routing_grid_area;
              // dreamplaceAssert(intersect_ratio <= 1);
              auto idb_layer = db->get_idb_layout()->get_layers()->get_routing_layers().at(layer_idx);
              idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
              if (idb_routing_layer->get_track_grid_list().empty()) {
                continue;
              }
              for (IdbTrackGrid* track_grid : idb_routing_layer->get_track_grid_list()) {
                auto idb_track_grid = track_grid->get_track();
                int track_num = track_grid->get_track_num();
                if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionX) {
                  initial_vertical_routing_map[index] += ceil(intersect_ratio * track_num);
                  // track_axis.get_x_grid_list().push_back(track_grid);
                } else if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionY) {
                  // int track_num = static_cast<int32_t>((routing_grid_xh - routing_grid_xl) / track_pitch);
                  initial_horizontal_routing_map[index] += ceil(intersect_ratio * track_num);
                  // track_axis.get_y_grid_list().push_back(track_grid);
                }
              }
            }
            // printf("Instance %s, Coordinate (%d, %d, %d, %d)\n", node->get_name().c_str(), node->get_coordinate()->get_x(),
            //        node->get_coordinate()->get_y(), node->get_bounding_box()->get_high_x(),
            //        node->get_bounding_box()->get_high_y());
          }
        }
      }
    }
  }

  for (auto blockage : db->get_idb_design()->get_blockage_list()->get_blockage_list()) {
    if (!blockage->is_routing_blockage()) {
      continue;
    }
    IdbRoutingBlockage* routing_blockage = dynamic_cast<IdbRoutingBlockage*>(blockage);
    // auto rect = routing_blockage->get_rect();

    index_type layer = routing_blockage->get_layer()->get_id();
    idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(routing_blockage->get_layer());
    for (auto rect : blockage->get_rect_list()) {
      // convert to absolute box
      Box box(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      index_type grid_index_xl = std::max(int((box.xl - routing_grid_xl) / routing_grids_size_x), 0);
      index_type grid_index_yl = std::max(int((box.yl - routing_grid_yl) / routing_grids_size_y), 0);
      index_type grid_index_xh = std::min(unsigned((box.xh - routing_grid_xl) / routing_grids_size_x) + 1, num_routing_grids_x);
      index_type grid_index_yh = std::min(unsigned((box.yh - routing_grid_yl) / routing_grids_size_y) + 1, num_routing_grids_y);
      for (index_type k = grid_index_xl; k < grid_index_xh; ++k) {
        coordinate_type grid_xl = routing_grid_xl + k * routing_grids_size_x;
        coordinate_type grid_xh = grid_xl + routing_grids_size_x;
        for (index_type h = grid_index_yl; h < grid_index_yh; ++h) {
          coordinate_type grid_yl = routing_grid_yl + h * routing_grids_size_y;
          coordinate_type grid_yh = grid_yl + routing_grids_size_y;
          Box grid_box(grid_xl, grid_yl, grid_xh, grid_yh);
          index_type index = layer * num_routing_grids_x * num_routing_grids_y + (k * num_routing_grids_y + h);
          double intersect_ratio = intersectArea(box, grid_box) / routing_grid_area;
          // dreamplaceAssert(intersect_ratio <= 1);
          for (IdbTrackGrid* track_grid : idb_routing_layer->get_track_grid_list()) {
            auto idb_track_grid = track_grid->get_track();
            int track_num = track_grid->get_track_num();
            if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionX) {
              // track_axis.get_x_grid_list().push_back(track_grid);
              initial_vertical_routing_map[index] += ceil(intersect_ratio * track_num);
            } else if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionY) {
              // int track_num = static_cast<int32_t>((routing_grid_xh - routing_grid_xl) / track_pitch);
              initial_horizontal_routing_map[index] += ceil(intersect_ratio * track_num);
              // track_axis.get_y_grid_list().push_back(track_grid);
            }
          }
          // initial_horizontal_routing_map[index] += ceil(intersect_ratio * db.numRoutingTracks(PlanarDirectEnum::HORIZONTAL, layer));
          // initial_vertical_routing_map[index] += ceil(intersect_ratio * db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer));
          if (layer == 2) {
            // dreamplaceAssert(db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer) == 0);
            // dreamplaceAssertMsg(initial_vertical_routing_map[index] == 0,
            //                     "intersect_ratio %g, initial_vertical_routing_map[%u] = %d, capacity %u, product %g",
            //                     intersect_ratio, index, initial_vertical_routing_map[index],
            //                     db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer), intersect_ratio *
            //                     db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer));
          }
        }
      }
    }
  }
  // clamp maximum for overlapping fixed cells
  for (index_type layer = 0; layer < all_layer_num; ++layer) {
    for (int i = 0, ie = num_routing_grids_x * num_routing_grids_y; i < ie; ++i) {
      auto idb_layer = db->get_idb_layout()->get_layers()->get_routing_layers().at(layer);
      idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
      for (IdbTrackGrid* track_grid : idb_routing_layer->get_track_grid_list()) {
        auto idb_track_grid = track_grid->get_track();
        int track_num = track_grid->get_track_num();
        if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionX) {
          auto& vvalue = initial_vertical_routing_map[layer * ie + i];
          vvalue = std::min(vvalue, track_num);
        } else if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionY) {
          auto& hvalue = initial_horizontal_routing_map[layer * ie + i];
          hvalue = std::min(hvalue, track_num);
        }
      }
    }
  }
  for (auto item : initial_horizontal_routing_map) {
    initial_horizontal_demand_map.append(item);
  }
  for (auto item : initial_vertical_routing_map) {
    initial_vertical_demand_map.append(item);
  }
}

using Graph = std::unordered_map<std::string, std::vector<std::string>>;
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
    const Graph& graph, const std::vector<std::string>& start_nodes = {})
{
  // --- Initialization ---
  std::unordered_map<std::string, int> in_degree;  // Stores the in-degree of each node
  std::unordered_map<std::string, int> levels;     // Stores the level of each node
  std::unordered_set<std::string> all_nodes;       // Stores all unique nodes in the graph

  // 1. Collect all nodes and calculate in-degrees (based on the provided graph)
  // Note: The caller must ensure that the graph structure is correct and reflects the dependencies between nodes.
  for (const auto& pair : graph) {
    all_nodes.insert(pair.first);  // Add source node
    for (const std::string& neighbor : pair.second) {
      in_degree[neighbor]++;
      all_nodes.insert(neighbor);  // Add target node
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
    // The number of nodes in the topological order is less than the total number of nodes, indicating a cycle
    std::cerr << "Error: A cycle was detected in the graph! Topological sort failed." << std::endl;
    // Return empty results and false
    return std::make_tuple(std::vector<std::string>(), std::unordered_map<std::string, int>(), false);
  }
}

void PyPlaceDB::init_timing(idm::DataManager* db, std::unordered_map<std::string, int>& mPin2ID,
                            std::map<std::string, index_type>& mNodeName2ID, std::vector<IdbInstance*>& inst_resort_list,
                            std::map<std::string, std::vector<index_type>>& mNode2NewNodes, int ext_blockage_num)
{
  /*************************************************************************/
  /*************************************************************************/
  /*--------------------------------timing init------------------------------------------*/

  /*--------------------------------topo init------------------------------------------*/
  /* topo*/

  // pybind11::list cells_by_level;          //
  // pybind11::list cells_by_reverse_level;  //
  auto db_deisgn = db->get_idb_design();

  std::vector<string> start_points_str;  // PIs and FFs' output pins
  std::vector<string> end_points_str;    // POs and FFs' input pins
  for (auto pin : db_deisgn->get_io_pin_list()->get_pin_list()) {
    if (pin->get_net() == nullptr || pin->get_net()->is_ground() || pin->get_net()->is_power() || pin->get_net()->is_pdn()
        || pin->get_net()->is_clock()) {
      continue;
    }
    if (pin->is_primary_input()) {
      start_points_str.push_back(pin->get_pin_name());
    } else if (pin->is_primary_output()) {
      end_points_str.push_back(pin->get_pin_name());
    }
  }

  Graph forward_graph;  //
  Graph reverse_graph;  //
  for (auto instance : db_deisgn->get_instance_list()->get_instance_list()) {
    if (instance->is_flip_flop() || instance->is_clock_instance()) {
      for (auto pin : instance->get_pin_list()->get_pin_list()) {
        string pin_full_name = instance->get_name() + pin->get_pin_name();
        if (mPin2ID.count(pin_full_name)) {
          // driven pin
          if (pin->is_net_pin() && pin->get_term()->get_direction() == IdbConnectDirection::kOutput) {
            start_points_str.push_back(pin_full_name);
          } else if (pin->is_net_pin() && pin->get_term()->get_direction() == IdbConnectDirection::kInput) {
            // if (pin->) {
            // TODO: ignore clock pins
            end_points_str.push_back(pin_full_name);
            // }
          }
        }
      }
    }
  }
  for (auto net : db_deisgn->get_net_list()->get_net_list()) {
    if (net->is_ground() || net->is_power() || net->is_pdn() || net->is_clock() || net->get_instance_pin_list()->get_pin_list().size() == 0
        || net->get_driving_pin()->get_instance() == nullptr) {
      continue;
    }
    auto from_inst = net->get_driving_pin()->get_instance();
    if (from_inst->is_clock_instance() || from_inst->is_flip_flop()) {
      continue;
    }
    auto from_inst_name = net->get_driving_pin()->get_instance()->get_name();
    for (auto pin : net->get_load_pins()) {
      if (pin->get_instance() == nullptr || pin->get_instance()->is_flip_flop() || pin->get_instance()->is_clock_instance()) {
        continue;
      }
      string to_inst_name = pin->get_instance()->get_name();
      // printf(" %s -> %s\n", from_inst_name.c_str(), to_inst_name.c_str());
      forward_graph[from_inst_name].push_back(to_inst_name);
      reverse_graph[to_inst_name].push_back(from_inst_name);
    }
  }
  auto [_t1, forward_node_levels, forward_is_dag] = topologicalSortAndLevelize(forward_graph);
  auto [_t2, reverse_node_levels, reverse_is_dag] = topologicalSortAndLevelize(reverse_graph);
  assert(forward_is_dag && reverse_is_dag);
  std::unordered_map<int, std::vector<std::string>> forward_level_to_nodes;
  std::unordered_map<int, std::vector<std::string>> reverse_level_to_nodes;
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
        // is there a bug ?
        // FIXME:
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

  for (auto& pin_name : start_points_str) {
    if (mPin2ID.count(pin_name)) {
      start_points.append(mPin2ID[pin_name]);
    }
  }
  for (auto& pin_name : end_points_str) {
    if (mPin2ID.count(pin_name)) {
      end_points.append(mPin2ID[pin_name]);
    }
  }

  /*--------------------------------sdc init------------------------------------------*/
  auto timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto ista = timing_engine->get_ista();
  SdcConstrain* the_constrain = ista->getConstrain();
  auto& sdc_io_constraints = the_constrain->get_sdc_io_constraints();
  for (auto& io_constraint : sdc_io_constraints) {
    if (io_constraint->isSetInputDelay()) {
      auto set_io_delay = dynamic_cast<SdcSetIODelay*>(io_constraint.get());
      auto& objs = set_io_delay->get_objs();
      double delay_value = set_io_delay->get_delay_value();
      for (auto* obj : objs) {
        std::string pin_or_port_name = obj->getFullName();
        if (set_io_delay->isRise() && set_io_delay->isMax()) {
          // inrdelays[pin_or_port_name].append(delay_value);
        } else if (set_io_delay->isRise() && set_io_delay->isMin()) {
        } else if (set_io_delay->isFall() && set_io_delay->isMax()) {
          // infdelays[pin_or_port_name].append(delay_value);

        } else if (set_io_delay->isFall() && set_io_delay->isMin()) {
        }
      }

    } else if (io_constraint->isSetOutputDelay()) {
      // may be not need.
    } else if (io_constraint->isSetInputTransition()) {
      auto* set_input_transition = dynamic_cast<SdcSetInputTransition*>(io_constraint.get());
      double slew = set_input_transition->get_transition_value();
      auto& objs = set_input_transition->get_objs();
      for (auto* obj : objs) {
        if (set_input_transition->isMax() && set_input_transition->isRise()) {
          // inrtrans[obj->getFullName()].append(slew);
        } else if (set_input_transition->isMax() && set_input_transition->isFall()) {
          // inftrans[obj->getFullName()].append(slew);
        } else if (set_input_transition->isMin() && set_input_transition->isRise()) {
          // inrtrans[obj->getFullName()].append(slew);
        } else if (set_input_transition->isMin() && set_input_transition->isFall()) {
          // inftrans[obj->getFullName()].append(slew);
        }
      }
    } else if (io_constraint->isSetLoad()) {
      auto* set_load = dynamic_cast<SdcSetLoad*>(io_constraint.get());
      double load = set_load->get_load_value();
      auto& objs = set_load->get_objs();
      for (auto* obj : objs) {
        if (set_load->isMax() && set_load->isRise()) {
          // outcaps[obj->getFullName()].append(load);
        } else if (set_load->isMax() && set_load->isFall()) {
          // outcaps[obj->getFullName()].append(load);
        } else if (set_load->isMin() && set_load->isRise()) {
          // outcaps[obj->getFullName()].append(load);
        } else if (set_load->isMin() && set_load->isFall()) {
          // outcaps[obj->getFullName()].append(load);
        }
      }
    }
  }

  for (auto& pin_name : start_points_str) {
    if (mPin2ID.count(pin_name)) {
      // TODO:
      // ISTA need to read sdc to initialize
      inrdelays;  //
      infdelays;  //
      inrtrans;   //
      inftrans;   //
    }
  }
  for (auto& pin_name : end_points_str) {
    if (mPin2ID.count(pin_name)) {
      // TODO:
      outcaps;  //
    }
  }

  /*--------------------------------net arcs init------------------------------------------*/
  int net_arc_count = 0;
  for (auto net : db_deisgn->get_net_list()->get_net_list()) {
    if (net->is_ground() || net->is_power() || net->is_pdn() || net->is_clock()
        || net->get_instance_pin_list()->get_pin_list().size() == 0) {
      continue;
    }
    net_flat_arcs_start.append(net_arc_count);  //
    string pin_full_name;
    if (net->get_driving_pin()->is_io_pin()) {
      pin_full_name = net->get_driving_pin()->get_pin_name();
    } else {
      pin_full_name = net->get_driving_pin()->get_instance()->get_name() + net->get_driving_pin()->get_pin_name();
    }
    assert(mPin2ID.count(pin_full_name));
    int from_pin_id = mPin2ID[pin_full_name];
    int arc_num = 0;  //
    net2driver_pin_map.append(from_pin_id);
    for (auto pin : net->get_load_pins()) {
      string pin_full_name;
      if (pin->is_io_pin()) {
        pin_full_name = pin->get_pin_name();
      } else {
        pin_full_name = pin->get_instance()->get_name() + pin->get_pin_name();
      }
      assert(mPin2ID.count(pin_full_name));
      int to_pin_id = mPin2ID[pin_full_name];
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
  auto _timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto _sta = _timing_engine->get_ista();

  vector<LibLibrary*> equiv_libs;
  auto& all_libs = _sta->getAllLib();
  for (auto& lib : all_libs) {
    equiv_libs.push_back(lib.get());
  }

  _sta->makeClassifiedCells(equiv_libs);

  int cell_flat_arc_count = 0;
  std::unordered_set<std::string> main_types;
  std::unordered_map<std::string, std::unordered_map<std::string, int>> main_type_with_width;
  std::unordered_map<std::string, std::vector<LibCell*>> main_type_libcells;
  std::unordered_map<std::string, std::string> cell_type2main_type;

  std::unordered_map<std::string, int> main_type2main_id;
  std::unordered_map<std::string, int> cell_type2cell_id;  // cell_id = libcell_start[main_id] + width

  for (auto node : inst_resort_list) {
    string cell_type = node->get_cell_master()->get_name();
    string main_type;
    auto lib_cell = _sta->findLibertyCell(cell_type.c_str());
    if (_sta->classifyCells(lib_cell)) {
      main_type = _sta->classifyCells(lib_cell)->at(0)->get_cell_name();
    } else {
      main_type = cell_type;
    }
    main_types.insert(main_type);
  }
  int main_id_idx = 0;
  int cell_id_idx = 0;
  for (auto& [main_type, cell_types] : main_type_with_width) {
    auto main_lib_cell = _sta->findLibertyCell(main_type.c_str());
    Vector<LibCell*> lib_cells = *(_sta->classifyCells(main_lib_cell));
    main_type2main_id[main_type] = main_id_idx;
    std::sort(lib_cells.begin(), lib_cells.end(), [](LibCell* a, LibCell* b) {
      return a->get_leakage_power_list().at(0)->get_value() < a->get_leakage_power_list().at(0)->get_value();
    });
    for (int size = 0; size < lib_cells.size(); ++size) {
      main_type_libcells[main_type].push_back(lib_cells[size]);
      main_type_with_width[main_type][lib_cells[size]->get_cell_name()] = size;
      cell_type2main_type[lib_cells[size]->get_cell_name()] = main_type;
      cell_type2cell_id[lib_cells[size]->get_cell_name()] = cell_id_idx;
      cell_id_idx++;
    }
    main_id_idx++;
  }

  /*--------------------------------lib cell arcs info------------------------------------------*/
  /*
    info_2_arc_idx:   [main_type, from_lib_pin, to_lib_pin] -> arc_idx

    flat_luts_values: torch.Tensor      # [ARC_NUM, MaxT, MaxC]
    flat_luts_trans_table : torch.Tensor  # [ARC_NUM, MaxT]
    flat_luts_cap_table : torch.Tensor   # [ARC_NUM, MaxC]
    flat_luts_dim: torch.Tensor        # [ARC_NUM, 3] - Actual dims [trans_dim]

    cells [main_type, size, lib_arc_idx] -> [, arc_idx]

    cell_type_arc_start :

    main_type -> cell_types -> arc_idx ->
  */

  std::unordered_map<std::string, int> info_2_arc_idx;
  int main_type_id = 0;
  int lib_cell_idx = 0;
  int arc_idx = 0;

  for (auto& [main_type, lib_cells] : main_type_libcells) {
    // auto lib_cell_t = lib_cells.at(0);
    // for (auto& arc_set : lib_cell_t->get_cell_arcs()) {
    //   for (auto& arc : arc_set->get_arcs()) {
    //     auto from_lib_pin = arc->get_src_port();
    //     auto to_lib_pin = arc->get_snk_port();
    //     string info = main_type + "_" + from_lib_pin + "_" + to_lib_pin;
    //     info_2_arc_idx[info] = arc_idx++;
    //     if (arc->get_timing_type() != ista::LibArc::TimingType::kCombFall
    //         || arc->get_timing_type() != ista::LibArc::TimingType::kCombRise) {
    //       continue;
    //     }

    //     arc->get_timing_sense();
    //   }
    // }

    //
    cell_main_id_start.append(lib_cell_idx);
    pybind11::list flat_luts_values[4];       // Forward delay flat LUT values
    pybind11::list flat_luts_trans_table[4];  // Forward delay flat LUT transition table
    pybind11::list flat_luts_cap_table[4];    // Forward delay flat LUT capacitance table
    pybind11::list flat_luts_dim[4];

    const int TRAN_AXIS = 0;
    const int CAP_AXIS = 1;
    for (auto lib_cell : lib_cells) {
      libcell_arc_start.append(arc_idx);
      int num_arcs = 0;
      for (auto& arc_set : lib_cell->get_cell_arcs()) {
        for (auto& arc : arc_set->get_arcs()) {
          auto from_lib_pin = arc->get_src_port();
          auto to_lib_pin = arc->get_snk_port();
          string cell_type_name = lib_cell->get_cell_name();
          string info = cell_type_name + "_" + from_lib_pin + "_" + to_lib_pin;
          info_2_arc_idx[info] = arc_idx++;
          if (arc->get_timing_type() != ista::LibArc::TimingType::kCombFall
              || arc->get_timing_type() != ista::LibArc::TimingType::kCombRise) {
            continue;
          }
          num_arcs++;
          auto init_lut_table = [&](pybind11::list& flat_luts_values, pybind11::list& flat_luts_trans_table,
                                    pybind11::list& flat_luts_cap_table, pybind11::list& flat_luts_dim, LibTable* table) {
            int num_tran = table->get_axes().at(TRAN_AXIS)->get_axis_size();
            int num_cap = table->get_axes().at(CAP_AXIS)->get_axis_size();
            pybind11::list luts_dim;
            pybind11::list luts_values;
            pybind11::list luts_cap_table;
            pybind11::list luts_trans_table;

            luts_dim.append(num_tran);
            luts_dim.append(num_cap);

            for (auto& value : table->get_axes().at(CAP_AXIS)->get_axis_values()) {
              luts_cap_table.append(value.get());
            }
            for (auto& value : table->get_axes().at(TRAN_AXIS)->get_axis_values()) {
              luts_trans_table.append(value.get());
            }

            // luts_values??
            for (auto& value : table->get_table_values()) {
              luts_values.append(value.get());
            }
            flat_luts_values.append(luts_values);
            flat_luts_trans_table.append(luts_trans_table);
            flat_luts_cap_table.append(luts_cap_table);
            flat_luts_dim.append(luts_dim);
            // flat_luts_dim.append(table->get);
          };
          auto* lib_delay_model = dynamic_cast<LibDelayTableModel*>(arc->get_table_model());
          auto fall_delay_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kCellFall));
          auto rise_delay_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kCellRise));
          auto fall_trans_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kFallTransition));
          auto rise_trans_table = lib_delay_model->getTable(CAST_TYPE_TO_INDEX(LibTable::TableType::kRiseTransition));
          init_lut_table(f_delay_flat_luts_values, f_delay_flat_luts_trans_table, f_delay_flat_luts_cap_table, f_delay_flat_luts_dim,
                         fall_delay_table);
          init_lut_table(r_delay_flat_luts_values, r_delay_flat_luts_trans_table, r_delay_flat_luts_cap_table, r_delay_flat_luts_dim,
                         rise_delay_table);
          init_lut_table(f_trans_flat_luts_values, f_trans_flat_luts_trans_table, f_trans_flat_luts_cap_table, f_trans_flat_luts_dim,
                         fall_trans_table);
          init_lut_table(r_trans_flat_luts_values, r_trans_flat_luts_trans_table, r_trans_flat_luts_cap_table, r_trans_flat_luts_dim,
                         rise_trans_table);
        }
      }

      arc_idx += num_arcs;
    }

    lib_cell_idx += lib_cells.size();
  }
  libcell_arc_start.append(arc_idx);
  cell_main_id_start.append(lib_cell_idx);

  //

  /*--------------------------------cell arcs init------------------------------------------*/
  /*--------------------------------cell arcs ------------------------------------------*/
  /*
  cell_flat_arcs: [inpin, outpin, lib_cell_idx, lib_cell_arc_idx, arc_type]
                  arc_type: 0 for neg, 1 for postive
  cell_flat_arcs_start
   inst_main_id;  // [num_main_type, ] cell_main_id + cell_width -> cell_id
   inst_width;  // [num_main_type, ] cell_main_id + cell_width -> cell_id
  */
  int cell_flat_arcs_idx = 0;
  // hadle cell arcs
  for (int i = 0; i < mNode2NewNodes.size() - num_terminal_NIs - ext_blockage_num; ++i) {
    auto node_name = node_names[i].cast<std::string>();
    IdbInstance* node = inst_resort_list[mNodeName2ID[node_name]];
    cell_flat_arcs_start.append(cell_flat_arcs_idx);

    string cell_type = node->get_cell_master()->get_name();
    string main_type = cell_type2main_type[cell_type];
    std::vector<IdbPin*> input_pins;
    std::vector<IdbPin*> output_pins;
    inst_main_id.append(main_type2main_id[main_type]);
    inst_width.append(main_type_with_width[main_type][cell_type]);
    for (auto pin : node->get_pin_list()->get_pin_list()) {
      if (pin->get_term()->get_direction() == IdbConnectDirection::kInput) {
        input_pins.push_back(pin);
      } else if (pin->get_term()->get_direction() == IdbConnectDirection::kOutput) {
        output_pins.push_back(pin);
      }
    }
    for (int i = 0; i < input_pins.size(); i++) {
      for (int j = 0; j < output_pins.size(); j++) {
        auto input_pin = input_pins[i];
        auto output_pin = output_pins[j];
        string from_lib_pin = input_pin->get_pin_name();
        string to_lib_pin = output_pin->get_pin_name();
        string info = cell_type + "_" + from_lib_pin + "_" + to_lib_pin;
        if (info_2_arc_idx.count(info)) {
          int arc_idx = info_2_arc_idx[info];
          pybind11::list arc;
          arc.append(mPin2ID[node_name + from_lib_pin]);
          arc.append(mPin2ID[node_name + to_lib_pin]);
          arc.append(cell_type2cell_id[cell_type]);
          arc.append(arc_idx);
          cell_flat_arcs.append(arc);
          cell_flat_arcs_idx++;
        }
      }
    }
  }
  // blockage
  for (int i = 0; i < ext_blockage_num; i++) {
    cell_flat_arcs_start.append(cell_flat_arcs_idx);
    inst_main_id.append(-1);
    inst_width.append(-1);
  }
  // IO PINS
  for (int i = mNode2NewNodes.size() - num_terminal_NIs; i < mNode2NewNodes.size(); ++i) {
    cell_flat_arcs_start.append(cell_flat_arcs_idx);
    inst_main_id.append(-1);
    inst_width.append(-1);
  }
  cell_flat_arcs_start.append(cell_flat_arcs_idx);
}

void PyPlaceDB::set(idm::DataManager* db, bool with_sta)
{
  printf("PyPlaceDB::set start!!! Db address is %p\n", db);
  printf("PyPlaceDB::set start!!! idb_design address is %p\n", db->get_idb_design());

  using namespace idb;
  namespace gtl = boost::polygon;
  using namespace gtl::operators;
  typedef gtl::polygon_90_set_data<coordinate_type> PolygonSet;
  IdbDesign* db_deisgn = db->get_idb_design();
  num_terminal_NIs = 0;  // IO pins
  // num_terminal_NIs = 0;  // IO pins

  double total_fixed_node_area = 0;  // compute total area of fixed cells, which is an upper bound
  // collect boxes for fixed cells and put in a polygon set to remove overlap later
  std::vector<gtl::rectangle_data<coordinate_type>> fixed_boxes;
  // record original node to new node mapping
  int inst_num = db_deisgn->get_instance_list()->get_num();
  std::map<std::string, std::vector<index_type>> mNode2NewNodes;
  std::map<std::string, index_type> mNodeName2ID;
  int node_id = 0;
  std::vector<IdbInstance*> inst_resort_list = db_deisgn->get_instance_list()->get_instance_list();
  std::stable_sort(inst_resort_list.begin(), inst_resort_list.end(),
                   [](IdbInstance* a, IdbInstance* b) { return a->is_fixed() < b->is_fixed(); });
  for (IdbInstance* node : inst_resort_list) {
    mNodeName2ID[node->get_name()] = node_id++;
  }
  // for(auto io_pin : db_deisgn->get_io_pin_list()->get_pin_list()) {
  //   mNodeName2ID[io_pin->get_pin_name()] = -1;
  // }
  std::map<std::string, int> mNet2ID;
  int net_id = 0;
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    // is special net
    if (net->is_ground() || net->is_power() || net->is_pdn() || net->is_clock()
        || net->get_instance_pin_list()->get_pin_list().size() == 0) {
      continue;
    }

    mNet2ID[net->get_net_name()] = net_id++;
  }
  std::unordered_map<std::string, int> mPin2ID;
  int pin_id = 0;
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    if (net->is_ground() || net->is_power() || net->is_pdn() || net->is_clock()
        || net->get_instance_pin_list()->get_pin_list().size() == 0) {
      continue;
    }

    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      string inst_name = pin->get_instance()->get_name();
      mPin2ID[inst_name + pin->get_pin_name()] = pin_id++;
    }

    for (IdbPin* io_pin : net->get_io_pins()->get_pin_list()) {
      string inst_name = io_pin->get_pin_name();
      mPin2ID[io_pin->get_pin_name()] = pin_id++;
    }
  }
  // add a node to a bin
  auto addNode2Bin = [&](Box const& box) { fixed_boxes.emplace_back(box.xl, box.yl, box.xh, box.yh); };
  // general add a node
  auto addNode = [&](std::string orient_str, std::string const& name, Box const& box, bool isFixed) {
    // this id may be different from node id
    int id = node_names.size();
    node_name2id_map[pybind11::str(name)] = id;
    node_names.append(pybind11::str(name));
    dmInst->get_idb_design()->m_instID2Name.push_back(name);
    node_x.append(box.xl);
    node_y.append(box.yl);
    // printf("PyPlaceDB::set start!!! Db address is %lld\n", __LINE__);
    node_orient.append(pybind11::str(orient_str));  //
    node_size_x.append(box.width());
    node_size_y.append(box.height());
    // map new node to original index
    if (mNodeName2ID.count(name)) {
      node2orig_node_map.append(mNodeName2ID[name]);
    } else {
      node2orig_node_map.append(-1);
    }
    // record original node to new node mapping
    if (mNode2NewNodes.count(name) == 0) {
      mNode2NewNodes[name] = std::vector<index_type>();
    }

    mNode2NewNodes[name].push_back(id);
    if (isFixed) {
      // dreamplacePrint(kDEBUG, "node %s\n", db.nodeName(node).c_str());
      addNode2Bin(box);
    }
  };
#if 0
  // add obstruction boxes for fixed nodes
  // initialize node shapes from obstruction
  // I do not differentiate obstruction boxes at different layers
  // At least, this is true for DAC/ICCAD 2012 benchmarks
  auto addObsBoxes = [&](Node const& node, std::vector<Box<coordinate_type>> const& vBox, bool dist2map) {
    Box<coordinate_type> bbox;
    for (index_type i = 0; i < vBox.size(); ++i) {
      auto box = vBox[i];
      box.set(box.xl + node.xl, box.yl + node.yl, box.xh + node.xl, box.yh + node.yl);
      char buf[128];
      dreamplaceSPrint(kNONE, buf, "%s.DREAMPlace.Shape%u", db.nodeName(node).c_str(), i);
      addNode(node, std::string(buf), Orient(node.orient()), box, dist2map);
      bbox.encompass(box);
    }
    // compute the upper bound of fixed cell area
    if (dist2map) {
      total_fixed_node_area += bbox.area();
    }
  };
#endif
  num_terminals = 0;  // regard only fixed macros as macros, placement blockages are ignored
  for (unsigned int i = 0; i < inst_num; ++i) {
    IdbInstance* node = inst_resort_list.at(i);
    // Macro const& macro = db.macro(db.macroId(node));
    if (node->get_status() != IdbPlacementStatus::kFixed) {  // || i >= db.nodes().size() - num_terminal_NIs
      Box box_tmp(node->get_coordinate()->get_x(), node->get_coordinate()->get_y(), node->get_bounding_box()->get_high_x(),
                  node->get_bounding_box()->get_high_y());
      addNode(IdbOrientToString(node->get_orient()), node->get_name(), box_tmp, false);
    }
    // else if (macro.className() != "DREAMPlace.PlaceBlockage") // fixed cells are special cases, skip placement blockages (looks like
    // ISPD2015 benchmarks do not process placement blockages)
    else  // Jiaqi: To compare with NTUPlace4dr, we have to consider blockages in ISPD2015 benchmarks
    {
      // Macro const& macro = db.macro(db.macroId(node));
      // printf("PyPlaceDB detect fixed cell: ");
      if (false) {
#if 0
        MacroObs::ObsConstIterator foundObs = macro.obs().obsMap().find("Bookshelf.Shape");
        // add obstruction boxes for fixed nodes
        // initialize node shapes from obstruction
        // I do not differentiate obstruction boxes at different layers
        // At least, this is true for DAC/ICCAD 2012 benchmarks

        // put all boxes into a polygon set to remove overlaps
        // this can make the placement engine more robust
        PolygonSet ps;
        if (foundObs != macro.obs().end())  // for BOOKSHELF
        {
          for (auto const& box : foundObs->second) {
            ps.insert(gtl::rectangle_data<coordinate_type>(box.xl, box.yl, box.xh, box.yh));
          }
        } else {
          for (auto it = macro.obs().begin(), ite = macro.obs().end(); it != ite; ++it) {
            for (auto const& box : it->second) {
              ps.insert(gtl::rectangle_data<coordinate_type>(box.xl, box.yl, box.xh, box.yh));
            }
          }

          // I do not know whether we should add the bounding box of this fixed cell as well
          ps.insert(gtl::rectangle_data<coordinate_type>(0, 0, node.width(), node.height()));
        }

        // Get unique boxes without overlap for each fixed cell
        // However, there may still be overlapping between fixed cells.
        // We cannot eliminate these because we want to keep the mapping from boxes to cells.
        std::vector<gtl::rectangle_data<coordinate_type>> vRect;
        ps.get_rectangles(vRect);
        std::vector<Box<coordinate_type>> vBox;
        vBox.reserve(vRect.size());
        for (auto const& rect : vRect) {
          vBox.emplace_back(gtl::xl(rect), gtl::yl(rect), gtl::xh(rect), gtl::yh(rect));
        }
        addObsBoxes(node, vBox, true);
        num_terminals += vBox.size();
#endif
      } else {
        Box box_tmp(node->get_coordinate()->get_x(), node->get_coordinate()->get_y(), node->get_bounding_box()->get_high_x(),
                    node->get_bounding_box()->get_high_y());
        addNode(IdbOrientToString(node->get_orient()), node->get_name(), box_tmp, true);
        printf("Instance %s, Coordinate (%d, %d, %d, %d)\n", node->get_name().c_str(), node->get_coordinate()->get_x(),
               node->get_coordinate()->get_y(), node->get_bounding_box()->get_high_x(), node->get_bounding_box()->get_high_y());
        // addNode(node, db.nodeName(node), Orient(node.orient()), node, true);
        num_terminals += 1;
        // compute upper bound of total fixed cell area
        total_fixed_node_area += 1LL * node->get_cell_master()->get_height() * node->get_cell_master()->get_width();
      }
    }
  }

  // blockage
  int ext_blockage_num = 0;
  for (auto blockage : db->get_idb_design()->get_blockage_list()->get_blockage_list()) {
    if (!blockage->is_palcement_blockage()) {
      continue;
    }

    IdbPlacementBlockage* placement_blockage = dynamic_cast<IdbPlacementBlockage*>(blockage);

    // add obstruction boxes for fixed nodes
    // initialize node shapes from obstruction
    // I do not differentiate obstruction boxes at different layers
    // At least, this is true for DAC/ICCAD 2012 benchmarks

    // put all boxes into a polygon set to remove overlaps
    // this can make the placement engine more robust
    PolygonSet ps;
    for (auto rect : blockage->get_rect_list()) {
      // convert to absolute box
      Box box(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      ps.insert(gtl::rectangle_data<coordinate_type>(box.xl, box.yl, box.xh, box.yh));
    }
    for (unsigned int i = 0; i < inst_num; ++i) {
      IdbInstance* node = inst_resort_list.at(i);
      // Macro const& macro = db.macro(db.macroId(node));
      if (node->get_status() == IdbPlacementStatus::kFixed) {  // || i >= db.nodes().size() - num_terminal_NIs
        Box box_tmp(node->get_coordinate()->get_x(), node->get_coordinate()->get_y(), node->get_bounding_box()->get_high_x(),
                    node->get_bounding_box()->get_high_y());
        ps -= gtl::rectangle_data<coordinate_type>(box_tmp.xl, box_tmp.yl, box_tmp.xh, box_tmp.yh);
      }
    }
    // Get unique boxes without overlap for each fixed cell
    // However, there may still be overlapping between fixed cells.
    // We cannot eliminate these because we want to keep the mapping from boxes to cells.
    std::vector<gtl::rectangle_data<coordinate_type>> vRect;
    ps.get_rectangles(vRect);
    std::vector<Box> vBox;
    vBox.reserve(vRect.size());
    for (auto const& rect : vRect) {
      vBox.emplace_back(gtl::xl(rect), gtl::yl(rect), gtl::xh(rect), gtl::yh(rect));
      auto box = vBox.back();
      int id = node_names.size();
      string block_name = "blockage" + std::to_string(id);
      printf("PyPlaceDB detect fixed blockage: %s\n", block_name.c_str());
      addNode("R0", block_name, box, true);
      // record original node to new node mapping
      total_fixed_node_area += 1LL * box.area();
    }
    num_terminals += vBox.size();
    ext_blockage_num += vBox.size();
  }

  // IO PINS
  for (auto io_pin : db_deisgn->get_io_pin_list()->get_pin_list()) {
    int lx = io_pin->get_location()->get_x();
    int ly = io_pin->get_location()->get_y();
    Box box_tmp(lx, ly, lx + 1, ly + 1);
    addNode("R0", io_pin->get_pin_name(), box_tmp, true);
    printf("IO Pin %s, Coordinate (%d, %d)\n", io_pin->get_pin_name().c_str(), lx, ly);
    // addNode(node, db.nodeName(node), Orient(node.orient()), node, true);
    num_terminal_NIs += 1;
  }
  // we only know num_nodes when all fixed cells with shapes are expanded
  printf("num_terminals %d, numPlaceBlockages %u, num_terminal_NIs %d\n", num_terminals, ext_blockage_num, num_terminal_NIs);
  num_nodes = inst_num + ext_blockage_num + num_terminal_NIs;  // db.nodes().size() + num_terminals - db.numFixed() - db.numPlaceBlockages()
  // dreamplaceAssertMsg(num_nodes == node_x.size(),
  //                     "%u != %lu, db.nodes().size = %lu, num_terminals = %d, numFixed = %u, numPlaceBlockages = %u, num_terminal_NIs =
  //                     %d", num_nodes, node_x.size(), db.nodes().size(), num_terminals, db.numFixed(), db.numPlaceBlockages(),
  //                     num_terminal_NIs);

  // this is different from simply summing up the area of all fixed nodes
  double total_fixed_node_overlap_area = 0;
  // compute total area uniquely
  // std::cout << __LINE__ << std::endl;
  PolygonSet ps(gtl::HORIZONTAL, fixed_boxes.begin(), fixed_boxes.end());
  // critical to make sure only overlap with the die area is computed
  IdbRect* core_rect = db->get_idb_layout()->get_core()->get_bounding_box();
  ps &= gtl::rectangle_data<coordinate_type>(core_rect->get_low_x(), core_rect->get_low_y(), core_rect->get_high_x(),
                                             core_rect->get_high_y());
  total_fixed_node_overlap_area = gtl::area(ps);

  // the total overlap area should not exceed the upper bound;
  // current estimation may exceed if there are many overlapping fixed cells or boxes
  total_space_area = core_rect->get_area() - std::min(total_fixed_node_overlap_area, total_fixed_node_area);
  // dreamplacePrint(kDEBUG, "fixed area overlap: %g fixed area total: %g, space area = %g\n", total_fixed_node_overlap_area,
  //                 total_fixed_node_area, total_space_area);

  /*----------------------construct node2pin_map and flat_node2pin_map-------------------------------*/

  int count = 0;
  for (int i = 0; i < mNode2NewNodes.size() - num_terminal_NIs - ext_blockage_num; ++i) {
    auto node_name = node_names[i].cast<std::string>();
    IdbInstance* node = inst_resort_list[mNodeName2ID[node_name]];
    pybind11::list pins;
    int pin_num = 0;
    for (IdbPin* pin : node->get_pin_list()->get_pin_list()) {
      string inst_name = pin->get_instance()->get_name();
      // assert(mPin2ID.count(inst_name + pin->get_pin_name()));
      if (!mPin2ID.count(inst_name + pin->get_pin_name())) {
        continue;
      }
      int pin_id = mPin2ID[inst_name + pin->get_pin_name()];
      pins.append(pin_id);
      flat_node2pin_map.append(pin_id);
      pin_num += 1;
    }
    node2pin_map.append(pins);
    flat_node2pin_start_map.append(count);
    count += pin_num;
  }
  // blockage
  for (int i = 0; i < ext_blockage_num; i++) {
    node2pin_map.append(pybind11::list());
    flat_node2pin_start_map.append(count);
  }
  // IO PINS
  for (int i = mNode2NewNodes.size() - num_terminal_NIs; i < mNode2NewNodes.size(); ++i) {
    auto io_pin_name = node_names[i].cast<std::string>();
    pybind11::list pins;
    int pin_num = 0;
    if (mPin2ID.count(io_pin_name)) {
      int pin_id = mPin2ID[io_pin_name];
      pins.append(pin_id);
      flat_node2pin_map.append(pin_id);
      pin_num = 1;
    }
    node2pin_map.append(pins);
    flat_node2pin_start_map.append(count);
    count += pin_num;
  }
  flat_node2pin_start_map.append(count);

  /*-----------------------------------------------------*/
  num_movable_pins = 0;
  unsigned int pin_index = 0;
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    if (net->is_ground() || net->is_power() || net->is_pdn() || net->is_clock()
        || net->get_instance_pin_list()->get_pin_list().size() == 0) {
      continue;
    }
    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      // Pin const& pin = db.pin(i);
      IdbInstance* node = pin->get_instance();
      pin_direct.append(pybind11::str(IdbOrientToString(node->get_orient())));
      // for fixed macros with multiple boxes, put all pins to the first one
      index_type new_node_id = mNode2NewNodes[node->get_name()].at(0);
      IdbCoordinate<int>* inst_coord = pin->get_instance()->get_coordinate();
      IdbCoordinate<int>* pin_coord = pin->get_average_coordinate();
      // Pin::point_type pin_pos(node.pinPos(pin));
      pin_offset_x.append(pin_coord->get_x() - inst_coord->get_x());
      pin_offset_y.append(pin_coord->get_y() - inst_coord->get_y());
      pin2node_map.append(new_node_id);
      pin2net_map.append(mNet2ID[pin->get_net()->get_net_name()]);

      if (node->get_status() != IdbPlacementStatus::kFixed /*&& node.status() != PlaceStatusEnum::DUMMY_FIXED*/) {
        num_movable_pins += 1;
      }
    }

    for (IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      // Pin const& pin = db.pin(i);
      string io_pin_name = pin->get_pin_name();
      pin_direct.append(pybind11::str("R0"));
      // for fixed macros with multiple boxes, put all pins to the first one
      assert(mNode2NewNodes.count(io_pin_name));
      index_type new_node_id = mNode2NewNodes[io_pin_name].at(0);
      // Pin::point_type pin_pos(node.pinPos(pin));
      pin_offset_x.append(0);
      pin_offset_y.append(0);
      pin2node_map.append(new_node_id);
      pin2net_map.append(mNet2ID[pin->get_net()->get_net_name()]);
    }
  }

  count = 0;
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    if (net->is_ground() || net->is_power() || net->is_pdn() || net->is_clock()
        || net->get_instance_pin_list()->get_pin_list().size() == 0) {
      continue;
    }
    // Net const& net = db.net(i);
    net_weights.append(1);
    net_name2id_map[pybind11::str(net->get_net_name())] = mNet2ID[net->get_net_name()];
    net_names.append(pybind11::str(net->get_net_name()));
    pybind11::list pins;
    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      string inst_name = pin->get_instance()->get_name();
      pins.append(mPin2ID[inst_name + pin->get_pin_name()]);
    }
    for (IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      pins.append(mPin2ID[pin->get_pin_name()]);
    }
    net2pin_map.append(pins);
    int pin_num = 0;
    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      string inst_name = pin->get_instance()->get_name();
      flat_net2pin_map.append(mPin2ID[inst_name + pin->get_pin_name()]);
    }
    pin_num = net->get_instance_pin_list()->get_pin_num();
    for (IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      flat_net2pin_map.append(mPin2ID[pin->get_pin_name()]);
      pin_num += 1;
    }
    flat_net2pin_start_map.append(count);
    count += pin_num;
  }
  flat_net2pin_start_map.append(count);

  for (IdbRow* idb_row : db->get_idb_layout()->get_rows()->get_row_list()) {
    IdbRect* row_rect = idb_row->get_bounding_box();
    pybind11::tuple row
        = pybind11::make_tuple(row_rect->get_low_x(), row_rect->get_low_y(), row_rect->get_high_x(), row_rect->get_high_y());
    rows.append(row);
  }

  // initialize regions
  count = 0;

  for (IdbRegion* region : db->get_idb_design()->get_region_list()->get_region_list()) {
    // Region const& region = *it;
    pybind11::list boxes;
    for (IdbRect* itb : region->get_boundary()) {
      pybind11::tuple box = pybind11::make_tuple(itb->get_low_x(), itb->get_low_y(), itb->get_high_x(), itb->get_high_y());
      boxes.append(box);
      flat_region_boxes.append(box);
    }
    regions.append(boxes);
    flat_region_boxes_start.append(count);
    count += region->get_boundary().size();
  }
  flat_region_boxes_start.append(count);

  // I assume one cell only belongs to one FENCE region
  std::vector<int> vNode2FenceRegion(inst_num, std::numeric_limits<int>::max());
  int region_id = 0;
  for (IdbRegion* region : db->get_idb_design()->get_region_list()->get_region_list()) {
    // Group const& group = *it;
    if (region->get_type() == IdbRegionType::kFence) {
      for (IdbInstance* inst : region->get_instance_list()) {
        // FIXME:
        index_type node_id = mNode2NewNodes[inst->get_name()].at(0);
        if (inst->get_status() != IdbPlacementStatus::kFixed)  // ignore fixed cells
        {
          vNode2FenceRegion.at(node_id) = region_id;
        }
      }
    }
    region_id++;
  }
  for (std::vector<int>::const_iterator it = vNode2FenceRegion.begin(), ite = vNode2FenceRegion.end(); it != ite; ++it) {
    node2fence_region_map.append(*it);
  }

  xl = db->get_idb_layout()->get_core()->get_bounding_box()->get_low_x();
  yl = db->get_idb_layout()->get_core()->get_bounding_box()->get_low_y();
  xh = db->get_idb_layout()->get_core()->get_bounding_box()->get_high_x();
  yh = db->get_idb_layout()->get_core()->get_bounding_box()->get_high_y();

  // origin_xl = db.get_origin_xl;
  // origin_yl = db.get_origin_yl;
  // origin_xh = db.get_origin_xh;
  // origin_yh = db.get_origin_yh;
  // origin_row_height = db.get_origin_row_height();

  row_height = db->get_idb_layout()->get_rows()->get_row_height();
  site_width = db->get_idb_layout()->get_rows()->get_row_list().at(0)->get_site()->get_width();
#if 1

  init_routability(db, inst_resort_list);
  if (with_sta) {
    init_timing(db, mPin2ID, mNodeName2ID, inst_resort_list, mNode2NewNodes, ext_blockage_num);
  }
  printf("PyPlaceDB::set end!!!\n");

#endif
}
#endif
}  // namespace python_interface
   // namespace python_interface