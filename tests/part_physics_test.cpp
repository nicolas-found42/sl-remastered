#include "part_physics.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

static bool near(float a, float b) { return std::abs(a-b) < 0.0001f; }
int main() {
  CarConfig config;
  config.rootBody = 0;
  config.bodies.resize(2);
  std::string error;
  std::vector<CarGeometryPrototype> geometry(2);
  for (size_t i=0;i<2;++i) {
    geometry[i].bodyIndex=i;
    geometry[i].transform.position={static_cast<float>(i*2+1),0,0};
    auto proto=physics::createBodyProto(1,1,0.5f,0,0,nullptr,error);
    assert(proto);
    *geometry[i].body = *proto;
  }
  auto result=buildPartPhysics(config,geometry,"10,0,0,0,0,0,42",error);
  assert(result && error.empty());
  assert(result->root->proto == &result->rootPrototype);
  assert(result->children.size()==2 && result->children[0].body->proto == geometry[0].body.get());
  assert(near(result->rootPrototype.invMass,0.5f));
  assert(near(result->root->state[0].pos[0],12) && near(result->root->state[1].pos[0],12));
  assert(near(result->children[0].body->attachment->relative.position[0],-1));
  assert(near(result->children[1].body->attachment->relative.position[0],1));
  auto* originalBody = result->children[0].body.get();
  auto* originalPrototype = originalBody->proto;
  for (int i=0; i<1024; ++i) result->children.emplace_back();
  assert(result->children[0].body.get() == originalBody && originalBody->proto == originalPrototype);
  assert(originalBody->attachment->parent == result->root.get());
  assert(result->argument==42 && result->bounds.vertices.size()==8);
  assert(near(result->rootPrototype.shape.extent[0],2.0f));
  geometry[1].bodyFlags=kBodyNoCollision;
  auto excluded=buildPartPhysics(config,geometry,"0,0,0,0,0,0,0",error);
  assert(excluded && near(excluded->rootPrototype.invMass,0.5f));
  assert(near(excluded->rootPrototype.shape.extent[0],2.0f));
  geometry.pop_back();
  assert(!buildPartPhysics(config,geometry,"0,0,0,0,0,0,0",error));
  assert(error.find("missing geometry")!=std::string::npos);
  std::unique_ptr<PartPhysics> independent;
  std::weak_ptr<physics::BodyProto> prototypeLifetime;
  {
    std::vector<CarGeometryPrototype> source(2);
    for (size_t i = 0; i < source.size(); ++i) {
      source[i].bodyIndex = i;
      auto p = physics::createBodyProto(1,1,0.5f,0,0,nullptr,error);
      assert(p);
      *source[i].body = *p;
    }
    prototypeLifetime = source[0].body;
    independent = buildPartPhysics(config, source, "0,0,0,0,0,0,0", error);
    assert(independent && independent->children[0].body->proto == source[0].body.get());
  }
  assert(!prototypeLifetime.expired());
  assert(independent->children[0].body->proto == prototypeLifetime.lock().get());
  independent.reset();
  assert(prototypeLifetime.expired());
  CarConfig clickOnly;
  CarGeometryPrototype click;
  auto clickProto = physics::createBodyProto(0, 1, 0.5f, 0, 0, nullptr, error);
  assert(clickProto);
  *click.body = *clickProto;
  click.transform.rotation = {1,0,0,0,1,0,0,0,1};
  auto battery = buildPartPhysics(clickOnly, {}, "1,2,3,0,0,0,7", error, &click, 4);
  assert(battery && battery->root && battery->children.empty());
  assert(battery->root->proto == &battery->rootPrototype && battery->rootPrototype.invMass == 0);
  assert(near(battery->root->state[0].pos[0], 1) && near(battery->root->state[0].pos[1], 2));
  assert(near(battery->root->state[0].pos[2], 3) && near(battery->root->state[0].time, 4));
  assert(battery->argument == 7);
  std::cout << "part physics: ok\n";
}
