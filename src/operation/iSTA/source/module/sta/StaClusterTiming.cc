// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file StaClusterTiming.cc
 * @author shy long (longshy@pcl.ac.cn)
 * @brief The class for macro place cluster timing analysis.
 * @version 0.1
 * @date 2024-05-31
 */

#include "StaClusterTiming.hh"

namespace ista {

/**
 * @brief according to the clusters to add hierarchical sub netlists.
 *
 */
void StaClusterTiming::addHierSubNetlist() {
  auto* design_netlist = getSta()->get_netlist();
  std::vector<Netlist*> hier_sub_netlists;
  int cluster_index = 1;
  for (auto& cluster_instance : _cluster_instances) {
    if (cluster_instance.size() == 1) {
      continue;
    }
    // _netlist.set_name(design_name);
    auto* sub_netlist = new Netlist();
    std::string design_name = "cluster" + std::to_string(cluster_index);
    sub_netlist->set_name(design_name.c_str());
    for (auto& instance_name : cluster_instance) {
      auto* instance = design_netlist->findInstance(instance_name.c_str());
      Instance new_inst(*instance);
      bool is_boundary_instance =
          isBoundaryInstance(new_inst, cluster_instance, _cluster_instances);
      if (is_boundary_instance) {
        addPortForBoundaryInstance(new_inst, cluster_instance, *sub_netlist);
        // std::string port_virtual_net_name;
        // if (virtual_port.isOutput()) {
        //   const char* sep = "2";
        //   auto [driver_inst, load_inst] =
        //       Str::splitTwoPart(virtual_port.get_name(), sep);
        //   port_virtual_net_name = driver_inst;
        // } else {
        //   const char* sep = "2";
        //   auto [load_inst, driver_inst] =
        //       Str::splitTwoPart(virtual_port.get_name(), sep);
        //   port_virtual_net_name = driver_inst;
        // }
        // std::unique_ptr<Net> port_virtual_net(
        //     findVirtualNet(port_virtual_net_name.c_str()));
        // if (!port_virtual_net) {
        //   port_virtual_net =
        //       std::make_unique<Net>(port_virtual_net_name.c_str());
        //   addVirtualNet(std::move(*port_virtual_net.get()));
        // }
        // port_virtual_net->addPinPort(&virtual_port);

        // for (auto net : port_net_collect.get_nets()) {
        //   sub_netlist->addNet(std::move(net));
        // }

        // for (auto& port : port_net_collect.get_ports()) {
        //   sub_netlist->addPort(std::move(port));
        // }
        sub_netlist->addInstance(std::move(new_inst));
      } else {
        addPortForSubnetlist(new_inst, *sub_netlist);
        sub_netlist->addInstance(std::move(new_inst));
      }
    }
    hier_sub_netlists.emplace_back(sub_netlist);
    cluster_index++;
  }

  for (auto& cluster_instance : _cluster_instances) {
    if (cluster_instance.size() == 1) {
      for (auto& instance_name : cluster_instance) {
        auto* instance = design_netlist->findInstance(instance_name.c_str());
        Instance new_inst = (*instance).cloneInstance();
        addRemainingInstances(std::move(new_inst));
      }
    }
  }

  design_netlist->set_hier_sub_netlists(hier_sub_netlists);
}

/**
 * @brief add port for the subnetlist.
 *
 * @param inst
 * @param subnetlist
 */
void StaClusterTiming::addPortForSubnetlist(Instance& inst,
                                            Netlist& subnetlist) {
  for (auto& pin : inst.get_pins()) {
    Net* connect_net = pin->get_net();
    LOG_FATAL_IF(!connect_net)
        << "pin " << pin->getFullName() << " connect net is not exist";
    auto& pin_ports = connect_net->get_pin_ports();
    for (auto& pin_port : pin_ports) {
      if (pin_port->isPort()) {
        // Port* pin_port1 = dynamic_cast<Port*>(pin_port);
        // subnetlist.addPort(std::move(*pin_port1));
        Port* pin_port1 = dynamic_cast<Port*>(pin_port);
        Port new_port = Port(*pin_port1);
        subnetlist.addPort(std::move(new_port));
      }
    }
  }
}

/**
 * @brief judge whether a instance in a cluster is connected to another instance
 * in another cluster.
 *
 * @param inst
 * @param instance_own_cluster
 * @param cluster_instances
 * @return true
 * @return false
 */
bool StaClusterTiming::isBoundaryInstance(
    Instance& inst, std::set<std::string> instance_own_cluster,
    const std::vector<std::set<std::string>>& cluster_instances) {
  bool is_boundary_instance = false;

  for (auto& pin : inst.get_pins()) {
    Net* connect_net = pin->get_net();
    LOG_FATAL_IF(!connect_net)
        << "pin " << pin->getFullName() << " connect net is not exist";
    auto& pin_ports = connect_net->get_pin_ports();
    for (auto& pin_port : pin_ports) {
      if (pin_port->isPort() ||
          (pin_port->isPin() && dynamic_cast<Pin*>(pin_port)->getFullName() ==
                                    pin.get()->getFullName())) {
        continue;
      }
      std::string own_instance_name =
          pin_port->get_own_instance()->getFullName();
      if (!instance_own_cluster.contains(own_instance_name)) {
        is_boundary_instance = true;
        break;
      }
    }
    if (is_boundary_instance) {
      break;
    }
  }

  return is_boundary_instance;
}

/**
 * @brief add the virtual port for the boundary instance.
 *
 * @param boundary_inst
 * @param boundary_inst_own_cluster
 * @param subnetlist
 */
void StaClusterTiming::addPortForBoundaryInstance(
    Instance& boundary_inst, std::set<std::string> boundary_inst_own_cluster,
    Netlist& subnetlist) {
  // if (Str::equal(boundary_inst.get_name(), "u3") == true) {
  //   for (auto& pin : boundary_inst.get_pins()) {
  //     auto the_found_pin = boundary_inst.getPin(pin->get_name());
  //     LOG_INFO << pin << pin.get() << ":" << pin->get_name();
  //   }
  // }
  // net with multi-load pin shouold build one port.
  for (auto& pin : boundary_inst.get_pins()) {
    Net* connect_net = pin->get_net();
    LOG_FATAL_IF(!connect_net)
        << "pin " << pin->getFullName() << " connect net is not exist";
    auto& pin_ports = connect_net->get_pin_ports();

    std::list<DesignObject*> boundary_inst_other_pins;
    Port virtual_port;
    Net virtual_net;
    Port* virtual_port_ptr = nullptr;
    Net* virtual_net_ptr = nullptr;
    if (pin->isInput()) {
      for (auto& pin_port : pin_ports) {
        if (pin_port->isPort()) {
          Port* pin_port1 = dynamic_cast<Port*>(pin_port);
          Port new_port = Port(*pin_port1);
          auto* found_port = subnetlist.findPort(pin_port->get_name());
          if (!found_port) {
            subnetlist.addPort(std::move(new_port));
          }
          continue;
        }

        if (dynamic_cast<Pin*>(pin_port)->getFullName() ==
            pin.get()->getFullName()) {
          continue;
        }

        // collect other load pins in a instance.(fit multi_load_pin net).
        std::string own_instance_name =
            pin_port->get_own_instance()->getFullName();
        if (pin_port->isPin() && pin_port->isInput() &&
            Str::equal(boundary_inst.get_name(), own_instance_name.c_str()) ==
                true) {
          auto the_found_pin = boundary_inst.getPin(pin_port->get_name());
          LOG_FATAL_IF(!the_found_pin)
              << "Unable to find the load pin in instance.";
          boundary_inst_other_pins.emplace_back(*the_found_pin);
        }

        // build the virtual port and virtual net between the net's dirver pin
        // and the net's load pin.
        if (pin_port->isPin() && pin_port->isOutput() &&
            !boundary_inst_own_cluster.contains(own_instance_name)) {
          // flag the instance'pin to connect to the virtual net.
          dynamic_cast<Pin*>(pin_port)->set_net_name_between_clusters(
              own_instance_name.c_str());
          // need virtual port on the connect net.
          PortDir dcl_type = PortDir::kIn;
          std::string dcl_name =
              boundary_inst.getFullName() + "2" + own_instance_name;
          virtual_port = Port(dcl_name.c_str(), dcl_type);
          virtual_net = Net(connect_net->get_name());
          subnetlist.addNet(std::move(virtual_net));
          subnetlist.addPort(std::move(virtual_port));
          virtual_net_ptr = subnetlist.findNet(connect_net->get_name());
          virtual_port_ptr = subnetlist.findPort(dcl_name.c_str());
          LOG_FATAL_IF(!virtual_net_ptr || !virtual_port_ptr);
          virtual_net_ptr->addPinPort(virtual_port_ptr);
          virtual_port_ptr->set_net(virtual_net_ptr);
        }
      }
    } else if (pin->isOutput()) {
      bool first = true;
      for (auto& pin_port : pin_ports) {
        if (pin_port->isPort()) {
          Port* pin_port1 = dynamic_cast<Port*>(pin_port);
          Port new_port = Port(*pin_port1);
          subnetlist.addPort(std::move(new_port));
          continue;
        }

        if (dynamic_cast<Pin*>(pin_port)->getFullName() ==
            pin.get()->getFullName()) {
          continue;
        }

        std::string own_instance_name =
            pin_port->get_own_instance()->getFullName();

        if (!boundary_inst_own_cluster.contains(own_instance_name)) {
          // flag the instance'pin to connect to the virtual net.
          dynamic_cast<Pin*>(pin_port)->set_net_name_between_clusters(
              boundary_inst.get_name());
          // need virtual port on the connect net.
          if (first) {
            PortDir dcl_type = PortDir::kOut;
            std::string dcl_name =
                boundary_inst.getFullName() + "2" + own_instance_name;
            virtual_port = Port(dcl_name.c_str(), dcl_type);
            virtual_net = Net(connect_net->get_name());
            subnetlist.addNet(std::move(virtual_net));
            subnetlist.addPort(std::move(virtual_port));
            virtual_net_ptr = subnetlist.findNet(connect_net->get_name());
            virtual_port_ptr = subnetlist.findPort(dcl_name.c_str());
            LOG_FATAL_IF(!virtual_net_ptr || !virtual_port_ptr);
            virtual_net_ptr->addPinPort(virtual_port_ptr);
            virtual_port_ptr->set_net(virtual_net_ptr);
          }
          first = false;
        }
      }
    }

    if (virtual_net_ptr) {
      virtual_net_ptr->addPinPort(pin.get());
      pin->set_net(virtual_net_ptr);
      if (!boundary_inst_other_pins.empty()) {
        for (const auto& boundary_inst_other_pin : boundary_inst_other_pins) {
          virtual_net_ptr->addPinPort(boundary_inst_other_pin);
          dynamic_cast<Pin*>(boundary_inst_other_pin)->set_net(virtual_net_ptr);
        }
      }

    } else {
      Net* the_net = subnetlist.findNet(connect_net->get_name());
      if (!the_net) {
        Net new_net = Net(*connect_net);
        subnetlist.addNet(std::move(new_net));
      }
    }
  }
}

void StaClusterTiming ::buildSubnetlistToInst() {
  auto* design_netlist = getSta()->get_netlist();
  design_netlist->reset();
  auto hier_sub_netlists = design_netlist->get_hier_sub_netlists();  // need & ?
  for (const auto& hier_sub_netlist : hier_sub_netlists) {
    const char* liberty_cell_name = hier_sub_netlist->get_name();
    auto* inst_cell = getSta()->findLibertyCell(liberty_cell_name);
    // inst_name is same with liberty_cell_name.
    const char* inst_name = liberty_cell_name;
    Instance inst(inst_name, inst_cell);
    Port* port;
    FOREACH_PORT(hier_sub_netlist, port) {
      const char* port_name = port->get_name();
      // consider hier_subnetlist's port is LibertyPort,not consider
      // hier_subnetlist's port is LibertyPortBus. as the current ETM generated
      // model does not include the pin is LibertyPortBus.
      auto* library_port_or_port_bus =
          inst_cell->get_cell_port_or_port_bus(port_name);

      if (!library_port_or_port_bus->isLibertyPortBus()) {
        LibertyPort* library_port =
            dynamic_cast<LibertyPort*>(library_port_or_port_bus);
        std::string pin_name = port_name;
        auto* inst_pin = inst.addPin(pin_name.c_str(), library_port);

        // addNet
        if (strchr(port_name, '2') != nullptr) {
          auto obtain_net_name = [&port]() -> const char* {
            const char* net_name;
            if (port->isOutput()) {
              const char* sep = "2";
              auto [driver_inst, load_inst] =
                  Str::splitTwoPart(port->get_name(), sep);
              net_name = driver_inst.c_str();
            } else if (port->isInput()) {
              const char* sep = "2";
              auto [load_inst, driver_inst] =
                  Str::splitTwoPart(port->get_name(), sep);
              net_name = driver_inst.c_str();
              ;
            }
            return net_name;
          };

          const char* net_name = obtain_net_name();
          Net* the_net = design_netlist->findNet(net_name);
          if (the_net) {
            the_net->addPinPort(inst_pin);
          } else {
            // DLOG_INFO << "create net " << net_name;
            auto& created_net = design_netlist->addNet(Net(net_name));

            created_net.addPinPort(inst_pin);
            the_net = &created_net;
          }

        } else {
          // port_name without "2" represents the port is not virtual port.
          // TODO: Applicable to nets with only one input port or one output
          // port, not applicable to nets with two output ports.
          const char* net_name = port_name;
          Net* the_net = design_netlist->findNet(net_name);
          if (the_net) {
            the_net->addPinPort(inst_pin);
          } else {
            // DLOG_INFO << "create net " << net_name;
            auto& created_net = design_netlist->addNet(Net(net_name));

            created_net.addPinPort(inst_pin);
            the_net = &created_net;
          }
          PortDir port_dir = port->get_port_dir();
          auto& created_port =
              design_netlist->addPort(Port(port_name, port_dir));
          the_net->addPinPort(&created_port);
        }

      } else {
        LOG_INFO_FIRST_N(5)
            << "the ETM generated timing modle has LibertyPotBus.";
      }
    }

    // when cluster only have one instance.
    for (auto& remaining_instance : _remaining_instances) {
      for (auto& pin : remaining_instance.get_pins()) {
        if (Str::equal(pin->get_net_name_between_clusters(), "") != true) {
          const char* net_name_between_clusters =
              pin->get_net_name_between_clusters();
          Net* net_between_clusters =
              design_netlist->findNet(net_name_between_clusters);
          LOG_FATAL_IF(!net_between_clusters);
          pin->set_net(net_between_clusters);
          net_between_clusters->addPinPort(pin.get());

        } else {
          auto* connect_net = pin->get_net();
          Net* the_net = design_netlist->findNet(connect_net->get_name());
          if (!the_net) {
            Net& ret_net = design_netlist->addNet(connect_net);
            connect_net = &ret_net;
            pin->set_net(connect_net);
          }
          for (auto& port : connect_net->get_ports()) {
            auto* the_port = design_netlist->findPort(port->get_name());
            if (!the_port) {
              // transfer ownership of port to design_netlist and get the
              // reference of ret_port.
              Port& ret_port = design_netlist->addPort(std::move(port));
              // Reset the port pointer in connect_net to the new pointer
              // "&ret_port"
              port.reset(&ret_port);
            }
          }
        }
      }
      // check std::move().
      design_netlist->addInstance(std::move(remaining_instance));
    }
  }
}

}  // namespace ista