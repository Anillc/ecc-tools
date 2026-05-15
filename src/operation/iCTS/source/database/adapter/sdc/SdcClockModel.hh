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
 * @file SdcClockModel.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-15
 * @brief CTS-owned SDC clock records for clock tracing.
 */

#pragma once

#include <string>
#include <vector>

namespace icts {

enum class SdcObjectKind
{
  kPort,
  kPin,
  kNet,
  kClock,
  kUnknown,
};

struct SdcObjectRef
{
  SdcObjectKind kind = SdcObjectKind::kUnknown;
  std::string pattern;
  bool from_collection_cmd = false;
};

struct SdcClockDecl
{
  enum class Kind
  {
    kPrimary,
    kGenerated,
  };

  Kind kind = Kind::kPrimary;
  std::string clock_name;
  std::vector<SdcObjectRef> targets;
  std::vector<SdcObjectRef> generated_sources;
  std::string master_clock_name;
  double period_ns = 0.0;
  bool period_resolved = false;
  int divide_by = 1;
  int multiply_by = 1;
  bool invert = false;
  bool is_virtual = false;
};

struct SdcCaseAnalysis
{
  int value = 0;
  std::vector<SdcObjectRef> objects;
};

struct SdcClockData
{
  std::vector<SdcClockDecl> clocks;
  std::vector<SdcCaseAnalysis> case_analyses;
  std::vector<std::string> diagnostics;
};

}  // namespace icts
