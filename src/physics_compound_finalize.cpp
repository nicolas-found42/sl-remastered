#include "physics_compound_finalize.hpp"
#include "physics_compound_mass.hpp"
#include "physics_polyhedron.hpp"
#include <algorithm>
#include <cmath>

bool finalizeCompound(physics::Body& root, physics::BodyProto& prototype,
                      const std::vector<physics::Body*>& children, std::string& error) {
  if (root.proto != &prototype || root.current > 1 ||
      (prototype.flags & physics::kProtoRoot) || (root.flags & 0x8000) ||
      (prototype.shape.type != physics::ShapeType::Mesh && prototype.shape.type != physics::ShapeType::Box)) {
    error = "unsupported compound root"; return false;
  }
  std::vector<physics::CompoundMassChild> masses;
  for (const auto* child : children) {
    if (!child || !child->proto || !child->attachment || child->attachment->parent != &root) {
      error = "invalid compound child"; return false;
    }
    const auto& relative = child->attachment->relative;
    masses.push_back({*child->proto, relative.position, relative.rotation()});
  }
  auto mass = physics::aggregateCompoundMass(masses);
  prototype.invMass = mass.prototype.invMass;
  prototype.inertia = mass.prototype.inertia;
  prototype.invInertia = mass.prototype.invInertia;
  prototype.linearDrag = mass.prototype.linearDrag;
  prototype.angularDrag = mass.prototype.angularDrag;
  prototype.flags = (prototype.flags & ~physics::kProtoStatic) | (mass.prototype.flags & physics::kProtoStatic);
  physics::Vec3 low{}, high{};
  for (const auto& c : children) {
    if ((*c).flags & 0x20) continue;
    const auto& shape = (*c).proto->shape;
    std::vector<physics::Vec3> points;
    switch (shape.type) {
      case physics::ShapeType::Sphere: points.push_back({}); break;
      case physics::ShapeType::CylinderX: case physics::ShapeType::CylinderY: case physics::ShapeType::CylinderZ: {
        auto axis = static_cast<size_t>(static_cast<uint32_t>(shape.type)-2); physics::Vec3 p{};
        p[axis] = shape.dim[0]; points.push_back(p); p[axis] = -p[axis]; points.push_back(p); break;
      }
      case physics::ShapeType::Box:
        for (int n=0;n<8;++n) points.push_back({(n&1?1.f:-1.f)*shape.dim[0],(n&2?1.f:-1.f)*shape.dim[1],(n&4?1.f:-1.f)*shape.dim[2]});
        break;
      case physics::ShapeType::Mesh: case physics::ShapeType::Hull:
        points = shape.vertices;
        if (points.empty() && shape.core)
          for (const auto& vertex : shape.core->vertices) points.push_back(vertex.position);
        break;
      default: error = "unsupported child collision shape"; return {};
    }
    if (points.empty()) { error = "missing collision vertices"; return {}; }
    const auto& rotation = (*c).attachment->relative.rotation();
    const float pad = shape.radius + (((*c).flags & physics::kBodyHasChildren) ? 0.0f : 0.01f);
    for (const auto& p : points) {
      const auto v = physics::transformPoint((*c).attachment->relative.position, rotation, p);
      for (size_t j=0;j<3;++j) { low[j] = std::min(low[j],v[j]-pad); high[j] = std::max(high[j],v[j]+pad); }
    }
  }
  float l2=0,h2=0; for(size_t j=0;j<3;++j) { l2+=low[j]*low[j]; h2+=high[j]*high[j]; }
  if (prototype.invMass == 0) for (size_t j=0;j<3;++j) mass.center[j] = (low[j]+high[j])*0.5f;
  for (size_t j=0;j<3;++j) { low[j]-=mass.center[j]; high[j]-=mass.center[j]; prototype.shape.centerOffset[j]=(prototype.invMass == 0 ? mass.center[j] : -mass.center[j]); prototype.shape.extent[j]=std::max(std::abs(low[j]),std::abs(high[j])); }
  auto bounds = physics::boundsVertices(low, high);
  prototype.shape.vertices.clear();
  prototype.shape.core = std::make_shared<physics::CollisionCore>(std::move(bounds));
  prototype.shape.radius=0;
  for (float& d : prototype.shape.dim) d=0;
  prototype.shape.boundRadius=std::sqrt(std::max(l2,h2));
  const float centerSq = mass.center[0]*mass.center[0]+mass.center[2]*mass.center[2]+mass.center[1]*mass.center[1];
  if (centerSq > 0.00001f) {
    for (auto& c : children) for(size_t j=0;j<3;++j) (*c).attachment->relative.position[j]-=mass.center[j];
    const auto shift = physics::matrixTransformRows(root.state[root.current].rot,mass.center);
    for (size_t j=0;j<3;++j) root.state[root.current].pos[j]+=shift[j];
    root.state[root.current ^ 1].pos=root.state[root.current].pos;
    if (root.attachment) {
      const auto offset = physics::matrixTransformRows(root.attachment->relative.rotation(), mass.center);
      for (size_t i = 0; i < 3; ++i) root.attachment->relative.position[i] += offset[i];
    }
  }
  for (auto& state : root.state) physics::updateWorldInertia(prototype,state);
  prototype.shape.type = physics::ShapeType::Mesh;
  return true;
}
