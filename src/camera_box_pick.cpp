#include "camera_box_pick.hpp"
#include "physics_box_ray.hpp"
#include <cmath>

namespace {
physics::Vec3 localPoint(const physics::State& pose, const physics::Vec3& point) {
  const physics::Vec3 d{point[0] - pose.pos[0], point[1] - pose.pos[1], point[2] - pose.pos[2]};
  return {d[0] * pose.rot[0] + d[1] * pose.rot[4] + d[2] * pose.rot[8],
          d[0] * pose.rot[1] + d[1] * pose.rot[5] + d[2] * pose.rot[9],
          d[0] * pose.rot[2] + d[1] * pose.rot[6] + d[2] * pose.rot[10]};
}

// FUN_00498580. The returned fraction runs from the second endpoint toward
// the first, as do FUN_00498240 and the final 0049e3d0 hit reconstruction.
std::optional<float> sphereFraction(const physics::Vec3& start, const physics::Vec3& end,
                                    const physics::Vec3& center, float radius) {
  const physics::Vec3 delta{end[0] - start[0], end[1] - start[1], end[2] - start[2]};
  const float length = std::sqrt(delta[0] * delta[0] + delta[2] * delta[2] + delta[1] * delta[1]);
  if (!(length > 0) || !std::isfinite(length))
    return std::nullopt;
  const float inverseLength = 1.0f / length;
  const physics::Vec3 direction{delta[0] * inverseLength, delta[1] * inverseLength, delta[2] * inverseLength};
  const physics::Vec3 offset{center[0] - start[0], center[1] - start[1], center[2] - start[2]};
  const float projection = offset[0] * direction[0] + offset[1] * direction[1] + offset[2] * direction[2];
  if (projection < -radius || length + radius < projection)
    return std::nullopt;
  // Newer 005007c0 groups the squared components in x,y,z order.
  const float perpendicular = (offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]) - projection * projection;
  const float radiusSquared = radius * radius;
  if (radiusSquared < perpendicular ||
      (projection < 0 && radiusSquared < projection * projection + perpendicular) ||
      (length < projection && radiusSquared < (projection - length) * (projection - length) + perpendicular))
    return std::nullopt;
  return std::max(0.0f, ((length - projection) - std::sqrt(radiusSquared - perpendicular)) / length);
}

std::optional<float> triangleFraction(const physics::Shape& shape, const physics::MeshTriangle& triangle,
                                      const physics::Vec3& start, const physics::Vec3& end,
                                      float nearest, std::string& error) {
  for (const auto index : triangle.indices)
    if (index >= shape.vertices.size()) {
      error = "cursor mesh triangle index unavailable";
      return std::nullopt;
    }
  const auto& a = shape.vertices[triangle.indices[0]];
  const auto& b = shape.vertices[triangle.indices[1]];
  const auto& c = shape.vertices[triangle.indices[2]];
  const auto& n = triangle.normal;
  for (size_t i = 0; i < 3; ++i)
    if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || !std::isfinite(c[i]) || !std::isfinite(n[i])) {
      error = "cursor mesh face is nonfinite";
      return std::nullopt;
    }
  // FUN_0049f7b0 is one-sided and rejects the two-endpoint near-plane case;
  // the broader ground contact helper FUN_0049fa60 has different behavior.
  const physics::Vec3 farDelta{end[0] - a[0], end[1] - a[1], end[2] - a[2]};
  const float first = (start[1] - a[1]) * n[1] + (start[0] - a[0]) * n[0] + (start[2] - a[2]) * n[2];
  const float last = farDelta[0] * n[0] + farDelta[1] * n[1] + farDelta[2] * n[2];
  if (!(last > 0) || first > 0)
    return std::nullopt;
  const float fraction = std::fabs(last / (first - last));
  if (nearest < fraction || (std::fabs(first) <= 0.02f && std::fabs(last) <= 0.02f))
    return std::nullopt;
  physics::Vec3 point{};
  for (size_t i = 0; i < 3; ++i)
    point[i] = ((start[i] - a[i]) - farDelta[i]) * fraction + farDelta[i];
  size_t x = 0, y = 1;
  if (std::fabs(n[1]) < std::fabs(n[0])) {
    if (std::fabs(n[2]) <= std::fabs(n[0])) {
      x = 1;
      y = 2;
    }
  } else if (std::fabs(n[2]) <= std::fabs(n[1])) {
    x = 0;
    y = 2;
  }
  const float bx = b[x] - a[x], by = b[y] - a[y];
  const float cx = c[x] - a[x], cy = c[y] - a[y];
  const float determinant = cy * bx - cx * by;
  const float inverse = 1.0f / determinant;
  const float u = (cy * point[x] - cx * point[y]) * inverse;
  const float v = (bx * point[y] - by * point[x]) * inverse;
  if (!(u >= 0 && u <= 1 && v >= 0 && u + v <= 1))
    return std::nullopt;
  return fraction;
}
} // namespace

