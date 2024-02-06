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
/**
 * @File Name: dm_save.cpp
 * @Brief :
 * @Author : Yell (12112088@qq.com)
 * @Version : 1.0
 * @Creat Date : 2022-04-15
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "idm.h"
#include <iostream>
#include <fstream>

namespace idm {

bool DataManager::save(std::string name, std::string def_path)
{
  std::string full_path = def_path + "/" + name + ".def";
  std::cout << full_path << std::endl;

  if (_idb_builder == nullptr) {
    return false;
  }

  if (def_path.empty()) {
    def_path = _config.get_output_path();
  }

  if (def_path.empty()) {
    return false;
  }

  full_path = def_path + "/" + name + ".def";

  if (!saveDef(full_path)) {
    return false;
  }

  return true;
}

bool DataManager::saveDef(string def_path)
{
  if (_idb_builder == nullptr || _idb_lef_service == nullptr || _layout == nullptr) {
    return false;
  }
  return _idb_builder->saveDef(def_path);
}

bool DataManager::saveMacroTCL(string tcl_path)
{
  std::ofstream out;
  out.open(tcl_path);
  if (_idb_builder == nullptr || _idb_lef_service == nullptr || _layout == nullptr) {
    return false;
  }
  std::string status = "fixed";
  auto dbu = _layout->get_units()->get_micron_dbu();
  for (auto& idb_inst : _design->get_instance_list()->get_instance_list()) {
    if (idb_inst->get_cell_master()->is_block()) {
      out << "placeInstance " << idb_inst->get_name() << " " << idb_inst->get_coordinate()->get_x() / dbu << " "
          << idb_inst->get_coordinate()->get_y() / dbu << " " 
          <<IdbEnum::GetInstance()->get_site_property()->get_orient_name(idb_inst->get_orient())<< std::endl;
      out << "setInstancePlacementStatus -status" << status <<" -name "<< idb_inst->get_name()<< std::endl;
    }
  }
  return true;
}

void DataManager::saveVerilog(string verilog_path, std::set<std::string>&& exclude_cell_names /*={}*/)
{
  if (_idb_builder == nullptr || _idb_lef_service == nullptr || _layout == nullptr) {
    std::cout << "idb_builder error.\n";
  }
  return _idb_builder->saveVerilog(verilog_path, exclude_cell_names);
}

bool DataManager::saveGDSII(string path)
{
  if (_idb_builder == nullptr || _idb_lef_service == nullptr || _layout == nullptr) {
    return false;
  }
  return _idb_builder->saveGDSII(path);
}
bool DataManager::saveJSON(string path, string options)
{
  if (_idb_builder == nullptr || _idb_lef_service == nullptr || _layout == nullptr) {
    return false;
  }
  return _idb_builder->saveJSON(path, options);
}

}  // namespace idm
