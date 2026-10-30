#include "physics_compound_step.hpp"
#include <cassert>
#include <cstring>
using namespace physics;
int main() {
 BodyProto proto; proto.invMass=1; proto.flags=kProtoSimple;
 Body root, child; root.proto=&proto; child.proto=&proto;
 root.flags=kBodyActive|kBodyHasChildren|kBodyNoRotation;
 child.flags=kBodyActive|kBodyChild;
 child.attachment=BodyAttachment{&root,{{2,1,0},{0,0,0,1}}};
 root.state[0].vel={2,1,0}; root.state[1].omega={0.25f,0,0};
 auto original=root.state[0];
 assert(stepCompound({&root},0.25f)==CompoundStepResult::Unsupported);
 assert(root.current==0 && std::memcmp(&original,&root.state[0],sizeof original)==0);
 child.flags|=kBodyBit10;
 assert(stepCompound({&root,&child},0.25f)==CompoundStepResult::Unsupported);
 child.flags&=~kBodyBit10;
 assert(stepCompound({&root,&child},0.25f)==CompoundStepResult::Stepped);
 assert(root.current==1 && child.current==1);
 assert(child.state[1].pos==Vec3({2.5f,1.25f,0}));
 assert(child.state[1].vel==Vec3({2,1,0.25f}));
 root.current=child.current=0; root.state[0].vel={};root.state[1].stepFlags=0;
 root.state[1].omega={};root.activity={};
 assert(stepCompound({&root,&child},0.25f)==CompoundStepResult::Slept);
 assert(root.current==0 && child.current==0);
 assert((root.flags & kBodyAsleep) && (child.flags & kBodyAsleep));
 assert(std::memcmp(&root.state[0],&root.state[1],sizeof(State))==0);
 assert(std::memcmp(&child.state[0],&child.state[1],sizeof(State))==0);
 assert(wakeDetachedCompound({&root,&child}));
 assert(!(root.flags & kBodyAsleep) && !(child.flags & kBodyAsleep));
 assert(root.activity==Vec3({0,100000,0}));
 // Predict two separate roots before either is committed, as the world pass does.
 Body a, ac, b, bc; a.proto=ac.proto=b.proto=bc.proto=&proto;
 a.flags=b.flags=kBodyActive|kBodyHasChildren|kBodyNoRotation;
 ac.flags=bc.flags=kBodyActive|kBodyChild;
 ac.attachment=BodyAttachment{&a,{{2,0,0},{0,0,0,1}}};
 bc.attachment=BodyAttachment{&b,{{1,0,0},{0,0,0,1}}};
 a.state[0].vel={2,0,0}; b.state[0].vel={0,2,0};
 assert(commitCompound({&a,&ac})==CompoundStepResult::Unsupported);
 assert(predictCompound({&a,&ac},0.5f));
 assert(predictCompound({&b,&bc},0.5f));
 assert(a.current==0 && ac.current==0 && b.current==0 && bc.current==0);
 assert(ac.state[1].pos==Vec3({3,0,0}) && bc.state[1].pos==Vec3({1,1,0}));
 assert(commitCompound({&a,&ac})==CompoundStepResult::Stepped);
 assert(a.current==1 && ac.current==1 && b.current==0 && bc.current==0);
 assert(commitCompound({&b,&bc})==CompoundStepResult::Stepped);
 assert(b.current==1 && bc.current==1);
 Body invalid, invalidChild; invalid.proto=invalidChild.proto=&proto;
 invalid.flags=kBodyActive|kBodyHasChildren; invalidChild.flags=kBodyActive|kBodyChild;
 invalidChild.attachment=BodyAttachment{&invalid,{}};
 assert(predictCompound({&invalid,&invalidChild},0.1f));
 invalidChild.proto=nullptr;
 const auto before=invalid.state[0];
 assert(commitCompound({&invalid,&invalidChild})==CompoundStepResult::Unsupported);
 assert(invalid.current==0 && std::memcmp(&before,&invalid.state[0],sizeof before)==0);
}
