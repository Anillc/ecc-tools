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
 * @file StatisticsWriter.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <string>

#include "EvaluatorData.hh"

namespace icts {

class EvaluatorRuntimeContextInterface;

class StatisticsWriter
{
 public:
  void writeStatistics(const EvaluatorRuntimeContextInterface& context, const std::string& save_dir, const EvaluatorMetrics& metrics) const;

 private:
  void pathLevelLog(const EvaluatorRuntimeContextInterface& context, const std::vector<PathInfo>& path_infos) const;
};

}  // namespace icts
