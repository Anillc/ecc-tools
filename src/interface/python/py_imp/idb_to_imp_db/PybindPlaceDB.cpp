/**
 * @file   PybindPlaceDB.cpp
 * @author Yibo Lin
 * @date   Apr 2020
 * @brief  Python binding for PlaceDB 
 */

#include "PyPlaceDB.h"

#if 0
PYBIND11_MAKE_OPAQUE(std::vector<bool>);
//PYBIND11_MAKE_OPAQUE(std::vector<char>);
//PYBIND11_MAKE_OPAQUE(std::vector<unsigned char>);
PYBIND11_MAKE_OPAQUE(std::vector<PlaceDB::coordinate_type>);
PYBIND11_MAKE_OPAQUE(std::vector<PlaceDB::index_type>);
//PYBIND11_MAKE_OPAQUE(std::vector<long>);
//PYBIND11_MAKE_OPAQUE(std::vector<unsigned long>);
//PYBIND11_MAKE_OPAQUE(std::vector<float>);
//PYBIND11_MAKE_OPAQUE(std::vector<double>);
PYBIND11_MAKE_OPAQUE(std::vector<std::string>);

PYBIND11_MAKE_OPAQUE(PlaceDB::string2index_map_type);

PYBIND11_MAKE_OPAQUE(std::vector<Box<Object::index_type>>);
PYBIND11_MAKE_OPAQUE(std::vector<Box<Object::coordinate_type>>);
PYBIND11_MAKE_OPAQUE(std::vector<Pin>);
PYBIND11_MAKE_OPAQUE(std::vector<Node>);
PYBIND11_MAKE_OPAQUE(std::vector<NodeProperty>);
PYBIND11_MAKE_OPAQUE(std::vector<Net>);
PYBIND11_MAKE_OPAQUE(std::vector<NetProperty>);
PYBIND11_MAKE_OPAQUE(std::vector<MacroPort>);
PYBIND11_MAKE_OPAQUE(std::vector<MacroPin>);
PYBIND11_MAKE_OPAQUE(std::vector<MacroObs>);
PYBIND11_MAKE_OPAQUE(std::vector<Macro>);
PYBIND11_MAKE_OPAQUE(std::vector<Row>);
PYBIND11_MAKE_OPAQUE(std::vector<SubRow>);
PYBIND11_MAKE_OPAQUE(std::vector<BinRow>);
PYBIND11_MAKE_OPAQUE(std::vector<Region>);
PYBIND11_MAKE_OPAQUE(std::vector<Group>);

