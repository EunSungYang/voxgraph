//
// Created by victor on 04.12.18.
//

#include "voxgraph/tools/submap_registration_helper.h"
#include <voxblox/interpolator/interpolator.h>
#include <utility>
#include "voxgraph/backend/constraint/cost_functions/submap_registration/explicit_implicit_registration_cost.h"
#include "voxgraph/backend/constraint/cost_functions/submap_registration/implicit_implicit_registration_cost.h"

namespace voxgraph {
SubmapRegistrationHelper::SubmapRegistrationHelper(
    cblox::SubmapCollection<VoxgraphSubmap>::ConstPtr submap_collection_ptr,
    const Options &options)
    : submap_collection_ptr_(std::move(submap_collection_ptr)),
      options_(options) {}

bool SubmapRegistrationHelper::testRegistration(
    const cblox::SubmapID &reference_submap_id,
    const cblox::SubmapID &reading_submap_id, double *world_pose_reading,
    ceres::Solver::Summary *summary) {
  // Get shared pointers to the reference and reading submaps
  VoxgraphSubmap::ConstPtr reference_submap_ptr =
      submap_collection_ptr_->getSubMapConstPtrById(reference_submap_id);
  VoxgraphSubmap::ConstPtr reading_submap_ptr =
      submap_collection_ptr_->getSubMapConstPtrById(reading_submap_id);
  CHECK_NOTNULL(reference_submap_ptr);
  CHECK_NOTNULL(reading_submap_ptr);

  // Create problem and initial conditions
  ceres::Problem problem;
  ceres::LossFunction *loss_function = nullptr;

  // Get initial pose of reference submap (not touched by the optimization)
  voxblox::Transformation::Vector6 T_vec_ref =
      reference_submap_ptr->getPose().log();
  double world_pose_ref[4] = {T_vec_ref[0], T_vec_ref[1], T_vec_ref[2],
                              T_vec_ref[5]};

  // Add the parameter blocks to the optimization
  problem.AddParameterBlock(world_pose_ref, 4);
  problem.SetParameterBlockConstant(world_pose_ref);
  problem.AddParameterBlock(world_pose_reading, 4);

  // Create and add submap alignment cost function
  RegistrationCost *registration_cost_function;
  if (options_.registration.registration_method ==
      RegistrationCost::RegistrationMethod::kImplicitToImplicit) {
    registration_cost_function = new ImplicitImplicitRegistrationCost(
        reference_submap_ptr, reading_submap_ptr, options_.registration);
  } else {
    registration_cost_function = new ExplicitImplicitRegistrationCost(
        reference_submap_ptr, reading_submap_ptr, options_.registration);
  }

  // Toggle between analytic and numeric Jacobians
  ceres::CostFunction *ceres_cost_function;
  if (options_.registration.jacobian_evaluation_method ==
      RegistrationCost::JacobianEvaluationMethod::kNumeric) {
    // Wrap the registration cost function in a numeric diff cost function,
    // which only requests residuals and calculates the Jacobians numerically
    ceres_cost_function =
        new ceres::NumericDiffCostFunction<RegistrationCost, ceres::CENTRAL,
                                           ceres::DYNAMIC, 4, 4>(
            registration_cost_function, ceres::TAKE_OWNERSHIP,
            registration_cost_function->num_residuals());
  } else {
    // Let Ceres use the registration cost function's analytic Jacobians
    ceres_cost_function = registration_cost_function;
  }

  // Add the cost function to the problem
  problem.AddResidualBlock(ceres_cost_function, loss_function, world_pose_ref,
                           world_pose_reading);

  // Run the solver
  ceres::Solve(options_.solver, &problem, summary);

  return summary->IsSolutionUsable();
}
}  // namespace voxgraph
