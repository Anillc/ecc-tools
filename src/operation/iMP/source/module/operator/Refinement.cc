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

void Refinement::initPostProcessingData(
    float macro_halo_micron, 
    const std::string& original_pin_dir, 
    int exp_space_x, 
    int exp_space_y, 
    int search_space_x, 
    int search_space_y, 
    int gap, 
    int virtual_macro_size, 
    bool beikaobei, 
    float h_weight, 
    float v_weight, 
    bool consider_std
) {
    std::cout << "Initializing post-processing data..." << std::endl;

    auto shared_parser = _parser.lock();
    assert(shared_parser && "Parser has expired");

    idb::IdbBuilder* idb_builder = shared_parser->getIdbBuilder();
    dbu = idb_builder->get_def_service()->get_layout()->get_units()->get_micron_dbu();
    _macro_halo = dbu * macro_halo_micron;
    _original_pin_dir = original_pin_dir;
    _exp_space_x = dbu * exp_space_x;
    _exp_space_y = dbu * exp_space_y;
    _search_space_x = dbu * search_space_x;
    _search_space_y = dbu * search_space_y;
    _gap = dbu * gap;
    _virtual_macro_size = dbu * virtual_macro_size;
    _beikaobei = beikaobei;
    _h_weight = h_weight;
    _v_weight = v_weight;
    _consider_std = consider_std;

    std::cout << "Macro halo set to: " << _macro_halo << " in database units" << std::endl;

    extractCoreData();
    extractMacroData();
    extractBlockageData();
    extractPadData();
}

void Refinement::extractCoreData()
{
    std::cout << "Extracting core data..." << std::endl;

    idb::IdbBuilder* idb_builder = _parser.lock()->getIdbBuilder();
    auto* layout = idb_builder->get_def_service()->get_layout();
    idb::IdbCore* core = layout->get_core();

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

            if (inst_ptr->isFixed()) {
                macro_info.is_fixed = true;
                _fix_macros.push_back(macro_info);
            } else {
                macro_info.is_fixed = false;
                _mov_macros.push_back(macro_info);
            }
        }
    }
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
            int lx = inst_ptr->get_lx(); 
            int ly = inst_ptr->get_ly(); 
            int width = inst_ptr->get_width(); 
            int height = inst_ptr->get_height();

            blockage_info.lx = lx;
            blockage_info.ly = ly;

            switch (inst_ptr->get_orient()) {
                case Orient::kN_R0:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
                case Orient::kW_R90:
                    blockage_info.ux = lx + height;
                    blockage_info.uy = ly + width;
                    break;
                case Orient::kS_R180:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
                case Orient::kE_R270:
                    blockage_info.ux = lx + height;
                    blockage_info.uy = ly + width;
                    break;
                case Orient::kFS_MX:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
                case Orient::kFN_MY:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
                default:
                    blockage_info.ux = lx + width;
                    blockage_info.uy = ly + height;
                    break;
            }
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
        if (inst_ptr->get_cell_master().isIOCell()) {
            PadInfo pad_info;
            pad_info.id = inst_ptr->get_name();
            pad_info.name = inst_ptr->get_name();

            auto bbox = inst_ptr->boundingbox();
            pad_info.x = bbox.min_corner().x();
            pad_info.y = bbox.min_corner().y();

            _pads.push_back(pad_info);
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

    if (0) {
        std::cout << "Core bounds: (" << _core.lx << ", " << _core.ly << ") to ("
                << _core.ux << ", " << _core.uy << ")" << std::endl;

        for (const auto& macro : _mov_macros) {
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

    export_to_json("test.json");
}

void Refinement::export_to_json(const std::string& filename)
{
    nlohmann::json json_data;

    json_data["core"] = {
        {"lx", _core.lx}, {"ly", _core.ly}, {"ux", _core.ux}, {"uy", _core.uy}
    };

    for (const auto& macro : _mov_macros) {
        json_data["macros"].push_back({
            {"id", macro.id},
            {"name", macro.name},
            {"x", macro.x}, 
            {"y", macro.y}, 
            {"width", macro.width},
            {"height", macro.height},
            {"orientation", macro.orient}
        });
    }

    for (const auto& blockage : _blockages) {
        json_data["blockages"].push_back({
            {"lx", blockage.lx}, {"ly", blockage.ly}, {"ux", blockage.ux}, {"uy", blockage.uy}
        });
    }

    for (const auto& pad : _pads) {
        json_data["pads"].push_back({
            {"id", pad.id},
            {"name", pad.name},
            {"x", pad.x},
            {"y", pad.y}
        });
    }

    std::ofstream file(filename);
    file << json_data.dump(4);
    file.close();

    std::cout << "Data exported to " << filename << std::endl;
}

void Refinement::readTcl(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open file " << file_path << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string command;
        iss >> command;

        if (command == "placeInstance") {
            std::string macro_name;
            double x, y;
            std::string orientation;
            iss >> macro_name >> x >> y >> orientation;

            bool found = false;
            for (auto& macro : _mov_macros) {
                if (macro.name == macro_name) {
                    macro.x = static_cast<int32_t>(x) * dbu;
                    macro.y = static_cast<int32_t>(y) * dbu;
                    macro.orient = orientation;
                    found = true;
                    break;
                }
            }

            if (!found) {
                std::cerr << "Warning: Macro " << macro_name << " not found in _macros." << std::endl;
            }
        } else if (command == "setInstancePlacementStatus") {
            std::string status_flag, macro_name, status;
            iss >> status_flag >> status_flag >> status >> status_flag >> macro_name;

        }
    }

    file.close();
}

}  // namespace imp
