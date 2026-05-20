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

#include <string>

namespace ircx {

#define RCX_API_INST (ircx::RCXAPI::getInst())
#define RCXAPIInst RCX_API_INST

class RCXAPI final {
 public:
  static RCXAPI& getInst() {
    static RCXAPI inst;
    return inst;
  }

  void init();
  void init(unsigned thread_number);
  void init(unsigned thread_number, double temperature);
  void setOperatingTemperature(double temperature);
  void resetAPI();

  [[nodiscard]] unsigned readCorner(const std::string& corner_name,
                                    const char* itf_file,
                                    const char* captab_file);
  [[nodiscard]] unsigned readMapping(const char* mapping_file);

  [[nodiscard]] unsigned adaptDB();

  [[nodiscard]] unsigned checkShortOpen();
  [[nodiscard]] unsigned buildTopology();
  [[nodiscard]] unsigned buildEnvironment();
  [[nodiscard]] unsigned buildProcessVariation();
  [[nodiscard]] unsigned extractParasitics();
  [[nodiscard]] unsigned run();
  [[nodiscard]] unsigned runFromConfig(const std::string& config);

  [[nodiscard]] unsigned reportSpef(const std::string& output_dir);

  RCXAPI(const RCXAPI&) = delete;
  RCXAPI& operator=(const RCXAPI&) = delete;
  RCXAPI(RCXAPI&&) = delete;
  RCXAPI& operator=(RCXAPI&&) = delete;

 private:
  RCXAPI() = default;
  ~RCXAPI() = default;
};

}  // namespace ircx
