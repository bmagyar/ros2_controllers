// Copyright 2020 PAL Robotics S.L.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "forward_command_controller/forward_command_controller_new_components.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/logging.hpp"

namespace
{
constexpr auto kFCCLoggerName = "forward command controller";
}

namespace forward_command_controller
{
using CallbackReturn = ForwardCommandControllerNewComponents::CallbackReturn;

ForwardCommandControllerNewComponents::ForwardCommandControllerNewComponents()
: controller_interface::ControllerInterfaceNewComponents(),
  joint_handles_(),
  rt_command_ptr_(nullptr),
  joints_command_subscriber_(nullptr),
  logger_name_(kFCCLoggerName)
{}

CallbackReturn ForwardCommandControllerNewComponents::on_configure(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  rclcpp::Parameter joints_param, interface_param;
  if (!lifecycle_node_->get_parameter("joints",
    joints_param))
  {
    RCLCPP_ERROR_STREAM(rclcpp::get_logger(logger_name_), "'joints' parameter not set");
    return CallbackReturn::ERROR;
  }

  // TODO(anyone): here should be list of interface_names and they should be defined for every joint
  if (!lifecycle_node_->get_parameter("interface_name",
    interface_param))
  {
    RCLCPP_ERROR_STREAM(rclcpp::get_logger(logger_name_), "'interface_name' parameter not set");
    return CallbackReturn::ERROR;
  }

  auto joint_names = joints_param.as_string_array();
  if (joint_names.empty()) {
    RCLCPP_ERROR_STREAM(rclcpp::get_logger(logger_name_), "'joints' is empty");
    return CallbackReturn::ERROR;
  }

  // TODO(anyone): In general case we have one or more interface for each joint
  auto interface_name = interface_param.as_string();
  if (interface_name.empty()) {
    RCLCPP_ERROR_STREAM(rclcpp::get_logger(logger_name_), "'interface_name' is empty");
    return CallbackReturn::ERROR;
  }
  // TODO(anyone): a vector should be recived from the parameter server.
  interfaces_.push_back(interface_name);

  if (auto rm_ptr = resource_manager_.lock()) {
    // check all requested joints and interfaces are present
    for (const auto & joint_name : joint_names) {
      if (rm_ptr->check_command_interfaces(joint_name, interfaces_) !=
        hardware_interface::return_type::OK)
      {
        RCLCPP_ERROR_STREAM(
          rclcpp::get_logger(
            logger_name_), "joint '" << joint_name << "' not registered");
        return CallbackReturn::ERROR;
      }
    }

    // get joint handles
    for (const auto & joint_name : joint_names) {
      std::shared_ptr<hardware_interface::components::Joint> joint_handle;
      if (rm_ptr->claim_command_handle(joint_name, interfaces_, joint_handle) !=
        hardware_interface::return_type::OK)
      {
        // uppon error, clear any previously requested handles
        // TODO(all) unclaim handles from the ResourceManager
        joint_handles_.clear();

        RCLCPP_ERROR_STREAM(
          rclcpp::get_logger(
            logger_name_), "could not get handle for joint '" << joint_name << "'");
        return CallbackReturn::ERROR;
      }
      joint_handles_.emplace_back(std::move(joint_handle));
    }
  } else {
    RCLCPP_ERROR_STREAM(
      rclcpp::get_logger(
        logger_name_), "could not lock pointer to resource_manager");
    return CallbackReturn::ERROR;
  }

  joints_command_subscriber_ = lifecycle_node_->create_subscription<CmdType>(
    "commands", rclcpp::SystemDefaultsQoS(),
    [this](const CmdType::SharedPtr msg)
    {
      rt_command_ptr_.writeFromNonRT(msg);
    });

  RCLCPP_INFO_STREAM(
    rclcpp::get_logger(
      logger_name_), "configure successful");
  return CallbackReturn::SUCCESS;
}

CallbackReturn ForwardCommandControllerNewComponents::on_activate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  return CallbackReturn::SUCCESS;
}

CallbackReturn ForwardCommandControllerNewComponents::on_deactivate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  return CallbackReturn::SUCCESS;
}

controller_interface::return_type ForwardCommandControllerNewComponents::update()
{
  auto joint_commands = rt_command_ptr_.readFromRT();

  // no command received yet
  if (!joint_commands || !(*joint_commands)) {
    return controller_interface::return_type::SUCCESS;
  }

  const auto joint_num = (*joint_commands)->data.size();
  if (joint_num != joint_handles_.size()) {
    RCLCPP_ERROR_STREAM_THROTTLE(
      rclcpp::get_logger(
        logger_name_),
      *lifecycle_node_->get_clock(), 1000, "command size does not match number of joints");
    RCLCPP_ERROR_STREAM(
      rclcpp::get_logger(
        logger_name_), "command size does not match number of joints");
    return controller_interface::return_type::ERROR;
  }

  for (auto index = 0ul; index < joint_num; ++index) {
    // TODO(anyone) this is very sub-optimal - but sufficient for proof of concept
    std::vector<double> data;
    data.push_back((*joint_commands)->data[index]);
    joint_handles_[index]->set_command(data, interfaces_);
  }

  return controller_interface::return_type::SUCCESS;
}

}  // namespace forward_command_controller

#include "pluginlib/class_list_macros.hpp"

PLUGINLIB_EXPORT_CLASS(
  forward_command_controller::ForwardCommandControllerNewComponents,
  controller_interface::ControllerInterfaceNewComponents)