std::optional<CameraBoxPick> pickCameraShapes(
    const std::array<float, 2>& cursor, const ViewportInstance& viewport,
    const std::array<float, 16>& cameraWorld, const std::array<float, 16>& projection,
    const std::vector<CameraPickShape>& shapes, std::string& error) {
  error.clear();
  if (!std::isfinite(cursor[0]) || !std::isfinite(cursor[1]) ||
      !std::isfinite(viewport.left) || !std::isfinite(viewport.top) ||
      !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
      viewport.width <= 0 || viewport.height <= 0)
    return std::nullopt;
  const float x = (cursor[0] - (2 * viewport.left - 1)) / viewport.width - 1;
  const float y = (cursor[1] - (2 * viewport.top - 1)) / viewport.height - 1;
  if (x < -1 || x > 1 || y < -1 || y > 1)
    return std::nullopt;
  const auto near = unprojectCameraPoint({x, -y, 1}, cameraWorld, projection);
  const auto far = unprojectCameraPoint({x, -y, 0}, cameraWorld, projection);
  if (!near || !far) {
    error = "cursor camera unprojection unavailable";
    return std::nullopt;
  }
  physics::Vec3 start{}, end{};
  for (size_t i = 0; i < 3; ++i) {
    start[i] = (*near)[i] * 0.1f;
    end[i] = (*far)[i] * 0.1f;
  }
  std::optional<CameraBoxPick> result;
  for (size_t i = 0; i < shapes.size(); ++i) {
    const auto& candidate = shapes[i];
    std::optional<float> fraction;
    physics::Vec3 localStart{}, localEnd{};
    if (candidate.box && !candidate.body) {
      if (candidate.box->geometryFlags != 5 || candidate.box->position[0] != candidate.box->position[1]) {
        error = "cursor static box unavailable";
        return std::nullopt;
      }
      for (size_t a = 0; a < 3; ++a) {
        localStart[a] = start[a] - candidate.box->position[0][a];
        localEnd[a] = end[a] - candidate.box->position[0][a];
      }
      fraction = physicsBoxRayFraction(localStart, localEnd, candidate.box->halfExtents);
    } else if (candidate.body && !candidate.box) {
      const auto& body = *candidate.body;
      if (!body.proto || body.current > 1) {
        error = "cursor body prototype or current pose unavailable";
        return std::nullopt;
      }
      const auto& shape = body.proto->shape;
      if ((body.flags & 0x4020u) || shape.type == physics::ShapeType::Empty)
        continue;
      const auto& pose = body.state[body.current];
      for (float value : pose.pos)
        if (!std::isfinite(value)) {
          error = "cursor body pose nonfinite";
          return std::nullopt;
        }
      for (float value : pose.rot)
        if (!std::isfinite(value)) {
          error = "cursor body rotation nonfinite";
          return std::nullopt;
        }
      if (!std::isfinite(shape.boundRadius) || shape.boundRadius < 0) {
        error = "cursor body bound unavailable";
        return std::nullopt;
      }
      const auto bound = sphereFraction(start, end, pose.pos, shape.boundRadius);
      if (!bound || (result && result->fraction < *bound))
        continue;
      localStart = localPoint(pose, start);
      localEnd = localPoint(pose, end);
      switch (shape.type) {
      case physics::ShapeType::Sphere:
        fraction = bound;
        break;
      case physics::ShapeType::Box:
        for (size_t axis = 0; axis < 3; ++axis)
          if (!std::isfinite(shape.dim[axis]) || shape.dim[axis] < 0) {
            error = "cursor box dimensions unavailable";
            return std::nullopt;
          }
        fraction = physicsBoxRayFraction(localStart, localEnd, {shape.dim[0], shape.dim[1], shape.dim[2]});
        break;
      case physics::ShapeType::Mesh:
      case physics::ShapeType::Hull:
        for (const auto& triangle : shape.triangles) {
          const auto hit = triangleFraction(shape, triangle, localStart, localEnd,
                                            fraction.value_or(result ? result->fraction : 1.0f), error);
          if (!error.empty()) return std::nullopt;
          if (hit && *hit > 0 && (!fraction || *hit < *fraction)) fraction = hit;
        }
        break;
      // FUN_0049deb0's default deliberately supplies no pick for cylinders.
      case physics::ShapeType::CylinderX:
      case physics::ShapeType::CylinderY:
      case physics::ShapeType::CylinderZ:
      case physics::ShapeType::Empty:
        break;
      default:
        error = "cursor body shape outside Stock ray dispatcher";
        return std::nullopt;
      }
    } else {
      error = "cursor shape must name exactly one body or static box";
      return std::nullopt;
    }
    if (fraction && *fraction < (result ? result->fraction : 1.0f)) {
      physics::Vec3 local{}, world{};
      for (size_t a = 0; a < 3; ++a) {
        local[a] = localEnd[a] + (localStart[a] - localEnd[a]) * *fraction;
        world[a] = end[a] + (start[a] - end[a]) * *fraction;
      }
      result = CameraBoxPick{i, *fraction, local, world};
    }
  }
  return result;
}

