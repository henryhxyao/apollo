/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

/*
 * hybrid_a_star.cc
 */

#include "modules/planning/planner/open_space/hybrid_a_star.h"

namespace apollo {
namespace planning {

HybridAStar::HybridAStar() {
  CHECK(common::util::GetProtoFromFile(FLAGS_open_space_config_filename,
                                       &open_space_conf_))
      << "Failed to load open space planner config file "
      << FLAGS_open_space_config_filename;
  reed_shepp_generator_.reset(new ReedShepp(vehicle_param_, open_space_conf_));
  next_node_num_ = open_space_conf_.next_node_num();
  max_steer_ = open_space_conf_.max_steering();
  step_size_ = open_space_conf_.step_size();
  xy_grid_resolution_ = open_space_conf_.xy_grid_resolution();
}

bool HybridAStar::AnalyticExpansion(std::shared_ptr<Node3d> current_node,
                                    ReedSheppPath* reeds_shepp_to_end) {
  if (!reed_shepp_generator_->ShortestRSP(current_node, end_node_,
                                          reeds_shepp_to_end)) {
    AINFO << "ShortestRSP failed";
    return false;
  }
  if (!RSPCheck(reeds_shepp_to_end)) {
    return false;
  }
  return true;
}

bool HybridAStar::RSPCheck(const ReedSheppPath* reeds_shepp_to_end) {
  for (std::size_t i = 0; i < reeds_shepp_to_end->x.size(); i++) {
    Node3d node(reeds_shepp_to_end->x[i], reeds_shepp_to_end->y[i],
                reeds_shepp_to_end->phi[i], open_space_conf_);
    if (!ValidityCheck(node)) {
      return false;
    }
  }
  return true;
}

bool HybridAStar::ValidityCheck(Node3d& node) {
  for (const auto& obstacle_box : obstacles_) {
    if (node.GetBoundingBox(vehicle_param_)
            .HasOverlap((*obstacle_box).PerceptionBoundingBox())) {
      return false;
    }
  }
  return true;
}

void HybridAStar::LoadRSPinCS(const ReedSheppPath* reeds_shepp_to_end,
                              std::shared_ptr<Node3d> current_node) {
  std::shared_ptr<Node3d> end_node = std::shared_ptr<Node3d>(new Node3d(
      reeds_shepp_to_end->x.back(), reeds_shepp_to_end->y.back(),
      reeds_shepp_to_end->phi.back(), reeds_shepp_to_end->x,
      reeds_shepp_to_end->y, reeds_shepp_to_end->phi, open_space_conf_));
  end_node->SetPre(current_node);
  close_set_.insert(std::make_pair(end_node->GetIndex(), end_node));
}

std::shared_ptr<Node3d> HybridAStar::Next_node_generator(
    std::shared_ptr<Node3d> current_node, std::size_t next_node_index) {
  double steering = 0.0;
  double index = 0.0;
  double traveled_distance = 0.0;
  if (next_node_index > next_node_num_ / 2 - 1) {
    steering = -max_steer_ +
               (2 * max_steer_ / (next_node_num_ / 2 - 1)) * next_node_index;
    traveled_distance = 1 * step_size_;
  } else {
    index = next_node_index - next_node_num_ / 2;
    steering =
        -max_steer_ + (2 * max_steer_ / (next_node_num_ / 2 - 1)) * index;
    traveled_distance = -1 * step_size_;
  }
  // take above motion primitive to generate a curve driving the car to a
  // different grid
  double arc = std::sqrt(2) * xy_grid_resolution_;
  std::vector<double> intermediate_x;
  std::vector<double> intermediate_y;
  std::vector<double> intermediate_phi;
  double last_x = current_node->GetX();
  double last_y = current_node->GetY();
  double last_phi = current_node->GetPhi();
  intermediate_x.emplace_back(last_x);
  intermediate_y.emplace_back(last_y);
  intermediate_phi.emplace_back(last_phi);
  for (std::size_t i = 0; i < arc / step_size_; i++) {
    double next_x = last_x + traveled_distance * std::cos(last_phi);
    double next_y = last_y + traveled_distance * std::sin(last_phi);
    double next_phi = common::math::NormalizeAngle(
        last_phi +
        traveled_distance / vehicle_param_.wheel_base() * std::tan(steering));
    intermediate_x.emplace_back(next_x);
    intermediate_y.emplace_back(next_y);
    intermediate_phi.emplace_back(next_phi);
    last_x = next_x;
    last_y = next_y;
    last_phi = next_phi;
  }
  std::shared_ptr<Node3d> next_node = std::shared_ptr<Node3d>(
      new Node3d(last_x, last_y, last_phi, intermediate_x, intermediate_y,
                 intermediate_phi, open_space_conf_));
  next_node->SetPre(current_node);
  next_node->SetDirec(traveled_distance>0);
  return next_node;
}

bool CalculateCost(std::shared_ptr<Node3d> current_node, std::shared_ptr<Node3d> next_node) {
  //evaluate cost on the trajectory and add current cost
  //evaluate heuristic cost
  return true;
}

double NonHoloNoObstacleHeuristic() {
  return 0.0;
}


bool HybridAStar::Plan(double sx, double sy, double sphi, double ex, double ey,
                       double ephi, std::vector<const Obstacle*> obstacles) {
  // load nodes and obstacles
  std::vector<double> sx_vec{sx};
  std::vector<double> sy_vec{sy};
  std::vector<double> sphi_vec{sphi};
  std::vector<double> ex_vec{ex};
  std::vector<double> ey_vec{ey};
  std::vector<double> ephi_vec{ephi};
  start_node_.reset(
      new Node3d(sx, sy, sphi, sx_vec, sy_vec, sphi_vec, open_space_conf_));
  end_node_.reset(
      new Node3d(ex, ey, ephi, ex_vec, ey_vec, ephi_vec, open_space_conf_));
  obstacles_ = obstacles;
  // load open set and priority queue
  open_set_.insert(std::make_pair(start_node_->GetIndex(), start_node_));
  open_pq_.push(
      std::make_pair(start_node_->GetIndex(), start_node_->GetCost()));
  // Hybrid A* begins
  while (!open_pq_.empty()) {
    // take out the lowest cost neighoring node
    std::size_t current_id = open_pq_.top().first;
    open_pq_.pop();
    std::shared_ptr<Node3d> current_node = open_set_[current_id];
    // check if a analystic curve could be connected from current configuration
    // to the end configuration without collision. if so, search ends.
    ReedSheppPath reeds_shepp_to_end;
    if (AnalyticExpansion(current_node, &reeds_shepp_to_end)) {
      AINFO << "Reach the end configuration with Reed Sharp";
      // load the whole RSP as nodes and add to the close set
      LoadRSPinCS(&reeds_shepp_to_end, current_node);
      break;
    }
    for (std::size_t i = 0; i < next_node_num_; i++) {
      std::shared_ptr<Node3d> next_node = Next_node_generator(current_node, i);
      // boundary and validity check
      if (!ValidityCheck(*next_node)) {
        continue;
      }
      // check if the node is already in the close set
      if (close_set_.find(next_node->GetIndex()) != close_set_.end()) {
        continue;
      }

      if (open_set_.find(next_node->GetIndex()) == open_set_.end()) {
        // TODO: only calculate cost here

        open_set_.insert(std::make_pair(next_node->GetIndex(), next_node));
        open_pq_.push(
            std::make_pair(next_node->GetIndex(), next_node->GetCost()));
      } else {
        // reinitial the cost for rewiring
      }
    }
  }
  result_ = GetResult();
  return true;
}
}  // namespace planning
}  // namespace apollo