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
#include "py_db.h"

#include <idm.h>

namespace python_interface {

bool initIdb(const std::string& config_path)
{
  return dmInst->init(config_path);
}

bool initTechLef(const std::string& techlef_path)
{
  dmInst->get_config().set_tech_lef_path(techlef_path);
  return dmInst->readLef(vector<string>{techlef_path}, true);
}

bool initLef(const std::vector<std::string>& lef_paths)
{
  dmInst->get_config().set_lef_paths(lef_paths);
  return dmInst->readLef(lef_paths);
}

bool initDef(const std::string& def_path)
{
  dmInst->get_config().set_def_path(def_path);
  return dmInst->readDef(def_path);
}

bool initVerilog(const std::string& verilog_path, const std::string& top_module)
{
  dmInst->get_config().set_verilog_path(verilog_path);
  return dmInst->readVerilog(verilog_path, top_module);
}

bool saveDef(const std::string& def_name)
{
  return dmInst->saveDef(def_name);
}

bool saveMacroTCL(const std::string& def_name)
{
  return dmInst->saveMacroTCL(def_name);
}

bool saveNetList(const std::string& netlist_path, std::set<std::string> exclude_cell_names /* = {} */,
                 bool is_add_space_for_escape_name /* = false*/)
{
  dmInst->saveVerilog(netlist_path, std::move(exclude_cell_names), is_add_space_for_escape_name);
  return true;
}

bool saveGDSII(const std::string& gds_name, bool is_hardened /* = false */)
{
  return dmInst->saveGDSII(gds_name, is_hardened);
}

bool saveJson(const std::string& path)
{
  std::string options = "";

  return dmInst->saveJSON(path, options);
}


bool writeAbstractLef(const std::string& output_lef_path)
{
  namespace fs = std::filesystem;

  const fs::path output_lef(output_lef_path);
  if (!output_lef.parent_path().empty()) {
    std::error_code ec;
    fs::create_directories(output_lef.parent_path(), ec);
    if (ec) {
      throw std::runtime_error("failed to create output directory: " +
                               output_lef.parent_path().string() + ", error=" +
                               ec.message());
    }
  }

  std::error_code ec;
  fs::remove(output_lef, ec);

  // saveLef currently inherits a legacy closeFile() return convention where a
  // successful fclose() yields 0/false, so use the emitted file as the
  // user-visible success criterion here.
  dmInst->saveLef(output_lef_path);

  if (!fs::exists(output_lef)) {
    return false;
  }
  return fs::file_size(output_lef) > 0;
}

}  // namespace python_interface