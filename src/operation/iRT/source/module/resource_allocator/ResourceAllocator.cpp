// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "ResourceAllocator.hpp"

#include "Logger.hpp"
#include "Monitor.hpp"
#include "RTInterface.hpp"

namespace irt {

// public

void ResourceAllocator::initInst()
{
  if (_ra_instance == nullptr) {
    _ra_instance = new ResourceAllocator();
  }
}

ResourceAllocator& ResourceAllocator::getInst()
{
  if (_ra_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ra_instance;
}

void ResourceAllocator::destroyInst()
{
  if (_ra_instance != nullptr) {
    delete _ra_instance;
    _ra_instance = nullptr;
  }
}

// function

void ResourceAllocator::allocate()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  RAModel ra_model = initRAModel();
  setRAComParam(ra_model);
  buildRAModel(ra_model);
  checkRAModel(ra_model);
  allocateRAModel(ra_model);
  updateRAModel(ra_model);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

ResourceAllocator* ResourceAllocator::_ra_instance = nullptr;

RAModel ResourceAllocator::initRAModel()
{
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();

  RAModel ra_model;
  ra_model.set_ra_net_list(convertToRANetList(net_list));
  return ra_model;
}

std::vector<RANet> ResourceAllocator::convertToRANetList(std::vector<Net>& net_list)
{
  std::vector<RANet> ra_net_list;
  ra_net_list.reserve(net_list.size());
  for (Net& net : net_list) {
    ra_net_list.emplace_back(convertToRANet(net));
  }
  return ra_net_list;
}

RANet ResourceAllocator::convertToRANet(Net& net)
{
  RANet ra_net;
  ra_net.set_origin_net(&net);
  ra_net.set_net_idx(net.get_net_idx());
  ra_net.set_connect_type(net.get_connect_type());
  for (Pin& pin : net.get_pin_list()) {
    ra_net.get_ra_pin_list().push_back(RAPin(pin));
  }
  ra_net.set_bounding_box(net.get_bounding_box());
  return ra_net;
}

void ResourceAllocator::setRAComParam(RAModel& ra_model)
{
  double supply_unit = 0.25;
  double initial_penalty = 100;    //!< 罚函数的参数
  double penalty_drop_rate = 0.8;  //!< 罚函数的参数下降系数
  int32_t outer_iter_num = 10;     //!< 外层循环数
  int32_t inner_iter_num = 50;     //!< 内层循环数
  /**
   * supply_unit, initial_penalty, penalty_drop_rate, outer_iter_num, inner_iter_num
   */
  RAComParam ra_com_param(supply_unit, initial_penalty, penalty_drop_rate, outer_iter_num, inner_iter_num);
  RTLOG.info(Loc::current(), "supply_unit: ", ra_com_param.get_supply_unit());
  RTLOG.info(Loc::current(), "initial_penalty: ", ra_com_param.get_initial_penalty());
  RTLOG.info(Loc::current(), "penalty_drop_rate: ", ra_com_param.get_penalty_drop_rate());
  RTLOG.info(Loc::current(), "outer_iter_num: ", ra_com_param.get_outer_iter_num());
  RTLOG.info(Loc::current(), "inner_iter_num: ", ra_com_param.get_inner_iter_num());
  ra_model.set_ra_com_param(ra_com_param);
}

RAModel ResourceAllocator::initRAModel(std::vector<RANet>& ra_net_list)
{
  RAModel ra_model;
  ra_model.set_ra_net_list(ra_net_list);
  return ra_model;
}

void ResourceAllocator::buildRAModel(RAModel& ra_model)
{
  initRANetDemand(ra_model);
  initRAGCellList(ra_model);
  buildRelation(ra_model);
  initTempObject(ra_model);
}

void ResourceAllocator::initRANetDemand(RAModel& ra_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

#pragma omp parallel for
  for (RANet& ra_net : ra_model.get_ra_net_list()) {
    std::vector<PlanarCoord> planar_coord_list;
    for (RAPin& ra_pin : ra_net.get_ra_pin_list()) {
      planar_coord_list.push_back(ra_pin.get_access_point().get_grid_coord());
    }
    std::sort(planar_coord_list.begin(), planar_coord_list.end(), CmpPlanarCoordByXASC());
    planar_coord_list.erase(std::unique(planar_coord_list.begin(), planar_coord_list.end()), planar_coord_list.end());

    double routing_demand = 0;
    for (Segment<PlanarCoord>& planar_topo : RTI.getPlanarTopoList(planar_coord_list)) {
      routing_demand += RTUTIL.getManhattanDistance(planar_topo.get_first(), planar_topo.get_second());
    }
    ra_net.set_routing_demand(routing_demand);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void ResourceAllocator::initRAGCellList(RAModel& ra_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();
  double supply_unit = ra_model.get_ra_com_param().get_supply_unit();

  std::vector<RAGCell>& ra_gcell_list = ra_model.get_ra_gcell_list();
  ra_gcell_list.resize(gcell_map.get_x_size() * gcell_map.get_y_size());

#pragma omp parallel for collapse(2)
  for (int32_t x = 0; x < gcell_map.get_x_size(); x++) {
    for (int32_t y = 0; y < gcell_map.get_y_size(); y++) {
      double track_supply = 0;
      for (auto& [layer_idx, orient_supply_map] : gcell_map[x][y].get_routing_orient_supply_map()) {
        for (auto& [orient, supply] : orient_supply_map) {
          track_supply += std::floor(supply_unit * supply);
        }
      }
      ra_gcell_list[x * gcell_map.get_y_size() + y].set_track_supply(track_supply);
    }
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void ResourceAllocator::buildRelation(RAModel& ra_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();

  std::vector<RANet>& ra_net_list = ra_model.get_ra_net_list();
  std::vector<RAGCell>& ra_gcell_list = ra_model.get_ra_gcell_list();
  std::vector<double>& result_list = ra_model.get_result_list();

  for (int32_t i = 0; i < static_cast<int32_t>(ra_net_list.size()); i++) {
    if (ra_net_list[i].get_net_idx() != i) {
      RTLOG.error(Loc::current(), "The order is error!");
    }
  }

  int32_t result_idx = 0;
  for (int32_t ra_net_idx = 0; ra_net_idx < static_cast<int32_t>(ra_net_list.size()); ra_net_idx++) {
    BoundingBox& bounding_box = ra_net_list[ra_net_idx].get_bounding_box();
    for (int32_t x = bounding_box.get_grid_ll_x(); x <= bounding_box.get_grid_ur_x(); x++) {
      for (int32_t y = bounding_box.get_grid_ll_y(); y <= bounding_box.get_grid_ur_y(); y++) {
        int32_t ra_gcell_idx = x * gcell_map.get_y_size() + y;
        ra_net_list[ra_net_idx].get_ra_gcell_node_list().emplace_back(ra_gcell_idx, result_idx);
        ra_gcell_list[ra_gcell_idx].get_ra_net_node_list().emplace_back(ra_net_idx, result_idx);
        ++result_idx;
      }
    }
  }
  result_list.resize(result_idx + 1);
  for (size_t i = 0; i < result_list.size(); i++) {
    result_list[i] = 0.0;
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void ResourceAllocator::initTempObject(RAModel& ra_model)
{
  std::vector<RAGCell>& ra_gcell_list = ra_model.get_ra_gcell_list();
  std::vector<RANet>& ra_net_list = ra_model.get_ra_net_list();
  std::vector<double>& nabla_f_row = ra_model.get_nabla_f_row();
  std::vector<double>& nabla_f_col = ra_model.get_nabla_f_col();

  nabla_f_row.resize(ra_gcell_list.size());
  for (size_t i = 0; i < nabla_f_row.size(); i++) {
    nabla_f_row[i] = 0.0;
  }

  nabla_f_col.resize(ra_net_list.size());
  for (size_t i = 0; i < nabla_f_col.size(); i++) {
    nabla_f_col[i] = 0.0;
  }
}

void ResourceAllocator::checkRAModel(RAModel& ra_model)
{
  for (RAGCell& ra_gcell : ra_model.get_ra_gcell_list()) {
    if (ra_gcell.get_track_supply() < 0) {
      RTLOG.error(Loc::current(), "The track_supply < 0!");
    }
  }
}

/**
 * @description: 使用二次规划 罚方法
 *
 * 迭代过程
 *  f(x) = (1/2) * x' * Q * x + b' * x
 *  nabla_f = Q * x + b
 *  alpha = (nabla_f' * nabla_f) / (nabla_f' * Q * nabla_f)
 *  x = x + (-nabla_f) * alpha
 *
 * eg.
 *  RAGCell : GCM    Target : T    RANet : NM    Constraint : C
 * ────────────────────────────────────────
 * │ GCM0(T1=3) │ GCM1(T2=4) │ GCM2(T3=5) │
 * ───────────────────────────────────────────────────│
 * │     x0     │     x1     │            │ NM0(C1=3) │
 * │            │            │            │           │
 * │            │     x2     │     x3     │ NM1(C2=6) │
 * ───────────────────────────────────────────────────│
 *
 * min [(x0 - T1)^2 + (x1 + x2 - T2)^2 + (x3 - T3)^2] +
 *     (1/(2 * u)) * [(x0 + x1 - C1)^2 + (x2 + x3 - C2)^2]
 *
 */
void ResourceAllocator::allocateRAModel(RAModel& ra_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  double initial_penalty = ra_model.get_ra_com_param().get_initial_penalty();      //!< 罚函数的参数
  double penalty_drop_rate = ra_model.get_ra_com_param().get_penalty_drop_rate();  //!< 罚函数的参数下降系数
  int32_t outer_iter_num = ra_model.get_ra_com_param().get_outer_iter_num();       //!< 外层循环数
  int32_t inner_iter_num = ra_model.get_ra_com_param().get_inner_iter_num();       //!< 内层循环数

  for (int32_t i = 0, stage = 1; i < outer_iter_num; i++, stage++) {
    double penalty_para = (1 / (2 * initial_penalty));
    RTLOG.info(Loc::current(), "************* Start iteration penalty_para=", penalty_para, " *************");
    for (int32_t j = 0, iter = 1; j < inner_iter_num; j++, iter++) {
      Monitor iter_monitor;

      calcNablaF(ra_model, penalty_para);
      double norm_nabla_f = calcAlpha(ra_model, penalty_para);
      double norm_square_step = updateResult(ra_model);

      RTLOG.info(Loc::current(), "Stage(", stage, "/", outer_iter_num, ") Iter(", iter, "/", inner_iter_num, "), norm_nabla_f=", norm_nabla_f,
                 ", norm_square_step=", norm_square_step, iter_monitor.getStatsInfo());
    }
    initial_penalty *= penalty_drop_rate;
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

/**
 * calculate nabla_f;
 * 因;
 * nabla_f = Q * x + b
 * 且;
 * Q = Q_ra_gcell + (1/2u) * Q_ra_net;
 * F = F_ra_gcell + (1/2u) * F_ra_net;
 * 代入;
 * nabla_f = Q_ra_gcell'*X + (1/2u)Q_ra_net'*X + F_ra_gcell + (1/2u)F_ra_net
 * 整理;
 * nabla_f = (Q_ra_gcell*X + F_ra_gcell) + (1/2u)*(Q_ra_net*X + F_ra_net)
 */
void ResourceAllocator::calcNablaF(RAModel& ra_model, double penalty_para)
{
  std::vector<RAGCell>& ra_gcell_list = ra_model.get_ra_gcell_list();
  std::vector<RANet>& ra_net_list = ra_model.get_ra_net_list();
  std::vector<double>& result_list = ra_model.get_result_list();
  std::vector<double>& nabla_f_row = ra_model.get_nabla_f_row();
  std::vector<double>& nabla_f_col = ra_model.get_nabla_f_col();
/**
 * calculate "Q_ra_gcell*X + F_ra_gcell";
 * 因;
 * Q_ra_gcell = (Q_ra_gcell_1 + Q_ra_gcell_2 + ...);
 * F_ra_gcell = (F_ra_gcell_1 + F_ra_gcell_2 + ...);
 * 代入;
 * (Q_ra_gcell*X + F_ra_gcell) = (Q_ra_gcell_1*X_1 + Q_ra_gcell_2*X_2 + ...) + (F_ra_gcell_1 + F_ra_gcell_2 + ...);
 * 整理;
 * (Q_ra_gcell*X + F_ra_gcell) = (Q_ra_gcell_1*X_1 + F_ra_gcell_1) + (Q_ra_gcell_2*X_2 + F_ra_gcell_2) + ...;
 */
#pragma omp parallel for
  for (size_t i = 0; i < ra_gcell_list.size(); i++) {
    RAGCell& ra_gcell = ra_gcell_list[i];
    std::vector<RANetNode>& ra_net_node_list = ra_gcell.get_ra_net_node_list();
    // calculate "Q_ra_gcell_i*X_i"
    double gcell_q_temp = 0;
    for (size_t j = 0; j < ra_net_node_list.size(); j++) {
      gcell_q_temp += result_list[ra_net_node_list[j].get_result_idx()];
    }
    // 由于有 f(x) = (1/2) * x' * Q * x + b' * x 中存在 "1/2" 故要 "*2"
    gcell_q_temp *= 2;
    // calculate "+F_ra_gcell_i"
    gcell_q_temp += (-2) * ra_gcell.get_track_supply();
    // update nabla_f_row
    nabla_f_row[i] = gcell_q_temp;
  }

  /**
   * calculate "(1/2u) * (Q_ra_net*X+F_ra_net)";
   * 因;
   * Q_ra_net = (Q_ra_net_1 + Q_ra_net_2 + ...);
   * F_ra_net = (F_ra_net_1 + F_ra_net_2 + ...);
   * 代入;
   * (1/2u) * (Q_ra_net*X + F_ra_net) = (1/2u) * [(Q_ra_net_1*X_1 + Q_ra_net_2*X_2 + ...) + (F_ra_net_1 + F_ra_net_2 + ...)]
   * 整理;
   * (1/2u) * (Q_ra_net*X + F_ra_net) = (1/2u) * [(Q_ra_net_1*X_1 + F_ra_net_1) + (Q_ra_net_2*X_2 + F_ra_net_2) + ...]
   */
#pragma omp parallel for
  for (size_t i = 0; i < ra_net_list.size(); i++) {
    RANet& ra_net = ra_net_list[i];
    std::vector<RAGCellNode>& ra_gcell_node_list = ra_net.get_ra_gcell_node_list();
    // calculate "Q_ra_net_i*X_i"
    double bounding_box_q_temp = 0;
    for (size_t j = 0; j < ra_gcell_node_list.size(); j++) {
      bounding_box_q_temp += result_list[ra_gcell_node_list[j].get_result_idx()];
    }
    // 由于有 f(x) = (1/2) * x' * Q * x + b' * x 中存在 "1/2" 故要 "*2"
    bounding_box_q_temp *= 2;
    // calculate "+F_ra_net_i"
    bounding_box_q_temp += (-2) * ra_net.get_routing_demand();
    /**
     * notice:当前逻辑不改变的情况下,由于是在nabla_f上追加的值,所以不能在算完所有值后将nabla_f内的每个值相乘;
     * 会存在数据依赖,如果算完所有值再乘的结果与下例子一样;
     * 原结果由 { A+k*(B+C) } 变为 { k*(A+B+C) };
     * 现在计算情况与上例一样,例子中B交C为空集,所以不会增加多余计算,若需要高性能则可以再增加空间(空间换时间);
     */
    // calculate "(1/2u)*"
    bounding_box_q_temp *= penalty_para;
    // update nabla_f_col
    nabla_f_col[i] = bounding_box_q_temp;
  }
}
//  calculate alpha = (nabla_f' * nabla_f) / (nabla_f' * Q * nabla_f)
double ResourceAllocator::calcAlpha(RAModel& ra_model, double penalty_para)
{
  std::vector<RAGCell>& ra_gcell_list = ra_model.get_ra_gcell_list();
  std::vector<RANet>& ra_net_list = ra_model.get_ra_net_list();
  std::vector<double>& nabla_f_row = ra_model.get_nabla_f_row();
  std::vector<double>& nabla_f_col = ra_model.get_nabla_f_col();
  /**
   * calculate "||nabla_f||"
   * calculate "nabla_f' * Q * nabla_f"
   * Q = (Q_ra_gcell_1 + Q_ra_gcell_2 + ...) + (1/2u) * [Q_ra_net1 + Q_ra_net2 + ...];
   * 计算情况与下例一样,最后结果为单值
   * (1;2;3)'*[1 0 1;0 0 0;1 0 1]*(1;2;3)
   * =((1+3),0,(1+3))*(1;2;3)
   * =(1+3)*1+(1+3)*3
   * =(1+3)*(1+3)
   */
  double norm_nabla_f = 0;
  double nabla_f_q_nabla_f = 0;

#pragma omp parallel for
  for (size_t i = 0; i < ra_gcell_list.size(); i++) {
    RAGCell& ra_gcell = ra_gcell_list[i];
    std::vector<RANetNode>& ra_net_node_list = ra_gcell.get_ra_net_node_list();

    double gcell_q_temp = 0;
    double gcell_norm_nabla_f = 0;
    gcell_q_temp += (static_cast<int32_t>(ra_net_node_list.size()) * nabla_f_row[i]);
    for (size_t j = 0; j < ra_net_node_list.size(); j++) {
      RANetNode& ra_net_node = ra_net_node_list[j];
      gcell_q_temp += nabla_f_col[ra_net_node.get_ra_net_idx()];
      gcell_norm_nabla_f += std::pow(nabla_f_row[i] + nabla_f_col[ra_net_node.get_ra_net_idx()], 2);
    }
    double gcell_nabla_f_q_nabla_f = std::pow(gcell_q_temp, 2);
#pragma omp atomic
    norm_nabla_f += gcell_norm_nabla_f;
#pragma omp atomic
    nabla_f_q_nabla_f += gcell_nabla_f_q_nabla_f;
  }

#pragma omp parallel for
  for (size_t i = 0; i < ra_net_list.size(); i++) {
    RANet& ra_net = ra_net_list[i];
    std::vector<RAGCellNode>& ra_gcell_node_list = ra_net.get_ra_gcell_node_list();

    double bounding_box_q_temp = 0;
    bounding_box_q_temp += (static_cast<int32_t>(ra_gcell_node_list.size()) * nabla_f_col[i]);
    for (size_t j = 0; j < ra_gcell_node_list.size(); j++) {
      bounding_box_q_temp += nabla_f_row[ra_gcell_node_list[j].get_ra_gcell_idx()];
    }
    double bounding_nabla_f_q_nabla_f = std::pow(bounding_box_q_temp, 2) * penalty_para;
#pragma omp atomic
    nabla_f_q_nabla_f += bounding_nabla_f_q_nabla_f;
  }
  // 由于有 f(x) = (1/2) * x' * Q * x + b' * x 中存在 "1/2" 故要 "*2"
  nabla_f_q_nabla_f *= 2;
  ra_model.set_alpha(norm_nabla_f / nabla_f_q_nabla_f);
  return norm_nabla_f;
}

// x = x + (-nabla_f) * alpha
double ResourceAllocator::updateResult(RAModel& ra_model)
{
  double norm_square_step = 0;

  std::vector<RAGCell>& ra_gcell_list = ra_model.get_ra_gcell_list();
  std::vector<double>& result_list = ra_model.get_result_list();
  std::vector<double>& nabla_f_row = ra_model.get_nabla_f_row();
  std::vector<double>& nabla_f_col = ra_model.get_nabla_f_col();

  double alpha = ra_model.get_alpha();

  for (size_t ra_gcell_idx = 0; ra_gcell_idx < ra_gcell_list.size(); ra_gcell_idx++) {
    RAGCell& ra_gcell = ra_gcell_list[ra_gcell_idx];
    std::vector<RANetNode>& ra_net_node_list = ra_gcell.get_ra_net_node_list();

    for (size_t j = 0; j < ra_net_node_list.size(); j++) {
      RANetNode& ra_net_node = ra_net_node_list[j];
      double step = (-1 * (nabla_f_row[ra_gcell_idx] + nabla_f_col[ra_net_node.get_ra_net_idx()]) * alpha);
      result_list[ra_net_node.get_result_idx()] += step;
      if (result_list[ra_net_node.get_result_idx()] < 0) {
        result_list[ra_net_node.get_result_idx()] = 0;
      }
      norm_square_step += std::pow(step, 2);
    }
  }
  return norm_square_step;
}

void ResourceAllocator::updateRAModel(RAModel& ra_model)
{
  updateAllocationMap(ra_model);
  updateOriginRACostMap(ra_model);
}

void ResourceAllocator::updateAllocationMap(RAModel& ra_model)
{
  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();

  std::vector<RANet>& ra_net_list = ra_model.get_ra_net_list();
  std::vector<double>& result_list = ra_model.get_result_list();

  for (RANet& ra_net : ra_net_list) {
    BoundingBox& bounding_box = ra_net.get_bounding_box();
    int32_t grid_ll_x = bounding_box.get_grid_ll_x();
    int32_t grid_ll_y = bounding_box.get_grid_ll_y();

    GridMap<double> allocation_map(bounding_box.getXSize(), bounding_box.getYSize());
    for (RAGCellNode& ra_gcell_node : ra_net.get_ra_gcell_node_list()) {
      int32_t grid_x = ra_gcell_node.get_ra_gcell_idx() / gcell_map.get_y_size();
      int32_t grid_y = ra_gcell_node.get_ra_gcell_idx() % gcell_map.get_y_size();
      allocation_map[grid_x - grid_ll_x][grid_y - grid_ll_y] = result_list[ra_gcell_node.get_result_idx()];
    }
    double lower_cost = 0.001;
    GridMap<double> cost_map = getCostMap(allocation_map, lower_cost);
    normalizeCostMap(cost_map, lower_cost);
    for (RAPin& ra_pin : ra_net.get_ra_pin_list()) {
      PlanarCoord& grid_coord = ra_pin.get_access_point().get_grid_coord();
      cost_map[grid_coord.get_x() - grid_ll_x][grid_coord.get_y() - grid_ll_y] = lower_cost;
    }
    ra_net.set_resource_cost_map(cost_map);
  }
}

GridMap<double> ResourceAllocator::getCostMap(GridMap<double>& allocation_map, double lower_cost)
{
  GridMap<double> cost_map(allocation_map.get_x_size(), allocation_map.get_y_size());

  double min_allocation = DBL_MAX;
  for (int32_t i = 0; i < allocation_map.get_x_size(); i++) {
    for (int32_t j = 0; j < allocation_map.get_y_size(); j++) {
      allocation_map[i][j] = std::max(allocation_map[i][j], lower_cost);
      if (RTUTIL.isNanOrInf(allocation_map[i][j])) {
        RTLOG.error(Loc::current(), "The allocation is nan or inf!");
      }
      min_allocation = std::min(allocation_map[i][j], min_allocation);
    }
  }
  double sum_allocation = 0;
  double sum_mid = 0;
  for (int32_t i = 0; i < allocation_map.get_x_size(); i++) {
    for (int32_t j = 0; j < allocation_map.get_y_size(); j++) {
      sum_allocation += allocation_map[i][j];
      sum_mid += (min_allocation / allocation_map[i][j]);
    }
  }
  double sum_cost = 0;
  for (int32_t i = 0; i < allocation_map.get_x_size(); i++) {
    for (int32_t j = 0; j < allocation_map.get_y_size(); j++) {
      cost_map[i][j] = (min_allocation / allocation_map[i][j]) * (sum_allocation / sum_mid);
      sum_cost += cost_map[i][j];
    }
  }
  if (!RTUTIL.equalDoubleByError(sum_allocation, sum_cost, RT_ERROR)) {
    RTLOG.error(Loc::current(), "The total allocation '", sum_allocation, "' is not equal to the total cost '", sum_cost, "'!");
  }
  return cost_map;
}

void ResourceAllocator::normalizeCostMap(GridMap<double>& cost_map, double lower_cost)
{
  double min_cost = DBL_MAX;
  double max_cost = 0;
  for (int32_t i = 0; i < cost_map.get_x_size(); i++) {
    for (int32_t j = 0; j < cost_map.get_y_size(); j++) {
      min_cost = std::min(min_cost, cost_map[i][j]);
      max_cost = std::max(max_cost, cost_map[i][j]);
    }
  }
  double base = std::max(max_cost - min_cost, lower_cost);
  for (int32_t i = 0; i < cost_map.get_x_size(); i++) {
    for (int32_t j = 0; j < cost_map.get_y_size(); j++) {
      cost_map[i][j] = std::max((cost_map[i][j] - min_cost) / base, lower_cost);
      if (RTUTIL.isNanOrInf(cost_map[i][j])) {
        RTLOG.error(Loc::current(), "The cost is nan or inf!");
      }
      // 只取小数点后3位
      cost_map[i][j] = RTUTIL.retainPlaces(cost_map[i][j], 3);
    }
  }
}

void ResourceAllocator::updateOriginRACostMap(RAModel& ra_model)
{
  for (RANet& ra_net : ra_model.get_ra_net_list()) {
    ra_net.get_origin_net()->set_resource_cost_map(ra_net.get_resource_cost_map());
  }
}

}  // namespace irt
