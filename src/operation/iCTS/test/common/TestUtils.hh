// compatibility header
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "types/TestDataTypes.hh"

namespace icts {
class Pin;
class Tree;
template <typename T>
class Point;
}  // namespace icts

namespace icts_test {

auto MakeNormal(std::size_t count, CanvasSize canvas, unsigned seed) -> GeneratedPins;
auto MakeGaussianMixture(std::size_t count, CanvasSize canvas, unsigned seed) -> GeneratedPins;
auto MakeWeightedQuadrants(std::size_t count, CanvasSize canvas, unsigned seed, const std::array<double, 4>& weights) -> GeneratedPins;

auto AnalyzeTopology(const icts::Tree& tree, const std::vector<icts::Pin*>& loads, TopologyStats& stats,
                     std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                     std::string& error) -> bool;
auto AnalyzeFirstLevelClusters(const icts::Tree& tree, const std::vector<icts::Pin*>& loads,
                               std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                               std::string& error) -> bool;
auto WriteClusterSvg(const std::string& path, const std::vector<icts::Pin*>& loads,
                     const std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, const std::vector<icts::Point<int>>& centers)
    -> bool;
auto WriteTopologySvg(const std::string& path, const icts::Tree& tree, const std::vector<icts::Pin*>& loads) -> bool;
auto WriteTextLog(const std::filesystem::path& path, const std::string& content) -> bool;
void EmitInfoReport(const InfoReport& report);
auto SanitizeOutputName(const std::string& raw_name) -> std::string;
auto PrepareCleanOutputDir(const std::filesystem::path& path) -> std::filesystem::path;
auto ResolveOutputDir() -> std::filesystem::path;
auto ResolveTopologyGenOutputDir() -> std::filesystem::path;
auto ResolveLinearClusteringOutputDir() -> std::filesystem::path;

}  // namespace icts_test
