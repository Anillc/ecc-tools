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

struct PadInfo {
    std::string id;
    std::string name;
    int32_t x;
    int32_t y;
};

struct CoreInfo {
    int32_t lx;
    int32_t ly;
    int32_t ux;
    int32_t uy;
};

struct MacroInfo {
    std::string id;       // Unique identifier for the macro in original design
    std::string name;
    int32_t x, y;         // Macro position
    int32_t width, height; // Macro dimensions
    std::string orient;    // Orientation (e.g., R0, R90, MX, MY)
};

struct BlockageInfo {
    int32_t lx;
    int32_t ly;
    int32_t ux;
    int32_t uy;
};

class Refinement
{
public:
    Refinement(std::weak_ptr<ParserEngine> parser);

    ~Refinement();

    void initPostProcessingData(float macro_halo_micron);

    void runRefinement(std::string output_tcl);

    void extractMacroData();

    void extractBlockageData();

    void extractCoreData();

    void extractPadData();

    void export_to_json(const std::string& filename);

private:

    std::weak_ptr<ParserEngine> _parser;

    Block* _root_block;
    float _macro_halo;

    std::vector<MacroInfo> _macros;

    std::string orientToString(Orient orient);

    std::vector<BlockageInfo> _blockages;

    CoreInfo _core;

    std::vector<PadInfo> _pads;
};

}  // namespace imp
