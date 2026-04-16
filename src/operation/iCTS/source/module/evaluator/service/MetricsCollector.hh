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
 * @file MetricsCollector.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <vector>

#include "CtsNet.hh"
#include "EvalNet.hh"
#include "EvaluatorData.hh"

namespace icts {

class EvaluatorRuntimeContextInterface;

class MetricsCollector
{
 public:
  void transferEvalNets(const EvaluatorRuntimeContextInterface& context, std::vector<EvalNet>& eval_nets) const;
  void initLevel(const EvaluatorRuntimeContextInterface& context) const;
  void calcInfo(const EvaluatorRuntimeContextInterface& context, const std::vector<EvalNet>& eval_nets, EvaluatorMetrics& metrics) const;

 private:
  static CtsNet* resolveDrivenNet(CtsInstance* inst);
  void recursiveSetLevel(CtsNet* net) const;

  void calcWL(const EvaluatorRuntimeContextInterface& context, const std::vector<EvalNet>& eval_nets, EvaluatorMetrics& metrics) const;
  void calcCellDist(const EvaluatorRuntimeContextInterface& context, const std::vector<EvalNet>& eval_nets,
                    EvaluatorMetrics& metrics) const;
  void calcCellStats(const EvaluatorRuntimeContextInterface& context, EvaluatorMetrics& metrics) const;
  void calcNetLevel(const std::vector<EvalNet>& eval_nets, EvaluatorMetrics& metrics) const;
  void calcPathBufStats(const EvaluatorRuntimeContextInterface& context, EvaluatorMetrics& metrics) const;
};

}  // namespace icts
