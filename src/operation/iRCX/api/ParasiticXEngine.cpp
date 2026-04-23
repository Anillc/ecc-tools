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
#include <filesystem>
#include <mutex>
#include <system_error>

#include <omp.h>

#include "ParasiticXEngine.hpp"
#include "RCX.hpp"
#include "RCXConfig.hh"
#include "log/Log.hh"

namespace ircx {

namespace fs = std::filesystem;

ParasiticXEngine* ParasiticXEngine::_instance = nullptr;

ParasiticXEngine* ParasiticXEngine::get_or_create_parasitic_x_engine()
{
  static std::mutex mutex;
  if (_instance == nullptr) {
    std::lock_guard<std::mutex> lock(mutex);
    if (_instance == nullptr) {
      _instance = new ParasiticXEngine();
      _instance->set_rcx();
    }
  }
  return _instance;
}

void ParasiticXEngine::destroyParasiticXEngine()
{
  delete _instance;
}

ParasiticXEngine::ParasiticXEngine() : _rcx(nullptr), _db_adapter(nullptr) {}

// setter
void ParasiticXEngine::set_rcx(RCX* rcx)
{
  if (rcx) {
    _rcx = rcx;
  } else {
    RCX& rcx_inst = RCX::getOrCreateInst();
    _rcx = &rcx_inst;
  }
}

bool ParasiticXEngine::run_rcx(const std::string& config)
{
  if (_rcx == nullptr) {
    return false;
  }

  RCXConfig rcx_config;
  if (!rcx_config.loadFromFile(config)) {
    return false;
  }

  const auto& corner = rcx_config.get_corner();
  _rcx->set_num_threads(rcx_config.get_thread_num());
  omp_set_num_threads(_rcx->num_threads());

  unsigned result = 1;
  result &= _rcx->readCorner(corner.name, corner.itf_file.c_str(), corner.captab_file.c_str());
  result &= _rcx->readMapping(rcx_config.get_mapping_file().c_str());
  if (!result) {
    return false;
  }

  result &= _rcx->adaptDB();
  result &= _rcx->buildTopology();
  result &= _rcx->buildEnvironment();
  result &= _rcx->buildProcessVariation();
  result &= _rcx->extractParasitics();
  if (!result) {
    return false;
  }

  const std::string output_dir = rcx_config.get_output_dir().empty() ? "." : rcx_config.get_output_dir();
  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) {
    LOG_ERROR << "Failed to create RCX output directory " << output_dir << ": " << ec.message();
    return false;
  }

  result &= _rcx->reportSpef(output_dir);

  return result;
}

bool ParasiticXEngine::report_rcx(const std::string& output_dir)
{
  if (_rcx == nullptr) {
    return false;
  }

  const std::string resolved_output_dir = output_dir.empty() ? "." : output_dir;
  std::error_code ec;
  fs::create_directories(resolved_output_dir, ec);
  if (ec) {
    LOG_ERROR << "Failed to create RCX output directory " << resolved_output_dir << ": " << ec.message();
    return false;
  }

  return _rcx->reportSpef(resolved_output_dir);
}

}  // namespace ircx
