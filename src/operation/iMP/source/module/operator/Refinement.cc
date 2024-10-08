#include "Refinement.hh"
#include <iostream>
#include <fstream>

namespace imp {
Refinement::Refinement(std::weak_ptr<ParserEngine> parser)
    : _parser(parser), _root_block(nullptr), _macro_halo(0.0) {
}

Refinement::~Refinement() {
}

void Refinement::initPostProcessingData(float macro_halo_micron)
{
    std::cout << "Initializing post-processing data..." << std::endl;
    
    if (auto shared_parser = _parser.lock()) {

        idb::IdbBuilder* idb_builder = shared_parser->getIdbBuilder();
        float dbu = idb_builder->get_def_service()->get_layout()->get_units()->get_micron_dbu();
        _macro_halo = dbu * macro_halo_micron;

        std::cout << "Macro halo set to: " << _macro_halo << " in database units" << std::endl;

        extractMacroData();
    } else {
        std::cerr << "Failed to lock parser." << std::endl;
    }
}

void Refinement::extractMacroData()
{
    if (auto shared_parser = _parser.lock()) {
        
        const auto& instances = shared_parser->get_instances();
        for (const auto& [name, inst_ptr] : instances) {
        if (inst_ptr->get_cell_master().isMacro()) {
            MacroInfo macro_info;

            macro_info.id = inst_ptr->get_name();

            macro_info.name = inst_ptr->get_name();

            auto bbox = inst_ptr->boundingbox();
            macro_info.x = bbox.min_corner().x();
            macro_info.y = bbox.min_corner().y();

            macro_info.width = bbox.max_corner().x() - bbox.min_corner().x();
            macro_info.height = bbox.max_corner().y() - bbox.min_corner().y();

            macro_info.orient = orientToString(inst_ptr->get_orient());

            _macros.push_back(macro_info);
        }
}
        std::cout << "Extracted " << _macros.size() << " macros." << std::endl;
    } else {
        std::cerr << "Failed to lock parser." << std::endl;
    }
}

std::string Refinement::orientToString(Orient orient)
{
    switch (orient) {
        case Orient::kN_R0:
            return "R0";
        case Orient::kW_R90:
            return "R90";
        case Orient::kS_R180:
            return "R180";
        case Orient::kE_R270:
            return "R270";
        case Orient::kFN_MY:
            return "MY";
        case Orient::kFS_MX:
            return "MX";
        case Orient::kFW_MX90:
            return "MX90";
        case Orient::kFE_MY90:
            return "MY90";
        default:
            return "None";
    }
}

void Refinement::runRefinement(std::string output_tcl)
{
    std::cout << "Running refinement process..." << std::endl;

    if (auto shared_parser = _parser.lock()) {
        
        for (const auto& macro : _macros) {
            std::cout << "Refining macro: " << macro.name << std::endl;
            std::cout << "  ID: " << macro.id << std::endl;  // Display the macro ID
            std::cout << "  Position: (" << macro.x << ", " << macro.y << ")" << std::endl;
            std::cout << "  Size: " << macro.width << "x" << macro.height << std::endl;
            std::cout << "  Orientation: " << macro.orient << std::endl;
        }

        // writePlacementTcl(_root_block, output_tcl, _root_block->netlist().property()->get_database_unit());
    } else {
        std::cerr << "Parser is no longer available!" << std::endl;
    }
}

}  // namespace imp
