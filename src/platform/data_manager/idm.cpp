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
 * @File Name: data_manager.cpp
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

#include <cassert>
#include <cstdlib>
#include <sstream>

#include "api/TimingEngine.hh"

namespace idm {

DataManager* DataManager::_instance = nullptr;

bool DataManager::initConfig(string config_path)
{
  return _config.initConfig(config_path);
}

bool DataManager::init(string config_path)
{
  if (_idb_builder == nullptr) {
    _idb_builder = new IdbBuilder();
  }

  if (!initConfig(config_path)) {
    return false;
  }

  if (!initLef(_config.get_lef_paths())) {
    return false;
  }

  if (!initDef(_config.get_def_path())) {
    return false;
  }

  return true;
}

bool DataManager::readLef(string config_path)
{
  if (_idb_builder == nullptr) {
    _idb_builder = new IdbBuilder();
  }

  if (!initConfig(config_path)) {
    return false;
  }

  // tech lef
  if (!initLef(std::vector<std::string>{_config.get_tech_lef_path()}, true)) {
    return false;
  }
  // lef
  if (!initLef(_config.get_lef_paths())) {
    return false;
  }

  return true;
}

bool DataManager::readLef(vector<string> lef_paths, bool b_techlef)
{
  if (_idb_builder == nullptr) {
    _idb_builder = new IdbBuilder();
  }

  if (!initLef(lef_paths, b_techlef)) {
    return false;
  }

  return true;
}

void DataManager::write_placement_back(float* x, float* y, int len)
{
  bool flag = false;
  // std::vector<ContestParser::Instance*> inst_list;
  int i = 0;
  printf("write_placement_back start!!! Db address is %p\n", this);
  printf("write_placement_back start!!! idb_design address is %p\n", this->get_idb_design());
  std::set<std::pair<int, IdbRow*>> row_set;
  for (auto row : this->get_idb_layout()->get_rows()->get_row_list()) {
    int32_t row_y = row->get_original_coordinate()->get_y();
    row_set.insert(std::make_pair(row_y, row));
  }
  for (auto name : this->get_idb_design()->m_instID2Name) {
    // std::string name;
    if (i >= len) {
      break;
    }

    auto inst = this->get_idb_design()->get_instance_list()->find_instance(name);
    // int node_id = m_mNodeName2Index.find(name)->second;
    float xx = x[i];
    float yy = y[i];
    inst->set_coodinate(xx, yy);
    if (!inst->get_cell_master()->is_block()) {
      auto iter = row_set.lower_bound(std::make_pair(yy, nullptr));
      assert(iter != row_set.end());
      inst->set_orient(iter->second->get_orient());
    }
    inst->set_status_placed();
    i++;
    // flag = true;
  }
  // output hpwl
  std::cout << "WriteBack placement finished!!" << std::endl;
  // std::cout << "WriteBack double finished, Current Contest DB Total HPWL : " << contest_db->obtainTotalHPWL() <<
  // std::endl;

  return;
}

bool DataManager::write_sizing_back(const std::vector<int32_t>& inst_cell_ids, const std::vector<std::string>& cell_master_names)
{
  auto* idb_design = this->get_idb_design();
  auto* idb_layout = this->get_idb_layout();
  if (idb_design == nullptr || idb_layout == nullptr) {
    return false;
  }
  auto* instance_list = idb_design->get_instance_list();
  auto* master_list = idb_layout->get_cell_master_list();
  if (instance_list == nullptr || master_list == nullptr) {
    return false;
  }

  const int instance_count = instance_list->get_num();
  const int limit = std::min(static_cast<int>(inst_cell_ids.size()), instance_count);
  int updated_count = 0;
  int timing_synced_count = 0;
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* timing_idb_adapter = timing_engine ? timing_engine->getIDBAdapter() : nullptr;
  const bool can_sync_timing =
      timing_idb_adapter != nullptr && timing_idb_adapter->get_idb() == _idb_builder;
  const char* debug_inst_env = std::getenv("IEDA_WRITEBACK_DEBUG_INSTS");
  const std::string debug_inst_csv = debug_inst_env ? std::string(debug_inst_env) : std::string();
  const char* debug_first_n_env = std::getenv("IEDA_WRITEBACK_DEBUG_FIRST_N");
  int debug_first_n = 0;
  if (debug_first_n_env != nullptr) {
    std::istringstream iss(debug_first_n_env);
    iss >> debug_first_n;
    if (!iss || debug_first_n < 0) {
      debug_first_n = 0;
    }
  }
  int debug_first_n_emitted = 0;
  auto should_debug_inst = [&debug_inst_csv](const std::string& inst_name) {
    if (debug_inst_csv.empty()) {
      return false;
    }
    size_t start = 0;
    while (start <= debug_inst_csv.size()) {
      size_t end = debug_inst_csv.find(',', start);
      if (end == std::string::npos) {
        end = debug_inst_csv.size();
      }
      std::string token = debug_inst_csv.substr(start, end - start);
      if (token == inst_name) {
        return true;
      }
      start = end + 1;
      if (end == debug_inst_csv.size()) {
        break;
      }
    }
    return false;
  };
  for (int i = 0; i < limit; ++i) {
    const int32_t cell_id = inst_cell_ids[i];
    if (cell_id < 0 || cell_id >= static_cast<int32_t>(cell_master_names.size())) {
      continue;
    }

    const std::string& inst_name = idb_design->m_instID2Name[i];
    auto* inst = instance_list->find_instance(inst_name);
    if (inst == nullptr) {
      continue;
    }

    auto* new_master = master_list->find_cell_master(cell_master_names[cell_id]);
    if (new_master == nullptr) {
      std::cout << "write_sizing_back warning: missing cell master " << cell_master_names[cell_id] << std::endl;
      return false;
    }
    if (inst->get_cell_master() == new_master) {
      continue;
    }
    const bool debug_by_name = should_debug_inst(inst_name);
    const bool debug_by_rank = debug_first_n > 0 && debug_first_n_emitted < debug_first_n;
    const bool debug_this_inst = debug_by_name || debug_by_rank;
    if (debug_this_inst) {
      std::cout << "WriteBack debug inst=" << inst_name << " old_idb_master=" << inst->get_cell_master()->get_name()
                << " target_idb_master=" << new_master->get_name();
      if (can_sync_timing) {
        auto* pre_sta_inst = timing_idb_adapter->dbToSta(inst);
        if (pre_sta_inst != nullptr && pre_sta_inst->get_inst_cell() != nullptr) {
          std::cout << " pre_sta_master=" << pre_sta_inst->get_inst_cell()->get_cell_name();
        } else {
          std::cout << " pre_sta_master=<null>";
        }
      } else {
        std::cout << " pre_sta_master=<timing_sync_disabled>";
      }
      std::cout << std::endl;
      if (debug_by_rank) {
        ++debug_first_n_emitted;
      }
    }

    bool pin_signature_ok = true;
    for (auto* inst_pin : inst->get_pin_list()->get_pin_list()) {
      if (inst_pin == nullptr) {
        continue;
      }
      std::string term_name = inst_pin->get_pin_name();
      if (inst_pin->get_term() != nullptr) {
        term_name = inst_pin->get_term()->get_name();
      }
      if (new_master->findTerm(term_name) == nullptr && new_master->findTerm(inst_pin->get_pin_name()) == nullptr) {
        std::cout << "write_sizing_back warning: pin signature mismatch for inst " << inst_name << " old_master "
                  << inst->get_cell_master()->get_name() << " new_master " << new_master->get_name() << " missing pin "
                  << term_name << std::endl;
        pin_signature_ok = false;
        break;
      }
    }
    if (!pin_signature_ok) {
      return false;
    }

    bool timing_synced = false;
    if (can_sync_timing) {
      auto* sta_inst = timing_idb_adapter->dbToSta(inst);
      auto* sta_cell = timing_idb_adapter->dbToSta(new_master);
      if (sta_inst != nullptr && sta_cell != nullptr) {
        timing_idb_adapter->substituteCell(sta_inst, sta_cell);
        if (debug_this_inst) {
          std::cout << "WriteBack debug inst=" << inst_name << " post_substitute_idb_master="
                    << inst->get_cell_master()->get_name();
          if (sta_inst->get_inst_cell() != nullptr) {
            std::cout << " post_sta_master=" << sta_inst->get_inst_cell()->get_cell_name();
          } else {
            std::cout << " post_sta_master=<null>";
          }
          std::cout << std::endl;
        }
        ++timing_synced_count;
        timing_synced = true;
      }
    }
    if (!timing_synced) {
      inst->swap_cell_master(new_master);
      if (debug_this_inst) {
        std::cout << "WriteBack debug inst=" << inst_name << " fallback_swap_idb_master="
                  << inst->get_cell_master()->get_name() << std::endl;
      }
    }
    ++updated_count;
  }

  std::cout << "WriteBack sizing finished!! updated_instances=" << updated_count
            << " timing_synced_instances=" << timing_synced_count << std::endl;
  return true;
}

bool DataManager::readDef(string path)
{
  if (_idb_builder == nullptr || _idb_lef_service == nullptr || _layout == nullptr) {
    return false;
  }

  if (!initDef(path)) {
    return false;
  }

  return true;
}

bool DataManager::readVerilog(string path, string top_module)
{
  if (_idb_builder == nullptr || _idb_lef_service == nullptr || _layout == nullptr) {
    return false;
  }

  if (!initVerilog(path, top_module)) {
    return false;
  }

  return true;
}

int DataManager::get_routing_layer_1st()
{
  string routing_layer_1st = _config.get_routing_layer_1st();
  auto layout = get_idb_layout();
  auto layer_list = layout->get_layers();

  auto layer_find = layer_list->find_layer(routing_layer_1st);

  return layer_find != nullptr ? layer_find->get_id() : 0;
}

}  // namespace idm
