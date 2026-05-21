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
#include "py_ircx.h"

#include <filesystem>
#include <stdexcept>

#include "RCXAPI.hh"

namespace python_interface {

namespace {

namespace fs = std::filesystem;

void require_non_empty(const std::string& value, const char* field_name)
{
  if (value.empty()) {
    throw std::invalid_argument(std::string(field_name) + " is empty.");
  }
}

void require_file_exists(const std::string& path, const char* file_kind)
{
  require_non_empty(path, file_kind);
  if (!fs::exists(path)) {
    throw std::runtime_error(std::string(file_kind) + " not found: " + path);
  }
}

}  // namespace

bool init_rcx(unsigned thread_number, double temperature)
{
  RCXAPIInst.init(thread_number == 0 ? 1U : thread_number, temperature);
  return true;
}

bool read_rcx_corner(const std::string& corner_name,
                     const std::string& itf_file,
                     const std::string& captab_file)
{
  require_non_empty(corner_name, "corner_name");
  require_file_exists(itf_file, "itf_file");
  require_file_exists(captab_file, "captab_file");

  return RCXAPIInst.readCorner(corner_name, itf_file.c_str(), captab_file.c_str());
}

bool read_rcx_mapping(const std::string& mapping_file)
{
  require_file_exists(mapping_file, "mapping_file");
  return RCXAPIInst.readMapping(mapping_file.c_str());
}

bool adapt_rcx_db()
{
  return RCXAPIInst.adaptDB();
}

bool build_rcx_topology()
{
  return RCXAPIInst.buildTopology();
}

bool build_rcx_environment()
{
  return RCXAPIInst.buildEnvironment();
}

bool build_rcx_process_variation()
{
  return RCXAPIInst.buildProcessVariation();
}

bool extract_rcx_parasitics()
{
  return RCXAPIInst.extractParasitics();
}

bool run_rcx(const std::string& config)
{
  require_file_exists(config, "config");
  return RCXAPIInst.runFromConfig(config);
}

bool report_rcx(const std::string& output_dir)
{
  return RCXAPIInst.reportSpef(output_dir);
}

}  // namespace python_interface
