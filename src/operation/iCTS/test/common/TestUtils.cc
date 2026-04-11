// compatibility source

#include "common/TestUtils.hh"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/data/TestDataGenerator.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/topology/TopologyAnalysis.hh"
#include "common/types/TestDataTypes.hh"
#include "common/visualization/TestVisualization.hh"

namespace icts_test {

auto MakeNormal(std::size_t count, CanvasSize canvas, unsigned seed) -> GeneratedPins
{
  return common::data::MakeNormal(count, canvas, seed);
}

auto MakeGaussianMixture(std::size_t count, CanvasSize canvas, unsigned seed) -> GeneratedPins
{
  return common::data::MakeGaussianMixture(count, canvas, seed);
}

auto MakeWeightedQuadrants(std::size_t count, CanvasSize canvas, unsigned seed, const std::array<double, 4>& weights) -> GeneratedPins
{
  return common::data::MakeWeightedQuadrants(count, canvas, seed, weights);
}

auto AnalyzeTopology(const icts::Tree& tree, const std::vector<icts::Pin*>& loads, TopologyStats& stats,
                     std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                     std::string& error) -> bool
{
  return common::topology::AnalyzeTopology(tree, loads, stats, cluster_map, centers, error);
}

auto AnalyzeFirstLevelClusters(const icts::Tree& tree, const std::vector<icts::Pin*>& loads,
                               std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                               std::string& error) -> bool
{
  return common::topology::AnalyzeFirstLevelClusters(tree, loads, cluster_map, centers, error);
}

auto WriteClusterSvg(const std::string& path, const std::vector<icts::Pin*>& loads,
                     const std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, const std::vector<icts::Point<int>>& centers)
    -> bool
{
  return common::visualization::WriteClusterSvg(path, loads, cluster_map, centers);
}

auto WriteTopologySvg(const std::string& path, const icts::Tree& tree, const std::vector<icts::Pin*>& loads) -> bool
{
  return common::visualization::WriteTopologySvg(path, tree, loads);
}

auto WriteTextLog(const std::filesystem::path& path, const std::string& content) -> bool
{
  return common::io::WriteTextLog(path, content);
}

auto EmitInfoReport(const InfoReport& report) -> void
{
  common::io::EmitInfoReport(report);
}

auto SanitizeOutputName(const std::string& raw_name) -> std::string
{
  return common::io::SanitizeOutputName(raw_name);
}

auto PrepareCleanOutputDir(const std::filesystem::path& path) -> std::filesystem::path
{
  return common::io::PrepareCleanOutputDir(path);
}

auto ResolveOutputDir() -> std::filesystem::path
{
  return common::io::ResolveOutputDir();
}

auto ResolveTopologyGenOutputDir() -> std::filesystem::path
{
  return common::io::ResolveTopologyGenOutputDir();
}

auto ResolveLinearClusteringOutputDir() -> std::filesystem::path
{
  return common::io::ResolveLinearClusteringOutputDir();
}

}  // namespace icts_test
