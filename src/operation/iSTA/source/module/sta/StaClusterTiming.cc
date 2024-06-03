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

void StaClusterTiming::addHierSubNetlist() {
  auto* design_netlist = getSta()->get_netlist();
  std::vector<Netlist*> hier_sub_netlists;
  for (auto& cluster_instance : _cluster_instances) {
    // TODO:cluster only have one port:continue
    if (cluster_instance.size() == 1) {
      continue;
    }
    // _netlist.set_name(design_name);
    auto* sub_netlist = new Netlist();
    for (auto& instance_name : cluster_instance) {
      auto* instance = design_netlist->findInstance(instance_name.c_str());
      Instance new_inst(*instance);
      bool is_boundary_instance =
          isBoundaryInstance(new_inst, cluster_instance, _cluster_instances);
      if (is_boundary_instance) {
        // addPortForBoundaryInstance is fit for multiple_pin net??
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
        sub_netlist->addInstance(std::move(new_inst));
      }
    }
    hier_sub_netlists.emplace_back(sub_netlist);
  }

  design_netlist->set_hier_sub_netlists(hier_sub_netlists);
}

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
      // TODO:remove the following code
      Instance* own_instance;
      if (pin_port->isPin()) {
        own_instance = pin_port->get_own_instance();
      }

      //||(pin_port->isPin() &&
      // own_instance->isOneInOwnCluster(cluster_instances))
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

void StaClusterTiming::addPortForBoundaryInstance(
    Instance& boundary_inst, std::set<std::string> boundary_inst_own_cluster,
    Netlist& subnetlist) {
  // net with multi-load pin shouold build one port.
  for (auto& pin : boundary_inst.get_pins()) {
    Net* connect_net = pin->get_net();

    LOG_FATAL_IF(!connect_net)
        << "pin " << pin->getFullName() << " connect net is not exist";
    auto& pin_ports = connect_net->get_pin_ports();

    if (pin->isInput()) {
      Port virtual_port;
      Net virtual_net;

      for (auto& pin_port : pin_ports) {
        if (pin_port->isPort() || dynamic_cast<Pin*>(pin_port)->getFullName() ==
                                      pin.get()->getFullName()) {
          continue;
        }

        std::string own_instance_name =
            pin_port->get_own_instance()->getFullName();
        if (!boundary_inst_own_cluster.contains(own_instance_name)) {
          // need virtual port on the connect net.
          PortDir dcl_type = PortDir::kIn;
          std::string dcl_name =
              boundary_inst.getFullName() + "2" + own_instance_name;
          virtual_port = Port(dcl_name.c_str(), dcl_type);

          virtual_net = Net(connect_net->get_name());
          virtual_net.addPinPort(&virtual_port);
          virtual_port.set_net(&virtual_net);
        }

        // multi-load pin in a cluster
        // TODO:multi-load pin not in a cluster
        if ((pin->isInput() && pin_port->isPin() && pin_port->isInput()) &&
            strcmp(virtual_net.get_name(), "") != 0) {
          virtual_net.addPinPort(pin_port);
          pin_port->set_net(&virtual_net);
        }
      }
      if (strcmp(virtual_net.get_name(), "") != 0) {
        virtual_net.addPinPort(pin.get());
        pin->set_net(&virtual_net);
        // own_subnetlist.removeNet(connect_net);?
        subnetlist.addNet(std::move(virtual_net));
        subnetlist.addPort(std::move(virtual_port));
      }
      // get the virtual net's pinport size for debug.
      int pin_ports_size = virtual_net.get_pin_ports().size();
      LOG_INFO << "pin_ports_size:" << pin_ports_size;

    } else if (pin->isOutput()) {
      Port virtual_port;
      Net virtual_net;

      bool first = true;
      for (auto& pin_port : pin_ports) {
        if (pin_port->isPort() || dynamic_cast<Pin*>(pin_port)->getFullName() ==
                                      pin.get()->getFullName()) {
          continue;
        }
        std::string own_instance_name =
            pin_port->get_own_instance()->getFullName();

        if (!boundary_inst_own_cluster.contains(own_instance_name)) {
          // need virtual port on the connect net.
          if (first) {
            PortDir dcl_type = PortDir::kOut;
            std::string dcl_name =
                boundary_inst.getFullName() + "2" + own_instance_name;
            virtual_port = Port(dcl_name.c_str(), dcl_type);
            virtual_net = Net(connect_net->get_name());
            virtual_net.addPinPort(&virtual_port);
            virtual_port.set_net(&virtual_net);
          }
          first = false;
        }
      }

      if (strcmp(virtual_net.get_name(), "") != 0) {
        virtual_net.addPinPort(pin.get());
        pin->set_net(&virtual_net);

        // get the virtual net's pinport size for debug.
        int pin_ports_size = virtual_net.get_pin_ports().size();
        LOG_INFO << "pin_ports_size:" << pin_ports_size;

        // own_subnetlist.removeNet(connect_net);?
        subnetlist.addNet(std::move(virtual_net));
        subnetlist.addPort(std::move(virtual_port));
      }
    }
  }
}

}  // namespace ista