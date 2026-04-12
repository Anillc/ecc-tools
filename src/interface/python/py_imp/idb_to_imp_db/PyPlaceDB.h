/**
 * @file   PyPlaceDB.h
 * @author Yibo Lin
 * @date   Apr 2020
 * @brief  Placement database for python
 */

#ifndef _DREAMPLACE_PLACE_IO_PYPLACEDB_H
#define _DREAMPLACE_PLACE_IO_PYPLACEDB_H

#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <sstream>

#include "IdbEnum.h"
#include "IdbInstance.h"
#include "idm.h"

// Forward declarations
namespace ista {
class LibTable;
class LibCell;
}  // namespace ista

// #include <boost/timer/timer.hpp>
namespace python_interface {
typedef int coordinate_type;
typedef int index_type;

struct Box
{
  coordinate_type xl, yl, xh, yh;
  Box(coordinate_type xl, coordinate_type yl, coordinate_type xh, coordinate_type yh) : xl(xl), yl(yl), xh(xh), yh(yh) {}
  coordinate_type width() const { return xh - xl; }
  coordinate_type height() const { return yh - yl; }
  int64_t area() const { return 1LL * width() * height(); }
};

double intersectDistance(Box const& i1, Box const& i2, bool is_x);

/// \return the intersection area of two boxes
double intersectArea(Box const& b1, Box const& b2);

bool isInvailidNet(IdbNet* net);

std::string IdbOrientToString(IdbOrient orient);
/// database for python
struct PyPlaceDB
{
 public:
  unsigned int num_nodes;           ///< number of nodes, including terminals and terminal_NIs
  unsigned int num_terminals;       ///< number of terminals, essentially fixed macros
  unsigned int num_terminal_NIs;    ///< number of terminal_NIs, essentially IO pins
  pybind11::dict node_name2id_map;  ///< node name to id map, cell name
  pybind11::list node_names;        ///< 1D array, cell name
  pybind11::list node_x;            ///< 1D array, cell position x
  pybind11::list node_y;            ///< 1D array, cell position y
  pybind11::list node_orient;       ///< 1D array, cell orientation
  pybind11::list node_size_x;       ///< 1D array, cell width
  pybind11::list node_size_y;       ///< 1D array, cell height

  pybind11::list node2orig_node_map;  ///< due to some fixed nodes may have non-rectangular shapes, we flat the node
                                      ///< list; this map maps the new indices back to the original ones

  pybind11::list pin_direct;    ///< 1D array, pin direction IO
  pybind11::list pin_offset_x;  ///< 1D array, pin offset x to its node
  pybind11::list pin_offset_y;  ///< 1D array, pin offset y to its node
  pybind11::list pin_names;     ///< 1D array, pin name

  pybind11::dict net_name2id_map;         ///< net name to id map
  pybind11::list net_names;               ///< net name
  pybind11::list net2pin_map;             ///< array of 1D array, each row stores pin id
  pybind11::list flat_net2pin_map;        ///< flatten version of net2pin_map
  pybind11::list flat_net2pin_start_map;  ///< starting index of each net in flat_net2pin_map
  pybind11::list net_weights;             ///< net weight

  pybind11::list node2pin_map;             ///< array of 1D array, contains pin id of each node
  pybind11::list flat_node2pin_map;        ///< flatten version of node2pin_map
  pybind11::list flat_node2pin_start_map;  ///< starting index of each node in flat_node2pin_map

  pybind11::list pin2node_map;  ///< 1D array, contain parent node id of each pin
  pybind11::list pin2net_map;   ///< 1D array, contain parent net id of each pin

  pybind11::list rows;  ///< NumRows x 4 array, stores xl, yl, xh, yh of each row

  pybind11::list regions;                  ///< array of 1D array, each region contains rectangles
  pybind11::list flat_region_boxes;        ///< flatten version of regions
  pybind11::list flat_region_boxes_start;  ///< starting index of each region in flat_region_boxes

  pybind11::list node2fence_region_map;  ///< only record fence regions for each cell

  // STA graph
  pybind11::list flat_pin_to_graph;       ///< flatten version of flat_pin_to_graph
  pybind11::list flat_pin_to_graph_start;  ///< starting index of each pin in flat_pin_to_graph
  pybind11::list flat_pin_to_graph_reverse;       ///< flatten version of flat_pin_to_graph_reverse
  pybind11::list flat_pin_to_graph_start_reverse;  ///< starting index of each pin in flat_pin_to_graph_reverse

  unsigned int num_routing_grids_x;  ///< number of routing grids in x
  unsigned int num_routing_grids_y;  ///< number of routing grids in y
  int routing_grid_xl;               ///< routing grid region may be different from placement region
  int routing_grid_yl;
  int routing_grid_xh;
  int routing_grid_yh;
  int dbu;                                       ///< database unit, used to convert coordinate to integer
  pybind11::list unit_horizontal_capacities;     ///< number of horizontal tracks of layers per unit distance
  pybind11::list unit_vertical_capacities;       /// number of vertical tracks of layers per unit distance
  pybind11::list initial_horizontal_demand_map;  ///< initial routing demand from fixed cells, indexed by (layer, grid x, grid y)
  pybind11::list initial_vertical_demand_map;    ///< initial routing demand from fixed cells, indexed by (layer, grid x, grid y)

