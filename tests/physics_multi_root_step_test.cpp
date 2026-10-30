#include "physics_multi_root_step.hpp"
#include <cmath>
#include <string>
using namespace physics;

int main() {
  BodyProto proto; proto.invMass=1; proto.flags=kProtoSimple;
  Body root, child, camera;
  root.proto=child.proto=camera.proto=&proto;
  root.flags=kBodyActive|kBodyHasChildren|kBodyNoRotation;
  child.flags=kBodyActive|kBodyChild;
  camera.flags=kBodyActive|kBodyNoRotation;
  child.attachment=BodyAttachment{&root,{{2,0,0},{0,0,0,1}}};
  root.state[0].vel={2,0,0}; camera.state[0].vel={0,2,0};
  std::vector<BodyTree> roots{{&root,&child},{&camera}};
  std::string error;
  // Failure in a later root restores earlier predictions.
  camera.state[1].stepFlags=1;
  if (predictRootSet(roots,0.5f,error) || root.state[1].stepFlags || root.current || child.current) return 1;
  camera.state[1].stepFlags=0;
  if (!predictRootSet(roots,0.5f,error) || root.current || child.current || camera.current ||
      child.state[1].pos!=Vec3({3,0,0}) || camera.state[1].pos!=Vec3({0,1,0})) return 2;
  // Begin a fresh world step with no pre-existing predictions.
  root.state[1]={}; child.state[1]={}; camera.state[1]={};
  World world; world.params.gravityEnabled=false; world.bodies={&root,&camera};
  MultiRootWork work; work.collisionFreeProven=true;
  const auto oldRoot=root.state[0]; const auto oldCamera=camera.state[0];
  for (int mode=0;mode<6;++mode) {
    auto unsupported=work;
    if (mode==0) unsupported.collisionFreeProven=false;
    if (mode==1) unsupported.contacts=true;
    if (mode==2) unsupported.joints=true;
    if (mode==3) unsupported.wheelConstraints=true;
    if (mode==4) unsupported.callbacks=true;
    if (mode==5) unsupported.waterOrClipPlanes=true;
    if (stepMultiRootWorld(world,roots,0.5f,unsupported,error)!=FreeBodyStepResult::Unsupported ||
        world.time!=0 || root.current || camera.current || root.state[0].pos!=oldRoot.pos ||
        camera.state[0].force!=oldCamera.force) return 3;
  }
  if (stepMultiRootWorld(world,roots,0.5f,work,error)!=FreeBodyStepResult::Advanced ||
      root.current!=1 || child.current!=1 || camera.current!=1 ||
      child.state[1].pos!=Vec3({3,0,0}) || camera.state[1].pos!=Vec3({0,1,0}) ||
      std::fabs(world.time-0.5f)>1e-6f || world.stepsDone!=2) return 4;
  auto duplicate=roots; duplicate.push_back({&camera});
  const auto position=root.state[root.current].pos;
  if (stepMultiRootWorld(world,duplicate,0.1f,work,error)!=FreeBodyStepResult::Unsupported ||
      world.time!=0.5f || root.state[root.current].pos!=position) return 5;
  // The camera observes the compound root before prediction, and its force
  // affects the camera in the same step. A later prediction failure rolls
  // back both the body and the callback's persistent smoothing state.
  Body car, wheel, follower;
  car.proto=wheel.proto=follower.proto=&proto;
  car.flags=kBodyActive|kBodyHasChildren|kBodyNoRotation;
  wheel.flags=kBodyActive|kBodyChild;
  follower.flags=kBodyActive|kBodyNoRotation;
  wheel.attachment=BodyAttachment{&car,{{2,0,0},{0,0,0,1}}};
  car.state[0].pos={10,0,0}; car.state[0].vel={2,0,0};
  World followWorld; followWorld.params.gravityEnabled=false; followWorld.bodies={&car,&follower};
  std::vector<BodyTree> followRoots{{&car,&wheel},{&follower}};
  CameraFollowSmoothing smoothing;
  CameraFollowStep cb; cb.camera=&follower; cb.look=cb.move=&car; cb.smoothing=&smoothing;
  cb.input.otherBranchesInactive=true; cb.input.command.force=2; cb.input.maxForce=3;
  MultiRootWork followWork; followWork.collisionFreeProven=true; followWork.cameraCallbacks={cb};
  follower.state[1].stepFlags=1;
  if (stepMultiRootWorld(followWorld,followRoots,0.5f,followWork,error)!=FreeBodyStepResult::Unsupported ||
      followWorld.time!=0 || !smoothing.initializeLook || !smoothing.initializeMove ||
      follower.state[0].force!=Vec3{} || car.state[1].stepFlags) return 6;
  follower.state[1].stepFlags=0;
  if (stepMultiRootWorld(followWorld,followRoots,0.5f,followWork,error)!=FreeBodyStepResult::Advanced ||
      smoothing.look!=Vec3({10,0,0}) || smoothing.move!=Vec3({10,0,0}) ||
      follower.state[follower.current].pos[0]<=0 || car.state[car.current].pos[0]<=10) return 7;
  return 0;
}
