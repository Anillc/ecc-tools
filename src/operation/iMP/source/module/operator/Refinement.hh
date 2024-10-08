#pragma once
#include <cstddef>
#include <functional>
#include <future>
#include <numeric>
#include <set>
#include <unordered_set>
#include <vector>
#include <string>

#include "Block.hh"
#include "ClusterTimingEvaluator.hh"
#include "IDBParserEngine.hh"
#include "Layout.hh"
#include "Logger.hpp"
#include "Net.hh"
#include "Pin.hh"
#include "thread"

namespace imp {

struct MacroInfo {
    std::string id;       // Unique identifier for the macro in original design
    std::string name;
    int32_t x, y;         // Macro position
    int32_t width, height; // Macro dimensions
    std::string orient;    // Orientation (e.g., R0, R90, MX, MY)
};

class Refinement
{
public:
    Refinement(std::weak_ptr<ParserEngine> parser);

    ~Refinement();

    void initPostProcessingData(float macro_halo_micron);

    void runRefinement(std::string output_tcl);

    void extractMacroData();

private:

    std::weak_ptr<ParserEngine> _parser;

    Block* _root_block;
    float _macro_halo;

    std::vector<MacroInfo> _macros;

    std::string orientToString(Orient orient);
};

}  // namespace imp
