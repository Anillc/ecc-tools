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
#pragma once

#include "Config.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "RAModel.hpp"

namespace irt {

#define RTRA (irt::ResourceAllocator::getInst())

class ResourceAllocator
{
 public:
  static void initInst();
  static ResourceAllocator& getInst();
  static void destroyInst();
  // function
  void allocate();

 private:
  // self
  static ResourceAllocator* _ra_instance;

  ResourceAllocator() = default;
  ResourceAllocator(const ResourceAllocator& other) = delete;
  ResourceAllocator(ResourceAllocator&& other) = delete;
  ~ResourceAllocator() = default;
  ResourceAllocator& operator=(const ResourceAllocator& other) = delete;
  ResourceAllocator& operator=(ResourceAllocator&& other) = delete;
  // function
  RAModel initRAModel();
  std::vector<RANet> convertToRANetList(std::vector<Net>& net_list);
  RANet convertToRANet(Net& net);
  void setRAComParam(RAModel& ra_model);
  RAModel initRAModel(std::vector<RANet>& ra_net_list);
  void buildRAModel(RAModel& ra_model);
  void initRANetDemand(RAModel& ra_model);
  void initRAGCellList(RAModel& ra_model);
  void buildRelation(RAModel& ra_model);
  void initTempObject(RAModel& ra_model);
  void checkRAModel(RAModel& ra_model);
  void allocateRAModel(RAModel& ra_model);
  void calcNablaF(RAModel& ra_model, double penalty_para);
  double calcAlpha(RAModel& ra_model, double penalty_para);
  double updateResult(RAModel& ra_model);
  void updateRAModel(RAModel& ra_model);
  void updateAllocationMap(RAModel& ra_model);
  GridMap<double> getCostMap(GridMap<double>& allocation_map, double lower_cost);
  void normalizeCostMap(GridMap<double>& cost_map, double lower_cost);
  void updateOriginRACostMap(RAModel& ra_model);
};

}  // namespace irt
