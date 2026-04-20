#include "MappingBuilder.hpp"

#include <fstream>
#include <sstream>

namespace ircx {
namespace parser {

void MappingBuilder::read(const std::string& mappingPath)
{
  std::ifstream mappingFile(mappingPath);
  std::string line;
  while (std::getline(mappingFile, line)) {
    std::string designLayerName;
    std::string processLayerName;
    if (std::istringstream(line) >> designLayerName >> processLayerName) {
      design_to_process_layer_names_[designLayerName] = processLayerName;
      process_to_design_layer_names_[processLayerName] = designLayerName;
    }
  }
}

}  // namespace parser
}  // namespace ircx
