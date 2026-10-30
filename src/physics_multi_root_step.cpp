#include "physics_multi_root_step.hpp"
#include "physics_compound_step.hpp"
#include <algorithm>
#include <cmath>

namespace physics {
namespace {
bool validate(const std::vector<BodyTree>& roots, std::string& error) {
  std::vector<const Body*> seen;
  for (const auto& tree : roots) {
    if (tree.empty()) { error="multi-root: empty tree"; return false; }
    for (size_t i=0; i<tree.size(); ++i) {
      const auto* b=tree[i];
      if (!b || !b->proto || b->current>1 || !(b->flags&kBodyActive) ||
          (b->flags&(kBodyAsleep|kBodyBit10)) || (b->proto->flags&kProtoStatic) ||
          std::find(seen.begin(),seen.end(),b)!=seen.end()) {
        error="multi-root: unsupported or duplicate body"; return false;
      }
      if (i==0) {
        if (b->attachment || (b->flags&kBodyChild) || !std::isfinite(b->proto->invMass) || b->proto->invMass<=0) {
          error="multi-root: invalid root"; return false;
        }
      } else if (!(b->flags&kBodyChild) || !b->attachment ||
                 std::find(tree.begin(),tree.begin()+i,b->attachment->parent)==tree.begin()+i) {
        error="multi-root: invalid child"; return false;
      }
      bool hasChild=false;
      for (size_t j=i+1;j<tree.size();++j)
        if (tree[j] && tree[j]->attachment && tree[j]->attachment->parent==b) hasChild=true;
      if (hasChild != bool(b->flags&kBodyHasChildren)) {
        error="multi-root: incomplete tree"; return false;
      }
      seen.push_back(b);
    }
  }
  return true;
}
struct Snapshot {
  std::vector<Body*> pointers;
  std::vector<Body> bodies;
  explicit Snapshot(const std::vector<BodyTree>& roots) {
    for (const auto& tree : roots) for (auto* body : tree) {
      pointers.push_back(body); bodies.push_back(*body);
    }
  }
  void restore() const { for (size_t i=0;i<pointers.size();++i) *pointers[i]=bodies[i]; }
};
}
bool predictRootSet(const std::vector<BodyTree>& roots, float dt, std::string& error) {
  error.clear();
  if (!std::isfinite(dt) || dt<=0 || !validate(roots,error)) {
    if (error.empty()) error="multi-root: invalid prediction time";
    return false;
  }
  Snapshot snapshot(roots);
  for (const auto& tree : roots) {
    const bool ok=tree.size()==1 ? stepBody(*tree[0],dt) : predictCompound(tree,dt);
    if (!ok) { snapshot.restore(); error="multi-root: prediction unsupported"; return false; }
  }
  return true;
}
FreeBodyStepResult stepMultiRootWorld(World& world, const std::vector<BodyTree>& roots,
                                     float dt, const MultiRootWork& work,
                                     std::string& error) {
  error.clear();
  const auto fail=[&](const char* why) { error=why; return FreeBodyStepResult::Unsupported; };
  if (!work.collisionFreeProven || work.contacts || work.joints || work.wheelConstraints ||
      work.callbacks || work.waterOrClipPlanes) return fail("multi-root: physics work unsupported");
  if (!std::isfinite(dt) || dt<=0 || !std::isfinite(world.pending) || world.pending<0 ||
      !std::isfinite(world.time) || !std::isfinite(dt+world.pending) ||
      !std::isfinite(world.params.airDensity) ||
      !std::all_of(world.params.gravity.begin(),world.params.gravity.end(),[](float v){return std::isfinite(v);}))
    return fail("multi-root: invalid world time or forces");
  if (!validate(roots,error)) return FreeBodyStepResult::Unsupported;
  if (roots.size()!=world.bodies.size()) return fail("multi-root: registration mismatch");
  for (size_t i=0;i<roots.size();++i)
    if (roots[i][0]!=world.bodies[i]) return fail("multi-root: registration mismatch");
  const auto isRoot = [&](const Body* body) {
    return body && std::any_of(roots.begin(), roots.end(),
                               [&](const BodyTree& tree) { return tree[0] == body; });
  };
  for (const auto& cb : work.cameraCallbacks) {
    if (!isRoot(cb.camera) || !isRoot(cb.look) || !isRoot(cb.move) || !cb.smoothing ||
        !cb.input.otherBranchesInactive)
      return fail("multi-root: unsupported camera callback");
  }
  const float total=dt+world.pending;
  if (total<k::minimumStep) { world.pending=total; return FreeBodyStepResult::Accumulated; }
  if (!std::isfinite(world.time+total)) return fail("multi-root: time overflow");
  Snapshot snapshot(roots);
  const World saved=world;
  std::vector<CameraFollowSmoothing> smoothing;
  for (const auto& cb : work.cameraCallbacks) smoothing.push_back(*cb.smoothing);
  const auto rollback = [&] {
    snapshot.restore(); world=saved;
    for (size_t i=0; i<work.cameraCallbacks.size(); ++i)
      *work.cameraCallbacks[i].smoothing=smoothing[i];
  };
  world.pending=0; world.lastStep=total;
  for (const auto& tree : roots) for (auto* body : tree)
    if (!accumulateWorldForces(world.params,*body)) {
      rollback(); return fail("multi-root: force pass unsupported");
    }
  for (const auto& cb : work.cameraCallbacks) {
    auto input=cb.input;
    input.camera=cb.camera->state[cb.camera->current];
    input.lookTarget=cb.look->state[cb.look->current];
    input.moveTarget=cb.move->state[cb.move->current];
    const auto follow=cameraFollowForce(input,*cb.smoothing);
    if (!follow) { rollback(); return fail("multi-root: camera callback unsupported"); }
    *cb.smoothing=follow->smoothing;
    addForce(*cb.camera,follow->force,nullptr);
    auto& state=cb.camera->state[cb.camera->current];
    for (size_t i=0; i<3; ++i) state.torque[i]+=follow->torque[i];
  }
  if (!predictRootSet(roots,total,error)) { rollback(); return FreeBodyStepResult::Unsupported; }
  world.stepsDone += static_cast<uint32_t>(roots.size());
  for (const auto& tree : roots) {
    if (tree.size()==1) {
      const auto status=commit(*tree[0]);
      if (status==CommitResult::WentToSleep) sleepBodyState(*tree[0]);
      else if (status!=CommitResult::Flipped) {
        rollback(); return fail("multi-root: commit unsupported");
      }
    } else if (commitCompound(tree)==CompoundStepResult::Unsupported) {
      rollback(); return fail("multi-root: compound commit unsupported");
    }
  }
  world.time+=total;
  return FreeBodyStepResult::Advanced;
}
}
