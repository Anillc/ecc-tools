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

#include <omp.h>

#include "RCX.hpp"

namespace python_interface {

namespace {

namespace fs = std::filesystem;

ircx::RCX& rcx()
{
  return ircx::RCX::getOrCreateInst();
}

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

bool init_rcx(unsigned thread_number)
{
  rcx().set_num_threads(thread_number == 0 ? 1U : thread_number);
  return true;
}

bool read_rcx_corner(const std::string& corner_name,
                     const std::string& itf_file,
                     const std::string& captab_file)
{
  require_non_empty(corner_name, "corner_name");
  require_file_exists(itf_file, "itf_file");
  require_file_exists(captab_file, "captab_file");

  return rcx().readCorner(corner_name, itf_file.c_str(), captab_file.c_str());
}

bool read_rcx_itf(const std::vector<std::string>& itf_files)
{
  if (itf_files.empty()) {
    throw std::invalid_argument("itf_files is empty.");
  }

  for (const auto& itf_file : itf_files) {
    require_file_exists(itf_file, "itf_file");
  }

  return rcx().readItf(itf_files);
}

bool read_rcx_mapping(const std::string& mapping_file)
{
  require_file_exists(mapping_file, "mapping_file");
  return rcx().readMapping(mapping_file.c_str());
}

bool adapt_rcx_db()
{
  return rcx().adaptDB();
}

bool build_rcx_topology()
{
  return rcx().buildTopology();
}

bool build_rcx_environment()
{
  return rcx().buildEnvironment();
}

bool build_rcx_process_variation()
{
  return rcx().buildProcessVariation();
}

bool extract_rcx_parasitics()
{
  return rcx().extractParasitics();
}

bool run_rcx()
{
  auto& rcx_inst = rcx();
  omp_set_num_threads(rcx_inst.num_threads());

  unsigned result = 1;
  result &= rcx_inst.adaptDB();
  result &= rcx_inst.buildTopology();
  result &= rcx_inst.buildEnvironment();
  result &= rcx_inst.buildProcessVariation();
  result &= rcx_inst.extractParasitics();

  return result;
}

bool report_rcx(const std::string& output_dir)
{
  const std::string resolved_output_dir = output_dir.empty() ? "." : output_dir;
  fs::create_directories(resolved_output_dir);
  return rcx().reportSpef(resolved_output_dir);
}

}  // namespace python_interface
