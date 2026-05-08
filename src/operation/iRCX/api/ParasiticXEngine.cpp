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
#include <mutex>

#include "ParasiticXEngine.hpp"
#include "RCX.hpp"

namespace ircx {

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
  return _rcx != nullptr && _rcx->runFromConfig(config);
}

bool ParasiticXEngine::report_rcx(const std::string& output_dir)
{
  return _rcx != nullptr && _rcx->reportSpef(output_dir.empty() ? "." : output_dir);
}

}  // namespace ircx
