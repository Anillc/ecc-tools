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

#include <filesystem>
#include <string_view>

#include "StringUtils.hh"
#include "Types.hh"

namespace ircx {

inline Str resolvePath(const std::filesystem::path& base_dir, std::string_view raw_path)
{
  const Str trimmed_path = trimCopy(raw_path);
  if (trimmed_path.empty()) {
    return "";
  }

  std::filesystem::path path(trimmed_path);
  if (path.is_relative()) {
    path = base_dir / path;
  }

  return path.lexically_normal().string();
}

}  // namespace ircx
