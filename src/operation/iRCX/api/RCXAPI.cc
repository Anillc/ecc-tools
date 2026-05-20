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
#include "RCXAPI.hh"

#include "Flow.hh"
#include "Setup.hh"

namespace ircx {

void RCXAPI::init(const RCXInitOptions& options)
{
  RCX_FLOW_INST.set_num_threads(options.thread_number);
  RCX_FLOW_INST.set_operating_temperature(options.operating_temperature);
}

void RCXAPI::resetAPI()
{
  RCX_FLOW_INST.reset();
}

unsigned RCXAPI::readCorner(const std::string& corner_name,
                            const char* itf_file,
                            const char* captab_file)
{
  return Setup::readCorner(corner_name, itf_file, captab_file);
}

unsigned RCXAPI::readMapping(const char* mapping_file)
{
  return Setup::readMapping(mapping_file);
}

unsigned RCXAPI::adaptDB()
{
  return Setup::readData();
}

unsigned RCXAPI::checkShortOpen()
{
  return RCX_FLOW_INST.checkShortOpen();
}

unsigned RCXAPI::buildTopology()
{
  return RCX_FLOW_INST.buildTopology();
}

unsigned RCXAPI::buildEnvironment()
{
  return RCX_FLOW_INST.buildEnvironment();
}

unsigned RCXAPI::buildProcessVariation()
{
  return RCX_FLOW_INST.buildProcessVariation();
}

unsigned RCXAPI::extractParasitics()
{
  return RCX_FLOW_INST.extractParasitics();
}

unsigned RCXAPI::run()
{
  if (!adaptDB()) {
    return 0;
  }

  return RCX_FLOW_INST.run();
}

unsigned RCXAPI::runFromConfig(const std::string& config)
{
  if (!Setup::initialize(config)) {
    return 0;
  }
  if (!run()) {
    return 0;
  }

  return reportSpef(RCX_FLOW_INST.output_dir());
}

unsigned RCXAPI::reportSpef(const std::string& output_dir)
{
  return RCX_FLOW_INST.reportSpef(output_dir.empty() ? "." : output_dir);
}

}  // namespace ircx