std::optional<CameraBoxPick> pickCameraBox(
    const std::array<float, 2>& cursor, const ViewportInstance& viewport,
    const std::array<float, 16>& cameraWorld, const std::array<float, 16>& projection,
    const std::vector<PhysicsBox>& boxes) {
  if (!std::isfinite(cursor[0]) || !std::isfinite(cursor[1]) ||
      !std::isfinite(viewport.left) || !std::isfinite(viewport.top) ||
      !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
      viewport.width <= 0 || viewport.height <= 0) return std::nullopt;
  const float x = (cursor[0] - (2 * viewport.left - 1)) / viewport.width - 1;
  const float y = (cursor[1] - (2 * viewport.top - 1)) / viewport.height - 1;
  if (x < -1 || x > 1 || y < -1 || y > 1) return std::nullopt;
  const auto near = unprojectCameraPoint({x, -y, 1}, cameraWorld, projection);
  const auto far = unprojectCameraPoint({x, -y, 0}, cameraWorld, projection);
  if (!near || !far) return std::nullopt;
  std::optional<CameraBoxPick> result;
  for (size_t i = 0; i < boxes.size(); ++i) {
    if (boxes[i].geometryFlags != 5 || boxes[i].position[0] != boxes[i].position[1]) continue;
    std::array<float, 3> start{}, end{};
    for (size_t axis = 0; axis < 3; ++axis) {
      start[axis] = (*near)[axis] * 0.1f - boxes[i].position[0][axis];
      end[axis] = (*far)[axis] * 0.1f - boxes[i].position[0][axis];
    }
    const auto fraction = physicsBoxRayFraction(start, end, boxes[i].halfExtents);
    if (fraction && (!result || *fraction < result->fraction)) {
      std::array<float, 3> contact{};
      for (size_t axis = 0; axis < 3; ++axis)
        contact[axis] = end[axis] + (start[axis] - end[axis]) * *fraction;
      result = CameraBoxPick{i, *fraction, contact};
    }
  }
  return result;
}
