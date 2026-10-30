#pragma once
#include "part_config.hpp"
#include "physics_body.hpp"

struct PartAlignedPose {
  physics::Vec3 position;
  physics::Mat34 rotation;
};

inline PartAlignedPose alignPartSlots(const physics::State& target, const PartSlotState& targetSlot,
                                    const PartSlotState& partSlot) {
  auto matrix = [](const std::array<float, 9>& a) -> physics::Mat34 {
    return {a[0],a[1],a[2],0,a[3],a[4],a[5],0,a[6],a[7],a[8],0};
  };
  const auto targetRotation = physics::matrixMultiply(target.rot, matrix(targetSlot.rotation));
  const auto rotation = physics::matrixMultiply(targetRotation, physics::matrixTranspose(matrix(partSlot.rotation)));
  const auto slotPosition = physics::transformPoint(target.pos, target.rot, targetSlot.position);
  const auto offset = physics::matrixTransformRows(rotation, partSlot.position);
  return {{slotPosition[0] - offset[0], slotPosition[1] - offset[1], slotPosition[2] - offset[2]}, rotation};
}