void bind_PlaceDB(pybind11::module& m) 
{
    pybind11::bind_vector<std::vector<bool> >(m, "VectorBool");
    //pybind11::bind_vector<std::vector<char> >(m, "VectorChar", pybind11::buffer_protocol());
    //pybind11::bind_vector<std::vector<unsigned char> >(m, "VectorUChar", pybind11::buffer_protocol());
    pybind11::bind_vector<std::vector<PlaceDB::coordinate_type> >(m, "VectorCoordinate", pybind11::buffer_protocol());
    pybind11::bind_vector<std::vector<PlaceDB::index_type> >(m, "VectorIndex", pybind11::buffer_protocol());
    //pybind11::bind_vector<std::vector<long> >(m, "VectorLong", pybind11::buffer_protocol());
    //pybind11::bind_vector<std::vector<unsigned long> >(m, "VectorULong", pybind11::buffer_protocol());
    //pybind11::bind_vector<std::vector<float> >(m, "VectorFloat", pybind11::buffer_protocol());
    //pybind11::bind_vector<std::vector<double> >(m, "VectorDouble", pybind11::buffer_protocol());
    pybind11::bind_vector<std::vector<std::string> >(m, "VectorString");

    pybind11::bind_map<PlaceDB::string2index_map_type>(m, "MapString2Index");

    // Params.h
    pybind11::enum_<SolutionFileFormat>(m, "SolutionFileFormat")
        .value("DEF", DEF)
        .value("DEFSIMPLE", DEFSIMPLE)
        .value("BOOKSHELF", BOOKSHELF)
        .value("BOOKSHELFALL", BOOKSHELFALL)
        .export_values()
        ;

    // Util.h
    pybind11::enum_<Direction1DType>(m, "Direction1DType")
        .value("kLOW", kLOW)
        .value("kHIGH", kHIGH)
        .value("kX", kX)
        .value("kY", kY)
        .value("kLEFT", kLEFT)
        .value("kRIGHT", kRIGHT)
        .value("kBOTTOM", kBOTTOM)
        .value("kTOP", kTOP)
        .export_values()
        ;

    pybind11::enum_<Direction2DType>(m, "Direction2DType")
        .value("kXLOW", kXLOW)
        .value("kXHIGH", kXHIGH)
        .value("kYLOW", kYLOW)
        .value("kYHIGH", kYHIGH)
        .export_values()
        ;

    // Enums.h
    pybind11::class_<OrientEnum>orientenum (m, "OrientEnum")
        ;
    pybind11::enum_<OrientEnum::OrientType>(orientenum, "OrientType")
        .value("N", OrientEnum::N)
        .value("S", OrientEnum::S)
        .value("W", OrientEnum::W)
        .value("E", OrientEnum::E)
        .value("FN", OrientEnum::FN)
        .value("FS", OrientEnum::FS)
        .value("FW", OrientEnum::FW)
        .value("FE", OrientEnum::FE)
        .value("UNKNOWN", OrientEnum::UNKNOWN)
        .export_values()
        ;
    pybind11::class_<Orient>(m, "Orient")
        .def(pybind11::init<>())
        .def("value", &Orient::value)
        ;

    pybind11::class_<PlaceStatusEnum> placestatusenum (m, "PlaceStatusEnum")
        ;
    pybind11::enum_<PlaceStatusEnum::PlaceStatusType>(placestatusenum, "PlaceStatusType")
        .value("UNPLACED", PlaceStatusEnum::UNPLACED)
        .value("PLACED", PlaceStatusEnum::PLACED)
        .value("FIXED", PlaceStatusEnum::FIXED)
        .value("DUMMY_FIXED", PlaceStatusEnum::DUMMY_FIXED)
        .value("UNKNOWN", PlaceStatusEnum::UNKNOWN)
        .export_values()
        ;
    pybind11::class_<PlaceStatus> (m, "PlaceStatus")
        .def(pybind11::init<>())
        .def("value", &PlaceStatus::value)
        ;

    pybind11::class_<MultiRowAttrEnum> multirowattrenum (m, "MultiRowAttrEnum")
        ;
    pybind11::enum_<MultiRowAttrEnum::MultiRowAttrType>(multirowattrenum, "MultiRowAttrType")
        .value("SINGLE_ROW", MultiRowAttrEnum::SINGLE_ROW)
        .value("MULTI_ROW_ANY", MultiRowAttrEnum::MULTI_ROW_ANY)
        .value("MULTI_ROW_N", MultiRowAttrEnum::MULTI_ROW_N)
        .value("MULTI_ROW_S", MultiRowAttrEnum::MULTI_ROW_S)
        .value("UNKNOWN", MultiRowAttrEnum::UNKNOWN)
        .export_values()
        ;
    pybind11::class_<MultiRowAttr> (m, "MultiRowAttr")
        .def(pybind11::init<>())
        .def("value", &MultiRowAttr::value)
        ;

    pybind11::class_<SignalDirectEnum> signaldirectenum (m, "SignalDirectEnum")
        ;
    pybind11::enum_<SignalDirectEnum::SignalDirectType>(signaldirectenum, "SignalDirectType")
        .value("INPUT", SignalDirectEnum::INPUT)
        .value("OUTPUT", SignalDirectEnum::OUTPUT)
        .value("INOUT", SignalDirectEnum::INOUT)
        .value("UNKNOWN", SignalDirectEnum::UNKNOWN)
        .export_values()
        ;
    pybind11::class_<SignalDirect> (m, "SignalDirect")
        .def(pybind11::init<>())
        .def("value", &SignalDirect::value)
        ;

    pybind11::class_<PlanarDirectEnum> planardirectenum (m, "PlanarDirectEnum")
        ;
    pybind11::enum_<PlanarDirectEnum::PlanarDirectType>(planardirectenum, "PlanarDirectType")
        .value("HORIZONTAL", PlanarDirectEnum::HORIZONTAL)
        .value("VERTICAL", PlanarDirectEnum::VERTICAL)
        .value("UNKNOWN", PlanarDirectEnum::UNKNOWN)
        .export_values()
        ;
    pybind11::class_<PlanarDirect> (m, "PlanarDirect")
        .def(pybind11::init<>())
        .def("value", &PlanarDirect::value)
        ;

    pybind11::class_<RegionTypeEnum> regiontypeenum (m, "RegionTypeEnum")
        ;
    pybind11::enum_<RegionTypeEnum::RegionEnumType>(regiontypeenum, "RegionEnumType")
        .value("FENCE", RegionTypeEnum::FENCE)
        .value("GUIDE", RegionTypeEnum::GUIDE)
        .value("UNKNOWN", RegionTypeEnum::UNKNOWN)
        .export_values()
        ;
    pybind11::class_<RegionType> (m, "RegionType")
        .def(pybind11::init<>())
        .def("value", &RegionType::value)
        ;

    // Object.h
    pybind11::class_<Object> (m, "Object")
        .def(pybind11::init<>())
        //.def("setId", &Object::setId)
        .def("id", &Object::id)
        .def("__str__", &Object::toString)
        ;

    // Box.h
    pybind11::class_<Box<Object::coordinate_type>> (m, "BoxCoordinate")
        .def(pybind11::init<>())
        .def(pybind11::init<Box<Object::coordinate_type>::coordinate_type, Box<Object::coordinate_type>::coordinate_type, Box<Object::coordinate_type>::coordinate_type, Box<Object::coordinate_type>::coordinate_type>())
        //.def("unset", &Box<Object::coordinate_type>::unset, "set to uninitialized status")
        //.def("set", (Box<Object::coordinate_type>& (Box<Object::coordinate_type>::*)(Box<Object::coordinate_type>::coordinate_type, Box<Object::coordinate_type>::coordinate_type, Box<Object::coordinate_type>::coordinate_type, Box<Object::coordinate_type>::coordinate_type)) &Box<Object::coordinate_type>::set, "set xl, yl, xh, yh of the box")
        .def("xl", &Box<Object::coordinate_type>::xl)
        .def("yl", &Box<Object::coordinate_type>::yl)
        .def("xh", &Box<Object::coordinate_type>::xh)
        .def("yh", &Box<Object::coordinate_type>::yh)
        .def("width", &Box<Object::coordinate_type>::width)
        .def("height", &Box<Object::coordinate_type>::height)
        .def("area", &Box<Object::coordinate_type>::area)
        .def("__str__", &Box<Object::coordinate_type>::toString)
        ;
    pybind11::class_<Box<Object::index_type>> (m, "BoxIndex")
        .def(pybind11::init<>())
        .def(pybind11::init<Box<Object::index_type>::coordinate_type, Box<Object::index_type>::coordinate_type, Box<Object::index_type>::coordinate_type, Box<Object::index_type>::coordinate_type>())
        //.def("unset", &Box<Object::index_type>::unset, "set to uninitialized status")
        //.def("set", (Box<Object::index_type>& (Box<Object::index_type>::*)(Box<Object::index_type>::coordinate_type, Box<Object::index_type>::coordinate_type, Box<Object::index_type>::coordinate_type, Box<Object::index_type>::coordinate_type)) &Box<Object::index_type>::set, "set xl, yl, xh, yh of the box")
        .def("xl", &Box<Object::index_type>::xl)
        .def("yl", &Box<Object::index_type>::yl)
        .def("xh", &Box<Object::index_type>::xh)
        .def("yh", &Box<Object::index_type>::yh)
        .def("width", &Box<Object::index_type>::width)
        .def("height", &Box<Object::index_type>::height)
        .def("area", &Box<Object::index_type>::area)
        .def("__str__", &Box<Object::index_type>::toString)
        ;
    pybind11::bind_vector<std::vector<Box<Object::coordinate_type>> >(m, "VectorBoxCoordinate");
    pybind11::bind_vector<std::vector<Box<Object::index_type>> >(m, "VectorBoxIndex");

    // Site.h
    pybind11::class_<Site> (m, "Site")
        .def(pybind11::init<>())
        .def("name", &Site::name)
        .def("className", &Site::className)
        .def("symmetry", &Site::symmetry)
        .def("size", &Site::size)
        .def("width", &Site::width)
        .def("height", &Site::height)
        ;

    // Pin.h
    pybind11::class_<Pin, Object> (m, "Pin")
        .def(pybind11::init<>())
        .def("macroPinId", &Pin::macroPinId)
        .def("nodeId", &Pin::nodeId)
        .def("netId", &Pin::netId)
        .def("offset", &Pin::offset)
        .def("direct", &Pin::direct)
        ;
    pybind11::bind_vector<std::vector<Pin> >(m, "VectorPin");

    // Node.h
    pybind11::class_<Node, Box<Object::coordinate_type>, Object> (m, "Node")
        .def(pybind11::init<>())
        .def("status", &Node::status)
        .def("multiRowAttr", &Node::multiRowAttr)
        .def("orient", &Node::orient)
        .def("initPos", &Node::initPos)
        .def("pins", (std::vector<Node::index_type> const& (Node::*)() const) &Node::pins)
        .def("pinPos", (Node::point_type (Node::*)(Pin const&, Node::point_type const&) const) &Node::pinPos)
        .def("pinPos", (Node::point_type (Node::*)(Pin const&, Node::coordinate_type, Node::coordinate_type) const) &Node::pinPos)
        .def("pinPos", (Node::point_type (Node::*)(Pin const&) const) &Node::pinPos)
        .def("pinPos", (Node::coordinate_type (Node::*)(Pin const&, Direction1DType) const) &Node::pinPos)
        .def("pinX", &Node::pinX)
        .def("pinY", &Node::pinY)
        .def("siteArea", &Node::siteArea)
        .def("setStatus", (Node& (Node::*)(PlaceStatusEnum::PlaceStatusType s)) &Node::setStatus)
        ;
    pybind11::bind_vector<std::vector<Node> >(m, "VectorNode");

    pybind11::class_<NodeProperty> (m, "NodeProperty")
        .def(pybind11::init<>())
        .def("name", &NodeProperty::name)
        .def("macroId", &NodeProperty::macroId)
        ;
    pybind11::bind_vector<std::vector<NodeProperty> >(m, "VectorNodeProperty");

    // Net.h
    pybind11::class_<Net, Object> (m, "Net")
        .def(pybind11::init<>())
        .def("bbox", (Net::box_type const& (Net::*)() const) &Net::bbox)
        .def("weight", &Net::weight)
        .def("pins", (std::vector<Net::index_type> const& (Net::*)() const) &Net::pins)
        ;
    pybind11::bind_vector<std::vector<Net> >(m, "VectorNet");

    pybind11::class_<NetProperty> (m, "NetProperty")
        .def(pybind11::init<>())
        .def("name", &NetProperty::name)
        ;
    pybind11::bind_vector<std::vector<NetProperty> >(m, "VectorNetProperty");

    // MacroPin.h
    pybind11::class_<MacroPort, Object> (m, "MacroPort")
        .def(pybind11::init<>())
        .def("bbox", &MacroPort::bbox)
        .def("boxes", (std::vector<MacroPort::box_type> const& (MacroPort::*)() const) &MacroPort::boxes)
        .def("layers", (std::vector<std::string> const& (MacroPort::*)() const) &MacroPort::layers)
        ;
    pybind11::bind_vector<std::vector<MacroPort> >(m, "VectorMacroPort");

    pybind11::class_<MacroPin, Object> (m, "MacroPin")
        .def(pybind11::init<>())
        .def("name", &MacroPin::name)
        .def("direct", &MacroPin::direct)
        .def("bbox", &MacroPin::bbox)
        .def("macroPorts", (std::vector<MacroPort> const& (MacroPin::*)() const) &MacroPin::macroPorts)
        .def("macroPort", (MacroPort const& (MacroPin::*)(MacroPin::index_type) const) &MacroPin::macroPort)
        ;
    pybind11::bind_vector<std::vector<MacroPin> >(m, "VectorMacroPin");

    // MacroObs.h
    pybind11::class_<MacroObs, Object> (m, "MacroObs")
        .def(pybind11::init<>())
        .def("obsMap", (MacroObs::obs_map_type const& (MacroObs::*)() const) &MacroObs::obsMap)
        ;
    pybind11::bind_vector<std::vector<MacroObs> >(m, "VectorMacroObs");

    // Macro.h 
    pybind11::class_<Macro, Box<Object::coordinate_type>, Object> (m, "Macro")
        .def(pybind11::init<>())
        .def("name", &Macro::name)
        .def("className", &Macro::className)
        .def("siteName", &Macro::siteName)
        .def("edgeName", &Macro::edgeName)
        .def("symmetry", &Macro::symmetry)
        .def("initOrigin", &Macro::initOrigin)
        .def("obs", (MacroObs const& (Macro::*)() const) &Macro::obs)
        .def("macroPins", (std::vector<MacroPin> const& (Macro::*)() const) &Macro::macroPins)
        .def("macroPinName2Index", (Macro::string2index_map_type const& (Macro::*)() const) &Macro::macroPinName2Index)
        .def("macroPin", (MacroPin const& (Macro::*)(Macro::index_type) const) &Macro::macroPin)
        ;
    pybind11::bind_vector<std::vector<Macro> >(m, "VectorMacro");

    // Row.h
    pybind11::class_<Row, Box<Object::coordinate_type>, Object> (m, "Row")
        .def(pybind11::init<>())
        .def("name", &Row::name)
        .def("macroName", &Row::macroName)
        .def("orient", &Row::orient)
        .def("step", &Row::step)
        .def("numSites", &Row::numSites)
        ;
    pybind11::bind_vector<std::vector<Row> >(m, "VectorRow");

    pybind11::class_<SubRow, Box<Object::coordinate_type> > (m, "SubRow")
        .def(pybind11::init<>())
        .def("index1D", &SubRow::index1D)
        .def("rowId", &SubRow::rowId)
        .def("subRowId", &SubRow::subRowId)
        .def("binRows", (std::vector<SubRow::index_type> const& (SubRow::*)() const) &SubRow::binRows)
        ;
    pybind11::bind_vector<std::vector<SubRow> >(m, "VectorSubRow");

    pybind11::class_<BinRow, Box<Object::coordinate_type>> (m, "BinRow")
        .def(pybind11::init<>())
        .def("index1D", &BinRow::index1D)
        .def("binId", &BinRow::binId)
        .def("subRowId", &BinRow::subRowId)
        ;
    pybind11::bind_vector<std::vector<BinRow> >(m, "VectorBinRow");

    // Region.h
    pybind11::class_<Region, Object> (m, "Region")
        .def(pybind11::init<>())
        .def("name", &Region::name)
        .def("boxes", (std::vector<Region::box_type> const& (Region::*)() const) &Region::boxes)
        .def("type", &Region::type)
        ;
    pybind11::bind_vector<std::vector<Region> >(m, "VectorRegion");

    // Group.h
    pybind11::class_<Group, Object> (m, "Group")
        .def(pybind11::init<>())
        .def("name", &Group::name)
        .def("nodeNames", (std::vector<std::string> const& (Group::*)() const) &Group::nodeNames)
        .def("nodes", (std::vector<Group::index_type> const& (Group::*)() const) &Group::nodes)
        .def("region", &Group::region)
        ;
    pybind11::bind_vector<std::vector<Group> >(m, "VectorGroup");

    // PlaceDB.h
    pybind11::class_<PlaceDB> (m, "PlaceDB")
        .def(pybind11::init<>())
        .def("nodes", (std::vector<Node> const& (PlaceDB::*)() const) &PlaceDB::nodes)
        .def("node", (Node const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::node)
        .def("nodeProperty", (NodeProperty const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::nodeProperty)
        .def("nodeProperty", (NodeProperty const& (PlaceDB::*)(Node const&) const) &PlaceDB::nodeProperty)
        .def("setNodeStatus", (Node const& (PlaceDB::*)(PlaceDB::index_type, PlaceStatusEnum::PlaceStatusType)) &PlaceDB::setNodeStatus)
        .def("setNodeMultiRowAttr", (Node const& (PlaceDB::*)(PlaceDB::index_type, MultiRowAttrEnum::MultiRowAttrType)) &PlaceDB::setNodeMultiRowAttr)
        .def("setNodeOrient", (Node const& (PlaceDB::*)(PlaceDB::index_type, OrientEnum::OrientType s)) &PlaceDB::setNodeOrient)
        .def("nets", (std::vector<Net> const& (PlaceDB::*)() const) &PlaceDB::nets)
        .def("net", (Net const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::net)
        .def("netProperty", (NetProperty const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::netProperty)
        .def("netProperty", (NetProperty const& (PlaceDB::*)(Net const&) const) &PlaceDB::netProperty)
        .def("setNetWeight", (Net const& (PlaceDB::*)(PlaceDB::index_type, Net::weight_type)) &PlaceDB::setNetWeight)
        .def("pins", (std::vector<Pin> const& (PlaceDB::*)() const) &PlaceDB::pins)
        .def("pin", (Pin const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::pin)
        .def("macros", (std::vector<Macro> const& (PlaceDB::*)() const) &PlaceDB::macros)
        .def("macro", (Macro const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::macro)
        .def("rows", (std::vector<Row> const& (PlaceDB::*)() const) &PlaceDB::rows)
        .def("row", (Row const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::row)
        .def("site", &PlaceDB::site)
        .def("siteArea", &PlaceDB::siteArea)
        .def("dieArea", &PlaceDB::dieArea)
        .def("macroName2Index", (PlaceDB::string2index_map_type const& (PlaceDB::*)() const) &PlaceDB::macroName2Index)
        .def("nodeName2Index", (PlaceDB::string2index_map_type const& (PlaceDB::*)() const) &PlaceDB::nodeName2Index)
        .def("numMovable", &PlaceDB::numMovable)
        .def("numFixed", &PlaceDB::numFixed)
        .def("numMacro", &PlaceDB::numMacro)
        .def("numIOPin", &PlaceDB::numIOPin)
        .def("numPlaceBlockages", &PlaceDB::numPlaceBlockages)
        .def("numIgnoredNet", &PlaceDB::numIgnoredNet)
        .def("movableNodeIndices", (std::vector<PlaceDB::index_type> const& (PlaceDB::*)() const) &PlaceDB::movableNodeIndices)
        .def("fixedNodeIndices", (std::vector<PlaceDB::index_type> const& (PlaceDB::*)() const) &PlaceDB::fixedNodeIndices)
        .def("placeBlockageIndices", (std::vector<PlaceDB::index_type> const& (PlaceDB::*)() const) &PlaceDB::placeBlockageIndices)
        .def("regions", (std::vector<Region> const& (PlaceDB::*)() const) &PlaceDB::regions)
        .def("region", (Region const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::region)
        .def("groups", (std::vector<Group> const& (PlaceDB::*)() const) &PlaceDB::groups)
        .def("group", (Group const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::group)
        .def("lefUnit", &PlaceDB::lefUnit)
        .def("lefVersion", &PlaceDB::lefVersion)
        .def("defUnit", &PlaceDB::defUnit)
        .def("defVersion", &PlaceDB::defVersion)
        .def("designName", &PlaceDB::designName)
        .def("userParam", (UserParam const& (PlaceDB::*)() const) &PlaceDB::userParam)
        .def("benchMetrics", (BenchMetrics const& (PlaceDB::*)() const) &PlaceDB::benchMetrics)
        .def("getNode", (Node const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::getNode)
        .def("getNode", (Node const& (PlaceDB::*)(Pin const&) const) &PlaceDB::getNode)
        .def("getNet", (Net const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::getNet)
        .def("getNet", (Net const& (PlaceDB::*)(Pin const&) const) &PlaceDB::getNet)
        .def("pinPos", (PlaceDB::coordinate_type (PlaceDB::*)(PlaceDB::index_type, Direction1DType) const) &PlaceDB::pinPos)
        .def("pinPos", (PlaceDB::coordinate_type (PlaceDB::*)(Pin const&, Direction1DType) const) &PlaceDB::pinPos)
        .def("pinBbox", (Box<Object::coordinate_type> (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::pinBbox)
        .def("pinBbox", (Box<Object::coordinate_type> (PlaceDB::*)(Pin const&) const) &PlaceDB::pinBbox)
        .def("macroPin", (MacroPin const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::macroPin)
        .def("macroPin", (MacroPin const& (PlaceDB::*)(Pin const&) const) &PlaceDB::macroPin)
        .def("nodeName", (std::string const& (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::nodeName)
        .def("nodeName", (std::string const& (PlaceDB::*)(Node const&) const) &PlaceDB::nodeName)
        .def("macroId", (PlaceDB::index_type (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::macroId)
        .def("macroId", (PlaceDB::index_type (PlaceDB::*)(Node const&) const) &PlaceDB::macroId)
        .def("netName", (std::string const& (PlaceDB::*)(Net const&) const) &PlaceDB::netName)
        .def("macroName", (std::string const& (PlaceDB::*)(Node const&) const) &PlaceDB::macroName)
        .def("macroObs", (MacroObs const& (PlaceDB::*)(Node const&) const) &PlaceDB::macroObs)
        .def("xl", &PlaceDB::xl)
        .def("yl", &PlaceDB::yl)
        .def("xh", &PlaceDB::xh)
        .def("yh", &PlaceDB::yh)
        .def("width", &PlaceDB::width)
        .def("height", &PlaceDB::height)
        .def("rowHeight", &PlaceDB::rowHeight)
        .def("rowBbox", &PlaceDB::rowBbox)
        .def("rowXL", &PlaceDB::rowXL)
        .def("rowYL", &PlaceDB::rowYL)
        .def("rowXH", &PlaceDB::rowXH)
        .def("rowYH", &PlaceDB::rowYH)
        .def("siteWidth", &PlaceDB::siteWidth)
        .def("siteHeight", &PlaceDB::siteHeight)
        .def("maxDisplace", &PlaceDB::maxDisplace)
        .def("minMovableNodeWidth", &PlaceDB::minMovableNodeWidth)
        .def("maxMovableNodeWidth", &PlaceDB::maxMovableNodeWidth)
        .def("avgMovableNodeWidth", &PlaceDB::avgMovableNodeWidth)
        .def("totalMovableNodeArea", &PlaceDB::totalMovableNodeArea)
        .def("totalFixedNodeArea", &PlaceDB::totalFixedNodeArea)
        .def("totalRowArea", &PlaceDB::totalRowArea)
        .def("computeMovableUtil", &PlaceDB::computeMovableUtil)
        .def("computePinUtil", &PlaceDB::computePinUtil)
        .def("numMultiRowMovable", &PlaceDB::numMultiRowMovable)
        .def("numKRowMovable", &PlaceDB::numKRowMovable)
        .def("isMultiRowMovable", (bool (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::isMultiRowMovable)
        .def("isMultiRowMovable", (bool (PlaceDB::*)(Node const&) const) &PlaceDB::isMultiRowMovable)
        .def("isIgnoredNet", (bool (PlaceDB::*)(PlaceDB::index_type) const) &PlaceDB::isIgnoredNet)
        .def("isIgnoredNet", (bool (PlaceDB::*)(Net const&) const) &PlaceDB::isIgnoredNet)
        .def("netIgnoreFlag", &PlaceDB::netIgnoreFlag)
        .def("numRoutingGrids", (PlaceDB::index_type (PlaceDB::*)(Direction1DType) const) &PlaceDB::numRoutingGrids)
        .def("numRoutingLayers", &PlaceDB::numRoutingLayers)
        .def("routingGridOrigin", (PlaceDB::coordinate_type (PlaceDB::*)(Direction1DType) const) &PlaceDB::routingGridOrigin)
        .def("routingTileSize", (PlaceDB::coordinate_type (PlaceDB::*)(Direction1DType) const) &PlaceDB::routingTileSize)
        .def("routingBlockagePorosity", &PlaceDB::routingBlockagePorosity)
        .def("numRoutingTracks", (PlaceDB::index_type (PlaceDB::*)(Direction1DType, PlaceDB::index_type) const) &PlaceDB::numRoutingTracks)
        .def("adjustParams", &PlaceDB::adjustParams)
        ;
}
#endif