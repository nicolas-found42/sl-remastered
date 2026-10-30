#include "physics_contact_geometry.hpp"
#include <cmath>
#include <vector>
namespace physics {
static ContactGeometryResult convert(DirectContact& c, const CorePair& pair,
                                                   const State& a, const State& b,
                                                   float radiusA, float radiusB, Vec3& worldPoint, bool allowClose) {
  if (!c.a || !c.b || !std::isfinite(pair.distance) || !std::isfinite(radiusA) ||
      !std::isfinite(radiusB) || radiusA < 0 || radiusB < 0) return ContactGeometryResult::Unsupported;
  const float radii = radiusB + radiusA;
  if (!std::isfinite(radii)) return ContactGeometryResult::Unsupported;
  if (!allowClose && (pair.distance == 0 || radii + k::skin < pair.distance)) return ContactGeometryResult::Dropped;
  // Closer pairs also clear root step flags; overlaps additionally bisect time.
  if (!allowClose && pair.distance < radii + 0.02f) return ContactGeometryResult::Unsupported;
  for (unsigned i=0;i<3;++i) {
    if (!std::isfinite(pair.normal[i]) || !std::isfinite(pair.pointA[i]) ||
        !std::isfinite(pair.pointB[i]) || !std::isfinite(a.pos[i])) return ContactGeometryResult::Unsupported;
  }
  for (float x:a.rot) if (!std::isfinite(x)) return ContactGeometryResult::Unsupported;
  for (float x:b.rot) if (!std::isfinite(x)) return ContactGeometryResult::Unsupported;
  Vec3 pa=pair.pointA, pb=pair.pointB;
  float separation=pair.distance;
  if (radiusA > 0) {
    separation -= radiusA;
    for (unsigned i=0;i<3;++i)
      pa[i] += (a.rot[i]*pair.normal[0]+a.rot[8+i]*pair.normal[2]+a.rot[4+i]*pair.normal[1]) * -radiusA;
  }
  if (radiusB > 0) {
    separation -= radiusB;
    for (unsigned i=0;i<3;++i)
      pb[i] += (b.rot[i]*pair.normal[0]+b.rot[8+i]*pair.normal[2]+b.rot[4+i]*pair.normal[1]) * radiusB;
  }
  Vec3 point;
  for (unsigned i=0;i<3;++i)
    point[i] = (a.rot[i*4]*pa[0] + a.rot[i*4+2]*pa[2] + a.rot[i*4+1]*pa[1]) + a.pos[i];
  c.pointA=pa; c.pointB=pb; c.normal=pair.normal; c.separation=separation; worldPoint=point;
  return ContactGeometryResult::Updated;
}
ContactGeometryResult updateDirectContactGeometry(DirectContact& c, const CorePair& pair,
    const State& a, const State& b, float ra, float rb, Vec3& worldPoint) {
  return convert(c,pair,a,b,ra,rb,worldPoint,false);
}
namespace {
Body* root(Body* b) {
  std::vector<Body*> seen;
  while (b && (b->flags & kBodyChild)) {
    for (Body* p:seen) if (p==b) return nullptr;
    seen.push_back(b);
    if (!b->attachment) return nullptr;
    b=b->attachment->parent;
  }
  return b;
}
bool validRotation(const State& s) {
  if (s.rot!=matrixFromQuat(s.quat)) return false;
  float norm=((s.quat[2]*s.quat[2]+s.quat[1]*s.quat[1])+s.quat[3]*s.quat[3])+s.quat[0]*s.quat[0];
  return std::isfinite(norm) && std::fabs(norm-1.0f)<0.0001f;
}
State sampleInternal(const Body& b, float t) {
  State s;
  if (!(b.flags & kBodyActive)) return s;
  const State& predicted=b.state[b.current^1], &current=b.state[b.current];
  for(unsigned i=0;i<3;i++) s.pos[i]=(1-t)*current.pos[i]+t*predicted.pos[i];
  float dot=((current.quat[2]*predicted.quat[2]+current.quat[1]*predicted.quat[1])+
             current.quat[3]*predicted.quat[3])+current.quat[0]*predicted.quat[0];
  const float sign=dot<0?-1.0f:1.0f; dot=std::fabs(dot);
  float a=1-t,z=t;
  if (1-dot>0.0001) {
    float product=(1+dot)*(1-dot);
    float root=std::sqrt(product);
    float angle=static_cast<float>(std::atan2(static_cast<double>(root),static_cast<double>(dot)));
    float divisor=static_cast<float>(std::sin(static_cast<double>(angle)));
    float left=(1-t)*angle, right=angle*t;
    a=static_cast<float>(std::sin(static_cast<double>(left))/divisor);
    z=static_cast<float>(std::sin(static_cast<double>(right))/divisor);
  }
  for(unsigned i=0;i<4;i++) s.quat[i]=a*current.quat[i]+z*(sign*predicted.quat[i]);
  s.rot=matrixFromQuat(s.quat);
  s.time=t*predicted.time+(1-t)*current.time;
  return s;
}
}
State interpolateContactPose(const Body& body, float fraction) { return sampleInternal(body, fraction); }
ContactGeometryResult updateDirectContactGeometryWithSearch(DirectContact& c,
    const CorePair& predictedPair, float ra, float rb, Vec3& worldPoint,
    ContactGeometryWorld& world, const ContactCoreQuery& query) {
  if (!c.a || !c.b || c.a->current>1 || c.b->current>1 ||
      !std::isfinite(ra) || !std::isfinite(rb) || ra<0 || rb<0 ||
      !std::isfinite(predictedPair.distance)) return ContactGeometryResult::Unsupported;
  Body* a=root(c.a); Body* b=root(c.b);
  if (!a || !b || !a->proto || !b->proto || a->current>1 || b->current>1) return ContactGeometryResult::Unsupported;
  bool da=!(a->proto->flags & kProtoStatic), db=!(b->proto->flags & kProtoStatic);
  if (!da && !db) return ContactGeometryResult::Unsupported;
  float radii=rb+ra; if (!std::isfinite(radii) ) return ContactGeometryResult::Unsupported;
  if (predictedPair.distance==0 || radii+k::skin<predictedPair.distance) { ++world.queries; return ContactGeometryResult::Dropped; }
  const State& initialA=c.a->state[c.a->current^1];
  const State& initialB=c.b->state[c.b->current^1];
  if (predictedPair.distance<=-radii && c.separation<radii) {
    DirectContact candidate=c; Vec3 unused;
    auto result=convert(candidate,predictedPair,initialA,initialB,ra,rb,unused,true);
    if (result!=ContactGeometryResult::Updated) return result;
    ++world.queries; c=candidate;
    return ContactGeometryResult::Updated;
  }
  State const* poseA=&c.a->state[c.a->current^1]; State const* poseB=&c.b->state[c.b->current^1];
  State displayA=*poseA; CorePair pair=predictedPair; uint32_t extraQueries=0;
  if (pair.distance<=radii) {
    if (!query) return ContactGeometryResult::Unsupported;
    for (Body* body : {c.a,c.b}) if ((body->flags & kBodyActive) &&
        (!validRotation(body->state[body->current]) || !validRotation(body->state[body->current^1]))) return ContactGeometryResult::Unsupported;
    float t=0.5f, delta=0.25f;
    for (int iteration=0;iteration<10;iteration++) {
      State sa=sampleInternal(*c.a,t), sb=sampleInternal(*c.b,t); auto evaluated=query(sa,sb);
      if (!evaluated || !std::isfinite(evaluated->distance)) return ContactGeometryResult::Unsupported;
      pair=*evaluated; ++extraQueries; if (c.a->flags & kBodyActive) displayA=sa;
      if (pair.distance>radii) {
        if (pair.distance<radii+0.02f) {
          Vec3 wa=transformPoint(c.a->state[c.a->current^1].pos,c.a->state[c.a->current^1].rot,pair.pointA);
          Vec3 wb=transformPoint(c.b->state[c.b->current^1].pos,c.b->state[c.b->current^1].rot,pair.pointB);
          float dot=(wa[0]-wb[0])*pair.normal[0]+(wa[1]-wb[1])*pair.normal[1]+(wa[2]-wb[2])*pair.normal[2];
          if (dot<0) break;
        }
        t+=delta;
      } else t-=delta;
      delta*=0.5f;
    }
    if (pair.distance<=radii) {
      poseA=&c.a->state[c.a->current]; poseB=&c.b->state[c.b->current];
      auto evaluated=query(*poseA,*poseB); if (!evaluated) return ContactGeometryResult::Unsupported;
      pair=*evaluated; ++extraQueries;
    }
  }
  DirectContact candidate=c; Vec3 ignored;
  // The search branch can intentionally return an overlapping current pose.
  CorePair adjusted=pair;
  auto result=convert(candidate,adjusted,*poseA,*poseB,ra,rb,ignored,true);
  if (result!=ContactGeometryResult::Updated) return result;
  Vec3 point;
  for(unsigned i=0;i<3;i++) point[i]=(displayA.rot[i*4]*candidate.pointA[0]+displayA.rot[i*4+2]*candidate.pointA[2]+displayA.rot[i*4+1]*candidate.pointA[1])+displayA.pos[i];
  if (predictedPair.distance<radii+0.02f) {
    if (da) a->state[a->current^1].stepFlags &= ~1u;
    if (db) b->state[b->current^1].stepFlags &= ~1u;
    world.stepReady=0;
  }
  world.queries+=1+extraQueries; c=candidate; worldPoint=point;
  return ContactGeometryResult::Updated;
}

}
