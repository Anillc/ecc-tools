#pragma once
#include <cstddef>
#include <memory>
#include <numeric>
#include <unordered_set>
namespace imp {
class Block;
class HMetis;
class ParserEngine;

struct BlkClustering
{
  void operator()(imp::Block& block);

  size_t l1_nparts{std::numeric_limits<size_t>::max()};
  size_t l2_nparts{std::numeric_limits<size_t>::max()};
};

struct BlkClustering2
{
  void operator()(imp::Block& block);
  size_t l1_nparts{std::numeric_limits<size_t>::max()};
  size_t l2_nparts{std::numeric_limits<size_t>::max()};
  size_t level_num = 2;
  std::weak_ptr<ParserEngine> parser;
  std::unordered_set<std::string> critical_nets_name;
  std::unordered_set<std::string> non_critical_nets_name;
};

}  // namespace imp
