#include "Refinement.hh"
#include <iostream>
#include <fstream>
#include <cassert>

namespace imp {

Refinement::Refinement(std::weak_ptr<ParserEngine> parser)
    : _parser(parser), _root_block(nullptr), _macro_halo(0.0) {
}

Refinement::~Refinement() {
}

void Refinement::initPostProcessingData(float macro_halo_micron)
{
    std::cout << "Initializing post-processing data..." << std::endl;

    auto shared_parser = _parser.lock();
    assert(shared_parser && "Parser has expired");

    idb::IdbBuilder* idb_builder = shared_parser->getIdbBuilder();
    float dbu = idb_builder->get_def_service()->get_layout()->get_units()->get_micron_dbu();
    _macro_halo = dbu * macro_halo_micron;

    std::cout << "Macro halo set to: " << _macro_halo << " in database units" << std::endl;

    extractCoreData();
    extractMacroData();
    extractBlockageData();
    extractPadData();
}

void Refinement::extractCoreData()
{
    std::cout << "Extracting core data..." << std::endl;

    // 使用 idb_builder 获取 Core 信息
    idb::IdbBuilder* idb_builder = _parser.lock()->getIdbBuilder();
    auto* layout = idb_builder->get_def_service()->get_layout();
    idb::IdbCore* core = layout->get_core();  // 获取 core 对象

    // 获取 Core 的 bounding box
    auto* bounding_box = core->get_bounding_box();

    _core.lx = bounding_box->get_low_x();
    _core.ly = bounding_box->get_low_y();
    _core.ux = bounding_box->get_high_x();
    _core.uy = bounding_box->get_high_y();

    std::cout << "Extracted core bounds: (" << _core.lx << ", " << _core.ly << "), (" 
              << _core.ux << ", " << _core.uy << ")" << std::endl;
}

void Refinement::extractMacroData()
{
    const auto& instances = _parser.lock()->get_instances();
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
}

void Refinement::extractBlockageData()
{
    std::cout << "Extracting blockage data..." << std::endl;

    idb::IdbBuilder* idb_builder = _parser.lock()->getIdbBuilder();
    idb::IdbBlockageList* blockage_list = idb_builder->get_def_service()->get_design()->get_blockage_list();

    if (blockage_list != nullptr) {
        for (auto* blockage : blockage_list->get_blockage_list()) {
            for (auto* rect : blockage->get_rect_list()) {
                BlockageInfo blockage_info;
                blockage_info.lx = rect->get_low_x();
                blockage_info.ly = rect->get_low_y();
                blockage_info.ux = rect->get_high_x();
                blockage_info.uy = rect->get_high_y();

                _blockages.push_back(blockage_info);
            }
        }
    }

    const auto& instances = _parser.lock()->get_instances();
    for (const auto& [name, inst_ptr] : instances) {
        if (inst_ptr->get_cell_master().isMacro() && inst_ptr->isFixed()) {
            BlockageInfo blockage_info;
            blockage_info.lx = inst_ptr->get_halo_lx();
            blockage_info.ly = inst_ptr->get_halo_ly();
            blockage_info.ux = inst_ptr->get_halo_ux();
            blockage_info.uy = inst_ptr->get_halo_uy();
            _blockages.push_back(blockage_info);
        }
    }

    std::cout << "Extracted " << _blockages.size() << " blockages." << std::endl;
}

void Refinement::extractPadData()
{
    std::cout << "Extracting pad data..." << std::endl;

    const auto& instances = _parser.lock()->get_instances();
    for (const auto& [name, inst_ptr] : instances) {
        if (inst_ptr->get_cell_master().isIOCell()) {  // 假设 PAD 是 IOCell 类型
            PadInfo pad_info;
            pad_info.id = inst_ptr->get_name();
            pad_info.name = inst_ptr->get_name();

            auto bbox = inst_ptr->boundingbox();
            pad_info.x = bbox.min_corner().x();  // 左下角x坐标
            pad_info.y = bbox.min_corner().y();  // 左下角y坐标

            _pads.push_back(pad_info);  // 存储 PAD 信息
        }
    }

    std::cout << "Extracted " << _pads.size() << " pads." << std::endl;
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

    for (const auto& macro : _macros) {
        std::cout << "Refining macro: " << macro.name << std::endl;
        std::cout << "  ID: " << macro.id << std::endl;
        std::cout << "  Position: (" << macro.x << ", " << macro.y << ")" << std::endl;
        std::cout << "  Size: " << macro.width << "x" << macro.height << std::endl;
        std::cout << "  Orientation: " << macro.orient << std::endl;
    }

    for (const auto& blockage : _blockages) {
        std::cout << "Blockage: (" << blockage.lx << ", " << blockage.ly << ") to (" 
                  << blockage.ux << ", " << blockage.uy << ")" << std::endl;
    }

    for (const auto& pad : _pads) {
        std::cout << "Pad: " << pad.name << std::endl;
        std::cout << "  ID: " << pad.id << std::endl;
        std::cout << "  Position: (" << pad.x << ", " << pad.y << ")" << std::endl;
    }
}

}  // namespace imp
