#pragma once

#include <unordered_map>
#include <string>

namespace ircx {
namespace parser {

class MappingBuilder
{
 public:
  MappingBuilder() = default;
  ~MappingBuilder() = default;

  const std::unordered_map<std::string, std::string>& design_to_process_layer_names() const {
    return design_to_process_layer_names_;
  }
  const std::unordered_map<std::string, std::string>& process_to_design_layer_names() const {
    return process_to_design_layer_names_;
  }

  void read(const std::string& mappingPath);

 private:
  std::unordered_map<std::string, std::string> design_to_process_layer_names_;
  std::unordered_map<std::string, std::string> process_to_design_layer_names_;
};

}  // namespace parser
}  // namespace ircx