  pybind11::list net2driver_pin_map;

  /* topo */
  pybind11::list FF_ids;         //
  pybind11::list start_points;   //
  pybind11::list end_points;     //
  pybind11::list clock_pins;     //
  pybind11::list clk_pin_r_aat;  //
  pybind11::list clk_pin_f_aat;  //
  pybind11::list clk_pin_rtran;  //
  pybind11::list clk_pin_ftran;  //
  pybind11::list clk_pin_names;  ///< 1D array, pin name

  pybind11::list flat_cells_by_level;          //
  pybind11::list flat_cells_by_reverse_level;  //

  pybind11::list flat_cells_by_level_start;          //
  pybind11::list flat_cells_by_reverse_level_start;  //

  /*
    [N, 7]
    from_pin_id
    to_pin_id
    libcell id 不区分vt和size，是确定的某个具体的libcell type
    libarc idx 具体的某个libarc 的位置，不区分arc type
    libarc timing sense
    libarc timing type
    libarc offset
    inst id
  */
  pybind11::list flat_inst_arcs_by_level;                // inst arcs by level for timing propagation
  pybind11::list flat_inst_arcs_by_level_start;          //

  pybind11::list pin_pair_arc_keys;               // 每个 (src, dst) 对应的 pin 对
  pybind11::list flat_pin_pair_arc_start;         // CSR 起始索引
  pybind11::list flat_pin_pair_arc_indices;       // 扁平化的弧索引列表

  pybind11::list flat_inst_arcs_by_reverse_level;          //
  pybind11::list flat_inst_arcs_by_reverse_level_start;     //

  /*sdc */
  pybind11::list inrdelays;  //
  pybind11::list infdelays;  //
  pybind11::list inrtrans;   //
  pybind11::list inftrans;   //
  pybind11::list outcaps;    //
  pybind11::list endpoints_rRAT;
  pybind11::list endpoints_fRAT;

  /*instance arcs */
  pybind11::list net_flat_arcs_start;   //
  pybind11::list net_flat_arcs;         //
  pybind11::list inst_flat_arcs_start;  //
  pybind11::list inst_flat_arcs;        //

  pybind11::list endpoints_constraint_arcs;  // constraint arcs,

  /* ------------------------info for gate sizing----------------------*/
  pybind11::list main_id_2_cell_id_start;  // CSR start offsets, shape = [num_main_type + 1]
                                           // main_id_2_cell_id_start[main_id] + libcell_offset -> cell_id
  pybind11::list cell_id_2_arc_id_start;   // CSR start offsets, shape = [num_lib_cells + 1]
                                           // cell_id_2_arc_id_start[cell_id] + arc_offset -> arc

  pybind11::list inst_main_id;  // [inst_num, ] libcell_main_id + libcell_offset -> libcell_id
  pybind11::list inst_libcell_offset;     // [inst_num, ] libcell_main_id + libcell_offset -> libcell_id

  pybind11::list cell_id_2_libpin_id_start;  // CSR start offsets, shape = [num_lib_cells + 1]
                                             // cell_id_2_libpin_id_start[cell_id] + lib_pin_offset -> libpin_id
  pybind11::list pin_2_libpin_offset;
  pybind11::list flat_lib_pin_offset_x;   // [num_lib_pins, ] lib-pin offset x aligned with libpin_id
  pybind11::list flat_lib_pin_offset_y;   // [num_lib_pins, ] lib-pin offset y aligned with libpin_id
  pybind11::list flat_lib_pin_cap;         //
  pybind11::list flat_lib_pin_rcap;        //
  pybind11::list flat_lib_pin_fcap;        //
  pybind11::list flat_lib_pin_cap_limit;   //
  pybind11::list flat_lib_pin_slew_limit;  //

  
  pybind11::list flat_libarc_info;                 // raw pybind rows, at least [src_pin_name, dst_pin_name, libcell_idx, libarc_offset]
  pybind11::list flat_libcell_info;                // raw pybind rows, [libcell_name, libcell_main_id, libcell_size, libcell_vt]
  pybind11::list flat_libcell_width;               // [num_lib_cells, ] physical libcell width aligned with cell_id
  pybind11::list flat_libcell_height;              // [num_lib_cells, ] physical libcell height aligned with cell_id
  pybind11::list flat_libcell_leakage;             // [num_lib_cells, ] leakage aligned with flat_libcell_info / cell_id
  pybind11::list flat_libcell_main_id2size_vt_limit;  // raw pybind rows, [libcell_main_type, size_limit, vt_limit]
  pybind11::list main_id_is_sizeable;              // [num_main_type, ] family-level sizing eligibility flag
  /* ------------------------end info for gate sizing----------------------*/

