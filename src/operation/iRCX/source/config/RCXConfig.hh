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

class RCXConfig
{
 public:
  struct CornerConfig
  {
    std::string name;
    std::string itf_file;
    std::string captab_file;
  };

  RCXConfig() = default;
  ~RCXConfig() = default;

  [[nodiscard]] bool loadFromFile(const std::string& config_path);

  [[nodiscard]] const std::string& get_config_path() const { return _config_path; }
  [[nodiscard]] unsigned get_thread_num() const { return _thread_num; }
  [[nodiscard]] const std::string& get_output_dir() const { return _output_dir; }
  [[nodiscard]] const std::string& get_mapping_file() const { return _mapping_file; }
  [[nodiscard]] const CornerConfig& get_corner() const { return _corner; }

 private:
  std::string _config_path;
  unsigned _thread_num = 64U;
  std::string _output_dir;
  std::string _mapping_file;
  CornerConfig _corner;
};

}  // namespace ircx
