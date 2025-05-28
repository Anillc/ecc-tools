/**
 * @file   PyPlaceDB.cpp
 * @author Yibo Lin
 * @date   Apr 2020
 * @brief  Placement database for python
 */

#include "PyPlaceDB.h"
// #include "ContestDriver.h"
#include <algorithm>
#include <boost/polygon/polygon.hpp>
#include <cassert>
#include <cstdio>
#include <vector>

namespace python_interface {

#if 1
typedef int coordinate_type;
typedef int index_type;

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

void PyPlaceDB::set(idm::DataManager* db)
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
  // TODO:
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

  // TODO:
  // construct node2pin_map and flat_node2pin_map
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
  num_movable_pins = 0;
  // TODO:

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
    // TODO:
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

  if (true) {
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

    for (unsigned int i = 0; i < inst_num; ++i) {
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

  printf("PyPlaceDB::set end!!!\n");
#endif
}
#endif
}  // namespace python_interface
   // namespace python_interface