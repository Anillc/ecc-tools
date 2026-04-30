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
 * @file HTreeBuilderInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Shared internal H-tree builder helper contracts.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "LogFormat.hh"
#include "PatternId.hh"
#include "Point.hh"
#include "SegmentChar.hh"
#include "Tree.hh"
#include "ValueLattice.hh"
#include "characterization/CharBuilder.hh"
#include "htree/CharacterizationLibrary.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/HTreeCandidateTypes.hh"
#include "htree/HTreeCharacterizationTypes.hh"
#include "htree/HTreePatternRegistry.hh"
#include "htree/HTreeSinkLoadProfileTypes.hh"

namespace icts::htree_builder {

auto ToCharGridSourceName(CharGridSource source) -> const char*;
auto LogInfoTable(const std::string& title, const std::vector<std::string>& headers, const logformat::TableRows& rows) -> void;
auto CountUniqueAlignedLengthBins(const std::vector<double>& requested_lengths_um, double length_step_um) -> unsigned;
auto CollectRequestedLevelLengthsUm(const Tree& topology, int32_t dbu_per_um) -> std::vector<double>;
auto ResolveCharacterizationGridPlan(const Tree& topology, int32_t dbu_per_um) -> CharacterizationGridPlan;
auto ResolveCharacterizationGridPlan(const std::vector<double>& requested_lengths_um) -> CharacterizationGridPlan;
auto BuildLevelPlans(const Tree& topology, double length_step_um, int32_t dbu_per_um) -> std::vector<HTreeBuilder::LevelPlan>;
auto ResolveDirectCharacterizationLengthIndices(const Tree& topology, const CharacterizationGridPlan& char_grid_plan, int32_t dbu_per_um)
    -> std::vector<unsigned>;
auto ResolveDirectCharacterizationLengthIndices(const std::vector<double>& requested_lengths_um,
                                                const CharacterizationGridPlan& char_grid_plan) -> std::vector<unsigned>;
auto MakeCandidateLevelPlans(const std::vector<HTreeBuilder::LevelPlan>& full_level_plans, unsigned depth)
    -> std::vector<HTreeBuilder::LevelPlan>;
auto CountCandidateLeafNodes(const Tree& topology, unsigned depth) -> std::size_t;
auto ResolveDepthCandidates(unsigned max_depth, const HTreeBuilder::BuildOptions& options) -> std::vector<unsigned>;
auto ResolveBuildOptions(const HTreeBuilder::BuildOptions& options, const CharBuilder& char_builder) -> ResolvedBuildOptions;
auto RunCharacterizationFlow(const Tree& topology, int32_t dbu_per_um, const CharBuilder::InitOptions& base_char_options,
                             HTreeBuilder::BuildResult& result, CharacterizationLibrary& char_library,
                             const HTreeBuilder::BuildOptions& options) -> HTreeCharacterizationFlowResult;
auto SearchTopologyDepthCandidates(const Tree& topology, const std::vector<HTreeBuilder::LevelPlan>& full_level_plans,
                                   const std::vector<unsigned>& depth_candidates,
                                   const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets_by_length,
                                   BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& base_resolved_options,
                                   const UniformValueLattice& cap_lattice, unsigned char_slew_steps, bool used_explicit_target_depth)
    -> HTreeTopologyDepthSearchResult;
auto EvaluateTopologyDepthCandidate(const Tree& topology, const std::vector<HTreeBuilder::LevelPlan>& full_level_plans, unsigned depth,
                                    const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets_by_length,
                                    BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& base_resolved_options,
                                    SinkLoadProfileLegalityContext& sink_load_profile_legality_context, unsigned char_slew_steps)
    -> HTreeTopologyDepthEvaluationResult;
auto RecordTopologyDepthCandidateResult(unsigned depth, bool used_explicit_target_depth,
                                        const HTreeTopologyDepthEvaluationResult& candidate_result,
                                        std::vector<HTreeTopologyDepthSummary>& depth_summaries) -> void;
auto AppendGlobalCandidateRefs(std::size_t candidate_index, const CandidateBuildEvaluation& evaluation,
                               std::vector<CandidateCharRef>& global_feasible_pool, std::vector<CandidateCharRef>& global_candidate_pool)
    -> void;
auto LogHTreeSynthesisSummary(const HTreeBuilder::BuildResult& result, const CandidateBuildEvaluation& selected_evaluation,
                              const HTreeTopologyDepthSummary& selected_summary) -> void;
auto HasBoundaryConstraints(const ResolvedBuildOptions& options) -> bool;
auto CoveringBoundaryIndex(double value, const UniformValueLattice& lattice) -> std::optional<unsigned>;
auto SynthesizeSegmentEntrySets(const std::vector<SegmentChar>& base_segment_chars, BufferPatternRegistry& pattern_registry,
                                const std::vector<unsigned>& required_length_indices)
    -> std::unordered_map<unsigned, SegmentCandidateFrontierSet>;
auto CollectRequiredLengthIndices(const std::vector<HTreeBuilder::LevelPlan>& levels) -> std::vector<unsigned>;
auto EvaluateCandidateBuild(const std::vector<HTreeBuilder::LevelPlan>& levels,
                            const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets_by_length,
                            const BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& resolved_options,
                            const Tree& topology, SinkLoadProfileLegalityContext& sink_load_profile_legality_context,
                            std::size_t leaf_count, unsigned depth, unsigned char_slew_steps) -> CandidateBuildEvaluation;
auto FilterGlobalEntriesBySinkLoadProfileCoverage(const std::vector<CandidateCharRef>& entries,
                                                  const std::vector<CandidateBuildEvaluation>& evaluations, const Tree& topology,
                                                  const BufferPatternRegistry& segment_pattern_registry,
                                                  SinkLoadProfileLegalityContext& legality_context) -> CandidateCharRefFilterResult;
auto ResolveSinkLoadProfileLegality(const Tree& topology, PatternId topology_pattern_id, const TopologyPatternRegistry& topology_registry,
                                    const BufferPatternRegistry& segment_pattern_registry, SinkLoadProfileLegalityContext& legality_context)
    -> SinkLoadProfileLegalityResult;
auto FilterSinkLoadProfileLegalEntries(const std::vector<HTreeTopologyChar>& entries, const Tree& topology,
                                       const TopologyPatternRegistry& topology_registry,
                                       const BufferPatternRegistry& segment_pattern_registry,
                                       SinkLoadProfileLegalityContext& legality_context) -> SinkLoadProfileEntryFilterResult;
auto SelectBestGlobalEntry(const std::vector<CandidateCharRef>& entries) -> std::optional<CandidateCharRef>;
auto CalcBoundaryFallbackScore(const HTreeTopologyChar& entry, const ResolvedBuildOptions& resolved_options, unsigned slew_steps) -> double;
auto InterpolateManhattanPoint(const Point<int>& source, const Point<int>& sink, double normalized_position) -> Point<int>;
auto ValidateRootDriverSizing(const HTreeBuilder::BuildResult& result, const std::string& cell_master) -> bool;
auto ApplyRootDriverSizing(HTreeBuilder::BuildResult& result, const std::string& cell_master) -> bool;
auto BuildClockTreeObjects(HTreeBuilder::BuildResult& result, const BufferPatternRegistry& segment_pattern_registry) -> void;

}  // namespace icts::htree_builder