  /**/
  /* luts table*/
  pybind11::list f_delay_flat_luts_values;       // Forward delay flat LUT values
  pybind11::list f_delay_flat_luts_trans_table;  // Forward delay flat LUT transition table
  pybind11::list f_delay_flat_luts_cap_table;    // Forward delay flat LUT capacitance table
  pybind11::list f_delay_flat_luts_dim;          // Forward delay flat LUT dimensions

  pybind11::list r_delay_flat_luts_values;       // Reverse delay flat LUT values
  pybind11::list r_delay_flat_luts_trans_table;  // Reverse delay flat LUT transition table
  pybind11::list r_delay_flat_luts_cap_table;    // Reverse delay flat LUT capacitance table
  pybind11::list r_delay_flat_luts_dim;          // Reverse delay flat LUT dimensions

  pybind11::list f_trans_flat_luts_values;       // Forward transition flat LUT values
  pybind11::list f_trans_flat_luts_trans_table;  // Forward transition flat LUT transition table
  pybind11::list f_trans_flat_luts_cap_table;    // Forward transition flat LUT capacitance table
  pybind11::list f_trans_flat_luts_dim;          // Forward transition flat LUT dimensions

  pybind11::list r_trans_flat_luts_values;       // Reverse transition flat LUT values
  pybind11::list r_trans_flat_luts_trans_table;  // Reverse transition flat LUT transition table
  pybind11::list r_trans_flat_luts_cap_table;    // Reverse transition flat LUT capacitance table
  pybind11::list r_trans_flat_luts_dim;          // Reverse transition flat LUT dimensions

  // 把rise setup 存到 r_delay
  // 把clk trans 存到 trans
  // 把data trans 存到 cap

  /*-------------RC------------*/
  double c_unit;
  double r_unit;

  int xl;
  int yl;
  int xh;
  int yh;

  int origin_xl;
  int origin_yl;
  int origin_xh;
  int origin_yh;

  int origin_row_height;

  int row_height;
  int site_width;
  double total_space_area;  ///< total placeable space area excluding fixed cells.
                            ///< This is not the exact area, because we cannot exclude the overlapping fixed cells
                            ///< within a bin.

  int num_movable_pins;

  /// for contest
  double _bin_scale;
  bool _is_same_tech;

  PyPlaceDB() {}

  PyPlaceDB(idm::DataManager* db, bool with_sta, pybind11::list vt_config = pybind11::list(),
            std::string process_node = "")
  {
    set(db, with_sta, vt_config, std::move(process_node));
  }

  void set(idm::DataManager* db, bool with_sta, pybind11::list vt_config = pybind11::list(), std::string process_node = "");
  void set_sta();
  void init_routability(idm::DataManager* db, std::vector<IdbInstance*> inst_resort_list);
  void init_timing(idm::DataManager* db, std::unordered_map<std::string, int>& mClkPin2ID,
                   std::map<std::string, index_type>& mNodeName2ID, std::vector<IdbInstance*>& inst_resort_list, int ext_blockage_num);

  void set_debug_dump_lut(bool enable) { _debug_dump_lut = enable; }

 private:
  bool _debug_dump_lut = true;
  // 统一的LUT表格初始化函数
  void init_lut_table_unified(pybind11::list& flat_luts_values, pybind11::list& flat_luts_axis1_table,
                              pybind11::list& flat_luts_axis2_table, pybind11::list& flat_luts_dim, ista::LibTable* table,
                              ista::LibCell* lib_cell, bool is_constraint_table = false, std::string pin_name = "",
                              std::string related_pin_name = "", std::string table_type = "", std::string main_type = "",
                              double cell_size = 0, std::string vt = "");

  // VT configuration
  std::vector<std::pair<std::string, std::string>> _vt_config;
  std::string _process_node;
  void set_vt_config(pybind11::list config);
  double get_size_width(const std::string& cell_name);
  std::string get_vt_type(const std::string& cell_name);

  std::string getLibPinName(std::string libpin_name)
  {
    auto bracket_pos = libpin_name.find('[');
    if (bracket_pos != string::npos) {
      libpin_name = libpin_name.substr(0, bracket_pos);
    }
    return libpin_name;
  }
  std::map<std::string, index_type> mNode2PyNodeID;
  std::unordered_map<std::string, int> mPinName2PyPinID;
  std::vector<std::vector<int>> graph_local;
  std::vector<std::vector<int>> reverse_graph_local;
  int _num_vt_types = 1;

  ///<  python 中只存储 int 类型的main_id，代表一类功能都相同的cell master
  std::map<std::string, int> main_type2main_id;
  
  //< 
  std::unordered_map<std::string, int> cell_type2cell_id;  // cell_id = libcell_start[main_id] + width

  std::map<std::string, std::unordered_map<std::string, std::pair<std::string, double>>> main_type_2_vt_and_size;
};

}  // namespace python_interface

#endif
