#include "TopoPool.hpp"

#include <iterator>

#include "log/Log.hh"

namespace ircx {

// TopoEdge

void TopoEdge::set_shape(const GtlRectI& v) {
  shape_ = v;

  line_seg_.is_horz = geom::IsHorDominant(shape_);

  width_ = line_seg_.is_horz ? geom::DeltaY(shape_) : geom::DeltaX(shape_);
  half_width_ = width_ / 2;
  length_ = line_seg_.is_horz ? geom::DeltaX(shape_) : geom::DeltaY(shape_);
  center_ = geom::Center(shape_);

  line_seg_.fixed = line_seg_.is_horz ? geom::CenterY(shape_) : geom::CenterX(shape_);
  line_seg_.a0    = line_seg_.is_horz ? geom::MinX(shape_)    : geom::MinY(shape_);
  line_seg_.a1    = line_seg_.is_horz ? geom::MaxX(shape_)    : geom::MaxY(shape_);
}

// TopoPool

void TopoPool::reserve(Size net_count, Size total_nodes, Size total_edges) {
  net_node_ranges_.reserve(net_count);
  net_edge_ranges_.reserve(net_count);
  node_pool_.reserve(total_nodes);
  edge_pool_.reserve(total_edges);
}

std::span<const TopoNode> TopoPool::net_nodes(Size net_id) const {
  LOG_FATAL_IF(net_id >= net_node_ranges_.size()) << "net_id out of range.";
  auto [offset, count] = net_node_ranges_[net_id];
  return std::span<const TopoNode>(node_pool_).subspan(offset, count);
}

std::span<const TopoEdge> TopoPool::net_edges(Size net_id) const {
  LOG_FATAL_IF(net_id >= net_edge_ranges_.size()) << "net_id out of range.";
  auto [offset, count] = net_edge_ranges_[net_id];
  return std::span<const TopoEdge>(edge_pool_).subspan(offset, count);
}

std::span<TopoNode> TopoPool::net_nodes(Size net_id) {
  LOG_FATAL_IF(net_id >= net_node_ranges_.size()) << "net_id out of range.";
  auto [offset, count] = net_node_ranges_[net_id];
  return std::span<TopoNode>(node_pool_).subspan(offset, count);
}

std::span<TopoEdge> TopoPool::net_edges(Size net_id) {
  LOG_FATAL_IF(net_id >= net_edge_ranges_.size()) << "net_id out of range.";
  auto [offset, count] = net_edge_ranges_[net_id];
  return std::span<TopoEdge>(edge_pool_).subspan(offset, count);
}

std::pair<Size, Size> TopoPool::net_node_range(Size net_id) const {
  LOG_FATAL_IF(net_id >= net_node_ranges_.size()) << "net_id out of range.";
  return net_node_ranges_[net_id];
}

std::pair<Size, Size> TopoPool::net_edge_range(Size net_id) const {
  LOG_FATAL_IF(net_id >= net_edge_ranges_.size()) << "net_id out of range.";
  return net_edge_ranges_[net_id];
}

void TopoPool::addNet(std::vector<TopoNode> nodes,
                      std::vector<TopoEdge> edges)
{
  const Size node_off = node_pool_.size();
  const Size node_cnt = nodes.size();
  const Size edge_off = edge_pool_.size();
  const Size edge_cnt = edges.size();

  // Assign LOCAL ids (0..count-1) for the id() accessor.
  for (Size node_idx = 0; node_idx < nodes.size(); ++node_idx) nodes[node_idx].set_id(node_idx);
  for (Size edge_idx = 0; edge_idx < edges.size(); ++edge_idx) edges[edge_idx].set_id(edge_idx);

  net_node_ranges_.emplace_back(node_off, node_cnt);
  net_edge_ranges_.emplace_back(edge_off, edge_cnt);

  node_pool_.reserve(node_off + node_cnt);
  edge_pool_.reserve(edge_off + edge_cnt);

  node_pool_.insert(node_pool_.end(),
                    std::make_move_iterator(nodes.begin()),
                    std::make_move_iterator(nodes.end()));
  edge_pool_.insert(edge_pool_.end(),
                    std::make_move_iterator(edges.begin()),
                    std::make_move_iterator(edges.end()));

}

void TopoPool::addSpecialEdges(std::vector<TopoEdge> edges)
{
  for (Size edge_idx = 0; edge_idx < edges.size(); ++edge_idx) edges[edge_idx].set_id(edge_idx);

  special_edge_pool_.reserve(special_edge_pool_.size() + edges.size());
  special_edge_pool_.insert(special_edge_pool_.end(),
                            std::make_move_iterator(edges.begin()),
                            std::make_move_iterator(edges.end()));
}

}  // namespace ircx
