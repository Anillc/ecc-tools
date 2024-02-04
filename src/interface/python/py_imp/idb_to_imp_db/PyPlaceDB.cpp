/**
 * @file   PyPlaceDB.cpp
 * @author Yibo Lin
 * @date   Apr 2020
 * @brief  Placement database for python
 */

#include "PyPlaceDB.h"
// #include "ContestDriver.h"
#include <boost/polygon/polygon.hpp>
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
};

void PyPlaceDB::set(idm::DataManager* db)
{
  printf("PyPlaceDB::set start!!! Db address is %p\n", db);
  printf("PyPlaceDB::set start!!! idb_design address is %p\n", db->get_idb_design());

  using namespace idb;
  namespace gtl = boost::polygon;
  using namespace gtl::operators;
  typedef gtl::polygon_90_set_data<coordinate_type> PolygonSet;
  IdbDesign* db_deisgn = db->get_idb_design();
  // num_terminal_NIs = db_deisgn->get_io_pin_list()->get_pin_num();  // IO pins
  num_terminal_NIs = 0;  // IO pins

  double total_fixed_node_area = 0;  // compute total area of fixed cells, which is an upper bound
  // collect boxes for fixed cells and put in a polygon set to remove overlap later
  std::vector<gtl::rectangle_data<coordinate_type>> fixed_boxes;
  // record original node to new node mapping
  int inst_num = db_deisgn->get_instance_list()->get_num();
  std::map<std::string, std::vector<index_type>> mNode2NewNodes;
  std::map<std::string, index_type> mNodeName2ID;
  int node_id = 0;
  for (IdbInstance* node : db_deisgn->get_instance_list()->get_instance_list()) {
    mNodeName2ID[node->get_name()] = node_id++;
  }
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
  }
  // add a node to a bin
  auto addNode2Bin = [&](Box const& box) { fixed_boxes.emplace_back(box.xl, box.yl, box.xh, box.yh); };
  // general add a node
  auto addNode = [&](IdbInstance* node, std::string const& name, Box const& box, bool dist2map) {
    // this id may be different from node id
    int id = node_names.size();
    node_name2id_map[pybind11::str(name)] = id;
    node_names.append(pybind11::str(name));
    dmInst->get_idb_design()->m_instID2Name.push_back(name);
    node_x.append(box.xl);
    node_y.append(box.yl);
    // printf("PyPlaceDB::set start!!! Db address is %lld\n", __LINE__);
    node_orient.append(pybind11::str(IdbOrientToString(node->get_orient())));
    node_size_x.append(box.width());
    node_size_y.append(box.height());
    // map new node to original index
    node2orig_node_map.append(mNodeName2ID[node->get_name()]);
    // record original node to new node mapping
    if (mNode2NewNodes.count(node->get_name()) == 0) {
      mNode2NewNodes[node->get_name()] = std::vector<index_type>();
    }

    mNode2NewNodes[node->get_name()].push_back(id);
    if (dist2map) {
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
      box.set(box.xl() + node.xl(), box.yl() + node.yl(), box.xh() + node.xl(), box.yh() + node.yl());
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
    IdbInstance* node = db_deisgn->get_instance_list()->get_instance_list().at(i);
    // Macro const& macro = db.macro(db.macroId(node));
    if (node->get_status() != IdbPlacementStatus::kFixed) {  // || i >= db.nodes().size() - num_terminal_NIs
      Box box_tmp(node->get_coordinate()->get_x(), node->get_coordinate()->get_y(),
                  node->get_coordinate()->get_x() + node->get_cell_master()->get_width(),
                  node->get_coordinate()->get_y() + node->get_cell_master()->get_height());
      addNode(node, node->get_name(), box_tmp, false);
    }
    // else if (macro.className() != "DREAMPlace.PlaceBlockage") // fixed cells are special cases, skip placement blockages (looks like
    // ISPD2015 benchmarks do not process placement blockages)
    else  // Jiaqi: To compare with NTUPlace4dr, we have to consider blockages in ISPD2015 benchmarks
    {
      // Macro const& macro = db.macro(db.macroId(node));
      printf("PyPlaceDB detect fixed cell\n");
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
            ps.insert(gtl::rectangle_data<coordinate_type>(box.xl(), box.yl(), box.xh(), box.yh()));
          }
        } else {
          for (auto it = macro.obs().begin(), ite = macro.obs().end(); it != ite; ++it) {
            for (auto const& box : it->second) {
              ps.insert(gtl::rectangle_data<coordinate_type>(box.xl(), box.yl(), box.xh(), box.yh()));
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
        Box box_tmp(node->get_coordinate()->get_x(), node->get_coordinate()->get_y(),
                    node->get_coordinate()->get_x() + node->get_cell_master()->get_width(),
                    node->get_coordinate()->get_y() + node->get_cell_master()->get_height());
        addNode(node, node->get_name(), box_tmp, true);
        // addNode(node, db.nodeName(node), Orient(node.orient()), node, true);
        num_terminals += 1;
        // compute upper bound of total fixed cell area
        total_fixed_node_area += 1LL * node->get_cell_master()->get_height() * node->get_cell_master()->get_width();
      }
    }
  }
  // we only know num_nodes when all fixed cells with shapes are expanded
  // dreamplacePrint(kDEBUG, "num_terminals %d, numFixed %u, numPlaceBlockages %u, num_terminal_NIs %d\n", num_terminals, db.numFixed(),
  //                 db.numPlaceBlockages(), num_terminal_NIs);
  num_nodes = inst_num;  // db.nodes().size() + num_terminals - db.numFixed() - db.numPlaceBlockages()
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
  for (int i = 0; i < mNode2NewNodes.size(); ++i) {
    auto node_name = node_names[i].cast<std::string>();
    IdbInstance* node = db_deisgn->get_instance_list()->find_instance(node_name);
    pybind11::list pins;
    for (IdbPin* pin : node->get_pin_list()->get_pin_list()) {
      string inst_name = pin->get_instance()->get_name();
      int pin_id = mPin2ID[inst_name + pin->get_pin_name()];
      pins.append(pin_id);
      flat_node2pin_map.append(pin_id);
    }
    node2pin_map.append(pins);
    flat_node2pin_start_map.append(count);
    count += node->get_pin_list()->get_pin_num();
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
    // FIXME:
    //  for (IdbPin* pin : net->get_io_pin()) {
    //    /* code */
    //  }
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
    net2pin_map.append(pins);

    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      string inst_name = pin->get_instance()->get_name();
      flat_net2pin_map.append(mPin2ID[inst_name + pin->get_pin_name()]);
    }
    flat_net2pin_start_map.append(count);
    count += net->get_instance_pin_list()->get_pin_num();
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

  // origin_xl = db.get_origin_xl();
  // origin_yl = db.get_origin_yl();
  // origin_xh = db.get_origin_xh();
  // origin_yh = db.get_origin_yh();
  // origin_row_height = db.get_origin_row_height();

  row_height = db->get_idb_layout()->get_rows()->get_row_height();
  site_width = db->get_idb_layout()->get_rows()->get_row_list().at(0)->get_site()->get_width();
  printf("PyPlaceDB::set end!!!\n");
#if 0
  // routebilty driven placement
  // routing information initialized
  num_routing_grids_x = 0;
  num_routing_grids_y = 0;
  routing_grid_xl = xl;
  routing_grid_yl = yl;
  routing_grid_xh = xh;
  routing_grid_yh = yh;
  if (!db.routingCapacity(PlanarDirectEnum::HORIZONTAL).empty()) {
    num_routing_grids_x = db.numRoutingGrids(kX);
    num_routing_grids_y = db.numRoutingGrids(kY);
    routing_grid_xl = db.routingGridOrigin(kX);
    routing_grid_yl = db.routingGridOrigin(kY);
    routing_grid_xh = routing_grid_xl + num_routing_grids_x * db.routingTileSize(kX);
    routing_grid_yh = routing_grid_yl + num_routing_grids_y * db.routingTileSize(kY);
    for (index_type layer = 0; layer < db.numRoutingLayers(); ++layer) {
      unit_horizontal_capacities.append((double) db.numRoutingTracks(PlanarDirectEnum::HORIZONTAL, layer) / db.routingTileSize(kY));
      unit_vertical_capacities.append((double) db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer) / db.routingTileSize(kX));
    }
    // this is slightly different from db.routingGridOrigin
    // to be consistent with global placement
    double routing_grid_size_x = db.routingTileSize(kX);
    double routing_grid_size_y = db.routingTileSize(kY);
    double routing_grid_area = routing_grid_size_x * routing_grid_size_y;
    std::vector<int> initial_horizontal_routing_map(db.numRoutingLayers() * num_routing_grids_x * num_routing_grids_y, 0);
    std::vector<int> initial_vertical_routing_map(initial_horizontal_routing_map.size(), 0);
    for (FixedNodeConstIterator it = db.fixedNodeBegin(); it.inRange(); ++it) {
      Node const& node = *it;
      Macro const& macro = db.macro(db.macroId(node));

      for (MacroObs::ObsConstIterator ito = macro.obs().begin(); ito != macro.obs().end(); ++ito) {
        if (ito->first != "Bookshelf.Shape")  // skip dummy layer for BOOKSHELF
        {
          std::string const& layerName = ito->first;
          index_type layer = db.getLayer(layerName);
          for (auto const& obs_box : ito->second) {
            // convert to absolute box
            MacroObs::box_type box(node.xl() + obs_box.xl(), node.yl() + obs_box.yl(), node.xl() + obs_box.xh(), node.yl() + obs_box.yh());
            index_type grid_index_xl = std::max(int((box.xl() - db.routingGridOrigin(kX)) / routing_grid_size_x), 0);
            index_type grid_index_yl = std::max(int((box.yl() - db.routingGridOrigin(kY)) / routing_grid_size_y), 0);
            index_type grid_index_xh
                = std::min(unsigned((box.xh() - db.routingGridOrigin(kX)) / routing_grid_size_x) + 1, num_routing_grids_x);
            index_type grid_index_yh
                = std::min(unsigned((box.yh() - db.routingGridOrigin(kY)) / routing_grid_size_y) + 1, num_routing_grids_y);
            for (index_type k = grid_index_xl; k < grid_index_xh; ++k) {
              coordinate_type grid_xl = db.routingGridOrigin(kX) + k * routing_grid_size_x;
              coordinate_type grid_xh = grid_xl + routing_grid_size_x;
              for (index_type h = grid_index_yl; h < grid_index_yh; ++h) {
                coordinate_type grid_yl = db.routingGridOrigin(kY) + h * routing_grid_size_y;
                coordinate_type grid_yh = grid_yl + routing_grid_size_y;
                MacroObs::box_type grid_box(grid_xl, grid_yl, grid_xh, grid_yh);
                index_type index = layer * num_routing_grids_x * num_routing_grids_y + (k * num_routing_grids_y + h);
                double intersect_ratio = intersectArea(box, grid_box) / routing_grid_area;
                // dreamplaceAssert(intersect_ratio <= 1);
                initial_horizontal_routing_map[index] += ceil(intersect_ratio * db.numRoutingTracks(PlanarDirectEnum::HORIZONTAL, layer));
                initial_vertical_routing_map[index] += ceil(intersect_ratio * db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer));
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
      }
    }
    // clamp maximum for overlapping fixed cells
    for (index_type layer = 0; layer < db.numRoutingLayers(); ++layer) {
      for (int i = 0, ie = num_routing_grids_x * num_routing_grids_y; i < ie; ++i) {
        auto& hvalue = initial_horizontal_routing_map[layer * ie + i];
        hvalue = std::min(hvalue, (int) db.numRoutingTracks(PlanarDirectEnum::HORIZONTAL, layer));

        auto& vvalue = initial_vertical_routing_map[layer * ie + i];
        vvalue = std::min(vvalue, (int) db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer));
      }
    }
    for (auto item : initial_horizontal_routing_map) {
      initial_horizontal_demand_map.append(item);
    }
    for (auto item : initial_vertical_routing_map) {
      initial_vertical_demand_map.append(item);
    }
  }
#endif
}
#endif
}  // namespace python_interface