#include <cstdio>
#include "physics_core_pair.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>

namespace physics {
namespace {
bool point(const CollisionCore& c) {
  return c.vertices.size() == 1 && c.edges.empty() && c.faces.empty();
}
bool segment(const CollisionCore& c) {
  return c.vertices.size() == 2 && c.edges.size() == 1 && c.faces.empty() &&
         c.edges[0].vertices[0] < 2 && c.edges[0].vertices[1] < 2 &&
         c.edges[0].vertices[0] != c.edges[0].vertices[1];
}
Vec3 subtract(const Vec3& a, const Vec3& b) {
  return {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
}
float dot(const Vec3& a, const Vec3& b) {
  return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
Vec3 toLocal(const State& pose, const Vec3& world) {
  const auto v = subtract(world, pose.pos);
  return {pose.rot[0]*v[0] + pose.rot[4]*v[1] + pose.rot[8]*v[2],
          pose.rot[1]*v[0] + pose.rot[5]*v[1] + pose.rot[9]*v[2],
          pose.rot[2]*v[0] + pose.rot[6]*v[1] + pose.rot[10]*v[2]};
}
}
std::optional<CorePair> closestPointSegmentCores(const CollisionCore& a, const State& pa,
                                                  const CollisionCore& b, const State& pb) {
  const bool ap = point(a), bp = point(b);
  if (!(ap && bp) && !(ap && segment(b)) && !(bp && segment(a))) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  Vec3 localA = a.vertices[0].position, localB = b.vertices[0].position;
  if (!ap || !bp) {
    const auto& c = ap ? b : a;
    const auto& ps = ap ? pb : pa;
    const auto& pp = ap ? pa : pb;
    const auto& p = ap ? localA : localB;
    const auto& edge = c.edges[0];
    const auto& start = c.vertices[edge.vertices[0]].position;
    const auto& end = c.vertices[edge.vertices[1]].position;
    const auto relative = subtract(toLocal(ps, transformPoint(pp.pos, pp.rot, p)), start);
    float t = dot(relative, edge.direction);
    const float length = dot(subtract(end, start), edge.direction);
    Vec3 closest = start;
    if (t > length) closest = end;
    else if (t >= 0) for (int i = 0; i < 3; ++i) closest[i] += t * edge.direction[i];
    if (ap) localB = closest; else localA = closest;
  }
  CorePair result;
  result.pointA = localA;
  result.pointB = localB;
  result.normal = ap ? subtract(transformPoint(pa.pos, pa.rot, localA), transformPoint(pb.pos, pb.rot, localB))
                     : subtract(transformPoint(pb.pos, pb.rot, localB), transformPoint(pa.pos, pa.rot, localA));
  result.distance = ap && bp
      ? std::sqrt(result.normal[0]*result.normal[0] + result.normal[1]*result.normal[1] + result.normal[2]*result.normal[2])
      : std::sqrt(result.normal[0]*result.normal[0] +
                              result.normal[2]*result.normal[2] +
                              result.normal[1]*result.normal[1]);
  if (result.distance > 0) {
    const float inverse = 1.0f / result.distance;
    for (auto& v : result.normal) v *= inverse;
  }
  if (!ap) for (auto& v : result.normal) v = -v;
  return result;
}
}

namespace physics {
std::optional<CorePair> closestPointPolyhedron(const State& pointPose,
    const CollisionCore& core, const State& polyPose,
    std::optional<std::size_t>& cache, bool pointFirst) {
  if (core.features.empty() || core.vertices.empty() || core.faces.empty()) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  std::size_t index = cache.value_or(0);
  if (index >= core.features.size()) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  auto findFeature = [&](CoreFeatureRef ref) -> std::optional<std::size_t> {
    for (std::size_t i = 0; i < core.features.size(); ++i)
      if (core.features[i].ref.kind == ref.kind && core.features[i].ref.index == ref.index) return i;
    return std::nullopt;
  };
  const Vec3 p = toLocal(polyPose, pointPose.pos);
  for (std::size_t steps = 0; steps <= (core.features.size() + 1) * 2; ++steps) {
    const auto& f = core.features[index];
    float minimum = f.ref.kind == CoreFeatureKind::Face ? 1.0f : 0.0f;
    std::optional<std::size_t> next;
    for (const auto& a : f.adjacent) {
      if (a.anchorVertex >= core.vertices.size()) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      const auto v = subtract(p, core.vertices[a.anchorVertex].position);
      // The stock tests use y + z + x for edge/face and y + x + z for vertex.
      const float d = f.ref.kind == CoreFeatureKind::Vertex
          ? v[1]*a.normal[1] + v[0]*a.normal[0] + v[2]*a.normal[2]
          : v[1]*a.normal[1] + v[2]*a.normal[2] + v[0]*a.normal[0];
      if (d < minimum) {
        minimum = d; next = findFeature(a.neighbor);
        if (!next) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
        if (f.ref.kind != CoreFeatureKind::Face) break;
      }
    }
    if (next && minimum < 0) { index = *next; continue; }
    CorePair result;
    const Vec3 origin{};
    if (f.ref.kind == CoreFeatureKind::Vertex) {
      if (f.ref.index >= core.vertices.size()) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      result.pointB = core.vertices[f.ref.index].position;
      result.normal = pointFirst
          ? subtract(pointPose.pos, transformPoint(polyPose.pos, polyPose.rot, result.pointB))
          : subtract(transformPoint(polyPose.pos, polyPose.rot, result.pointB), pointPose.pos);
      result.distance = std::sqrt(result.normal[2]*result.normal[2] + result.normal[1]*result.normal[1] + result.normal[0]*result.normal[0]);
      if (result.distance > 0) { const float inv = 1.0f/result.distance; for (auto& v: result.normal) v *= inv; }
    } else if (f.ref.kind == CoreFeatureKind::Edge) {
      if (f.ref.index >= core.edges.size()) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      const auto& edge = core.edges[f.ref.index];
      if (edge.vertices[0] >= core.vertices.size() || edge.vertices[1] >= core.vertices.size()) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      CollisionCore c = segmentCore(core.vertices[edge.vertices[0]].position, core.vertices[edge.vertices[1]].position);
      c.edges[0].direction = edge.direction;
      auto pair = closestPointSegmentCores(pointCore(), pointPose, c, polyPose);
      if (!pair) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      result = *pair;
    } else {
      if (f.ref.index >= core.faces.size() || core.faces[f.ref.index].vertices.empty()) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      const auto& face = core.faces[f.ref.index];
      if (face.vertices[0] >= core.vertices.size()) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      const float d = dot(subtract(p, core.vertices[face.vertices[0]].position), face.normal);
      if (d <= 0) {
        float best = -3.402823466e38f;
        std::optional<std::size_t> bestIndex;
        for (std::size_t i = 0; i < core.features.size(); ++i) {
          const auto ref = core.features[i].ref;
          if (ref.kind != CoreFeatureKind::Face || ref.index >= core.faces.size()) continue;
          const auto& candidate = core.faces[ref.index];
          if (candidate.vertices.empty() || candidate.vertices[0] >= core.vertices.size()) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
          const float v = dot(subtract(p, core.vertices[candidate.vertices[0]].position), candidate.normal);
          if (v > best) { best = v; bestIndex = i; }
        }
        if (!bestIndex) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
        if (*bestIndex != index) { index = *bestIndex; continue; }
      }
      result.distance = d;
      for (int i=0;i<3;++i) result.pointB[i] = p[i] - d*face.normal[i];
      result.normal = matrixTransformRows(polyPose.rot, face.normal);
    }
    result.pointA = origin;
    cache = index;
    if (!pointFirst) {
      std::swap(result.pointA, result.pointB);
      if (f.ref.kind != CoreFeatureKind::Vertex) for (auto& v : result.normal) v = -v;
    }
    return result;
  }
  return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
}
}

namespace physics {
namespace {
Vec3 cross3(const Vec3& a, const Vec3& b) {
  return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
struct SegmentClip {
  float low = 0, high = 1;
  std::optional<std::size_t> lowNeighbor, highNeighbor;
};
std::optional<std::size_t> featureIndex(const CollisionCore& c, CoreFeatureRef r) {
  for (std::size_t i=0;i<c.features.size();++i)
    if(c.features[i].ref.kind==r.kind && c.features[i].ref.index==r.index) return i;
  return std::nullopt;
}
SegmentClip clipToFeature(const CollisionCore& c, const CoreFeature& f, const Vec3& start,
                          const Vec3& end, std::size_t first, std::size_t last,
                          float low = 0, float high = 1) {
  SegmentClip clip; clip.low=low; clip.high=high;
  for (std::size_t i=first;i<last;++i) {
    const auto& adj=f.adjacent[i];
    const auto& anchor=c.vertices[adj.anchorVertex].position;
    const auto x=subtract(start,anchor), y=subtract(end,anchor);
    const float a=x[2]*adj.normal[2]+x[1]*adj.normal[1]+x[0]*adj.normal[0];
    const float b=y[2]*adj.normal[2]+y[1]*adj.normal[1]+y[0]*adj.normal[0];
    auto neighbor=featureIndex(c,adj.neighbor);
    if (a>=0) {
      if (b<0) { float t=a/(a-b); if(t<clip.high) {clip.high=t;clip.highNeighbor=neighbor;} }
    } else {
      if(b<0) {clip.lowNeighbor=neighbor;clip.highNeighbor=neighbor;return clip;}
      float t=a/(a-b); if(t>clip.low) {clip.low=t;clip.lowNeighbor=neighbor;}
    }
    if(clip.high<clip.low) break;
  }
  return clip;
}
Vec3 along(const Vec3& a,const Vec3& b,float t) {
  return {a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1]),a[2]+t*(b[2]-a[2])};
}
}
std::optional<CorePair> closestSegmentCores(const CollisionCore& a,const State& pa,
    const CollisionCore& b,const State& pb,std::optional<std::size_t>& ca,
    std::optional<std::size_t>& cb) {
  if(!segment(a)||!segment(b)||a.features.size()!=3||b.features.size()!=3) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  const CollisionCore* c[]={&a,&b}; const State* pose[]={&pa,&pb};
  std::size_t f[]={ca.value_or(0),cb.value_or(0)};
  if(f[0]>=3||f[1]>=3) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  auto world=[&](int side,const Vec3& v){return transformPoint(pose[side]->pos,pose[side]->rot,v);};
  auto convert=[&](int side,int other,const Vec3& v){return toLocal(*pose[side],world(other,v));};
  auto anchor=[&](int side)->Vec3 { const auto ref=c[side]->features[f[side]].ref;
    return c[side]->vertices[ref.kind==CoreFeatureKind::Vertex?ref.index:c[side]->edges[ref.index].vertices[0]].position; };
  auto end=[&](int side)->Vec3 { const auto ref=c[side]->features[f[side]].ref;
    return c[side]->vertices[c[side]->edges[ref.index].vertices[1]].position; };
  for(int steps=0;steps<12;++steps) {
    const auto k0=c[0]->features[f[0]].ref.kind, k1=c[1]->features[f[1]].ref.kind;
    if(k0==CoreFeatureKind::Face||k1==CoreFeatureKind::Face) return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
    if(k0==CoreFeatureKind::Vertex&&k1==CoreFeatureKind::Vertex) {
      bool moved=false;
      for(int s=0;s<2&&!moved;++s) {
        const auto& feature=c[s]->features[f[s]];
        const Vec3 p=convert(s,1-s,anchor(1-s));
        for(const auto& adj:feature.adjacent) if(dot(subtract(p,c[s]->vertices[adj.anchorVertex].position),adj.normal)<0) {
          auto n=featureIndex(*c[s],adj.neighbor);if(!n)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);f[s]=*n;moved=true;break;
        }
      }
      if(moved) continue;
      CorePair r; r.pointA=anchor(0);r.pointB=anchor(1);
      r.normal=subtract(world(0,r.pointA),world(1,r.pointB));
      r.distance=std::sqrt(r.normal[0]*r.normal[0]+r.normal[1]*r.normal[1]+r.normal[2]*r.normal[2]);
      if(r.distance>0){float inv=1/r.distance;for(auto& x:r.normal)x*=inv;}
      ca=f[0];cb=f[1];return r;
    }
    if(k0!=k1) {
      int v=k0==CoreFeatureKind::Vertex?0:1, e=1-v;
      const Vec3 p=convert(e,v,anchor(v)); const auto& ef=c[e]->features[f[e]];
      bool moved=false;
      for(const auto& adj:ef.adjacent) if(dot(subtract(p,c[e]->vertices[adj.anchorVertex].position),adj.normal)<0) {
        auto n=featureIndex(*c[e],adj.neighbor);if(!n)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);f[e]=*n;moved=true;break;
      }
      if(moved)continue;
      const Vec3 x=convert(v,e,anchor(e)),y=convert(v,e,end(e)),delta=subtract(y,x);
      const auto& vf=c[v]->features[f[v]];
      auto clip=clipToFeature(*c[v],vf,x,y,0,vf.adjacent.size());
      if(clip.lowNeighbor==clip.highNeighbor&&clip.lowNeighbor){f[v]=*clip.lowNeighbor;continue;}
      if(clip.lowNeighbor && dot(subtract(along(x,y,clip.low),anchor(v)),delta)>0){f[v]=*clip.lowNeighbor;continue;}
      if(clip.highNeighbor && dot(subtract(along(x,y,clip.high),anchor(v)),delta)<0){f[v]=*clip.highNeighbor;continue;}
      CollisionCore pointOnly=pointCore(); pointOnly.vertices[0].position=anchor(v);
      auto r=v==0?closestPointSegmentCores(pointOnly,*pose[v],*c[e],*pose[e])
                 :closestPointSegmentCores(*c[e],*pose[e],pointOnly,*pose[v]);
      if(!r)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);ca=f[0];cb=f[1];return r;
    }
    bool moved=false;
    for(int side=0;side<2&&!moved;++side) {
      int other=1-side; const Vec3 x=convert(side,other,anchor(other)),y=convert(side,other,end(other));
      const Vec3 delta=subtract(y,x);const auto& feature=c[side]->features[f[side]];
      auto clip=clipToFeature(*c[side],feature,x,y,0,2);
      if(clip.lowNeighbor==clip.highNeighbor&&clip.lowNeighbor){f[side]=*clip.lowNeighbor;moved=true;break;}
      if(clip.lowNeighbor&&dot(subtract(along(x,y,clip.low),c[side]->vertices[feature.ref.index==0?c[side]->edges[0].vertices[0]:0].position),delta)>0){f[side]=*clip.lowNeighbor;moved=true;break;}
      if(clip.highNeighbor&&dot(subtract(along(x,y,clip.high),c[side]->vertices[c[side]->edges[0].vertices[1]].position),delta)<0){f[side]=*clip.highNeighbor;moved=true;break;}
    }
    if(moved)continue;
    Vec3 a0=world(0,anchor(0)),a1=world(0,end(0)),b0=world(1,anchor(1)),b1=world(1,end(1));
    Vec3 da=matrixTransformRows(pa.rot,a.edges[0].direction),db=matrixTransformRows(pb.rot,b.edges[0].direction);
    Vec3 n=cross3(da,db);
    if(dot(n,n)==0) {
      const Vec3 offset=subtract(b0,a0), lastOffset=subtract(b1,a0), span=subtract(a1,a0);
      const float t0=dot(offset,span), t1=dot(lastOffset,span);
      CorePair r;r.pointA=toLocal(pa,a0);r.pointB=toLocal(pb,b0);
      r.normal=cross3(cross3(lastOffset,da),da);
      const float length=std::sqrt(r.normal[2]*r.normal[2]+r.normal[1]*r.normal[1]+r.normal[0]*r.normal[0]);
      if(length!=0){const float inv=1/length;for(auto& x:r.normal)x*=inv;}
      Vec3 difference{};bool separated=false;
      if(t0>1){if(t0<t1){difference=subtract(a1,b0);separated=true;}else if(t1>1){difference=subtract(a1,b1);separated=true;}}
      else if(t0<0){if(t1<t0){difference=subtract(a0,b0);separated=true;}else if(t1<0){difference=subtract(a0,b1);separated=true;}}
      r.distance=separated?std::sqrt(dot(difference,difference)):0;
      ca=f[0];cb=f[1];return r;
    }
    Vec3 diff=subtract(b0,a0), na=cross3(n,db), nb=cross3(n,da);
    float ta=(na[2]*diff[2]+na[1]*diff[1]+na[0]*diff[0])/dot(na,da);
    float tb=-(nb[2]*diff[2]+nb[1]*diff[1]+nb[0]*diff[0])/(nb[2]*db[2]+nb[1]*db[1]+nb[0]*db[0]);
    float la=dot(subtract(a1,a0),da),lb=dot(subtract(b1,b0),db);
    float measureA=ta*da[0]*da[0]+ta*da[2]*da[2]+ta*da[1]*da[1];
    float measureB=tb*db[1]*db[1]+tb*db[2]*db[2]+tb*db[0]*db[0];
    Vec3 wa={a0[0]+ta*da[0],a0[1]+ta*da[1],a0[2]+ta*da[2]};
    Vec3 wb={b0[0]+tb*db[0],b0[1]+tb*db[1],b0[2]+tb*db[2]};
    if(measureA<0)wa=a0;else if(la<measureA)for(int i=0;i<3;++i)wa[i]=a0[i]+(a1[i]-a0[i]);
    if(measureB<0)wb=b0;else if(lb<measureB)for(int i=0;i<3;++i)wb[i]=b0[i]+(b1[i]-b0[i]);
    if(dot(n,diff)>0)for(auto& v:n)v=-v;
    CorePair r;
    const auto va=subtract(wa,pa.pos),vb=subtract(wb,pb.pos);
    r.pointA={pa.rot[0]*va[0]+pa.rot[4]*va[1]+pa.rot[8]*va[2],
              pa.rot[5]*va[1]+pa.rot[1]*va[0]+pa.rot[9]*va[2],
              pa.rot[6]*va[1]+pa.rot[2]*va[0]+pa.rot[10]*va[2]};
    r.pointB={pb.rot[0]*vb[0]+pb.rot[8]*vb[2]+pb.rot[4]*vb[1],
              pb.rot[1]*vb[0]+pb.rot[9]*vb[2]+pb.rot[5]*vb[1],
              pb.rot[2]*vb[0]+pb.rot[10]*vb[2]+pb.rot[6]*vb[1]};
    Vec3 distance=subtract(wa,wb);r.distance=std::sqrt(dot(distance,distance));
    float length=std::sqrt(n[2]*n[2]+n[1]*n[1]+n[0]*n[0]);
    if(length!=0){const float inverse=1/length;for(int i=0;i<3;++i)r.normal[i]=n[i]*inverse;}
    ca=f[0];cb=f[1];return r;
  }
  return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
}
}

namespace physics {
std::optional<CorePair> closestSegmentPolyhedron(const CollisionCore& seg,const State& ps,
    const CollisionCore& poly,const State& pp,std::optional<std::size_t>& cs,
    std::optional<std::size_t>& cp,bool segmentFirst) {
  if(!segment(seg)||seg.features.size()!=3||poly.faces.empty()||poly.features.empty())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  const CollisionCore* c[]={&seg,&poly};const State* pose[]={&ps,&pp};
  std::size_t f[]={cs.value_or(0),cp.value_or(0)};
  if(f[0]>=seg.features.size()||f[1]>=poly.features.size())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  auto world=[&](int s,const Vec3& v){return transformPoint(pose[s]->pos,pose[s]->rot,v);};
  auto convert=[&](int s,int other,const Vec3& v){return toLocal(*pose[s],world(other,v));};
  auto valid=[&](int s){auto r=c[s]->features[f[s]].ref;
    if(r.kind==CoreFeatureKind::Vertex)return r.index<c[s]->vertices.size();
    if(r.kind==CoreFeatureKind::Edge)return r.index<c[s]->edges.size()&&c[s]->edges[r.index].vertices[0]<c[s]->vertices.size()&&c[s]->edges[r.index].vertices[1]<c[s]->vertices.size();
    return r.index<c[s]->faces.size()&&!c[s]->faces[r.index].vertices.empty()&&c[s]->faces[r.index].vertices[0]<c[s]->vertices.size();};
  auto anchor=[&](int s)->Vec3 {auto r=c[s]->features[f[s]].ref;
    auto i=r.kind==CoreFeatureKind::Vertex?r.index:r.kind==CoreFeatureKind::Edge?c[s]->edges[r.index].vertices[0]:c[s]->faces[r.index].vertices[0];return c[s]->vertices[i].position;};
  auto end=[&](int s)->Vec3 {return c[s]->vertices[c[s]->edges[c[s]->features[f[s]].ref.index].vertices[1]].position;};
  auto finish=[&](CorePair r){cs=f[0];cp=f[1];if(!segmentFirst){std::swap(r.pointA,r.pointB);for(auto& v:r.normal)v=-v;}return std::optional<CorePair>(r);};
  for(std::size_t steps=0;steps<2*(seg.features.size()+poly.features.size());++steps) {
    if(!valid(0)||!valid(1))return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
    auto k0=c[0]->features[f[0]].ref.kind,k1=c[1]->features[f[1]].ref.kind;
    if(k0==CoreFeatureKind::Face)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
    for(int s=0;s<2;++s)for(const auto& x:c[s]->features[f[s]].adjacent)
      if(x.anchorVertex>=c[s]->vertices.size()||!featureIndex(*c[s],x.neighbor))return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
    if(k0==CoreFeatureKind::Vertex&&k1==CoreFeatureKind::Vertex) {
      bool moved=false;
      for(int s=0;s<2&&!moved;++s){const auto p=convert(s,1-s,anchor(1-s));
        for(const auto& adj:c[s]->features[f[s]].adjacent)
          if(dot(subtract(p,c[s]->vertices[adj.anchorVertex].position),adj.normal)<0){f[s]=*featureIndex(*c[s],adj.neighbor);moved=true;break;}}
      if(moved)continue;
      CorePair r;r.pointA=anchor(0);r.pointB=anchor(1);
      r.normal=segmentFirst?subtract(world(0,r.pointA),world(1,r.pointB)):subtract(world(1,r.pointB),world(0,r.pointA));
      r.distance=std::sqrt(r.normal[0]*r.normal[0]+r.normal[1]*r.normal[1]+r.normal[2]*r.normal[2]);
      if(r.distance>0){const float inv=1/r.distance;for(auto& x:r.normal)x*=inv;}
      if(!segmentFirst)for(auto& x:r.normal)x=-x;
      return finish(r);
    }
    if((k0==CoreFeatureKind::Vertex&&k1==CoreFeatureKind::Edge)||(k0==CoreFeatureKind::Edge&&k1==CoreFeatureKind::Vertex)) {
      int v=k0==CoreFeatureKind::Vertex?0:1,e=1-v;const Vec3 p=convert(e,v,anchor(v));
      bool moved=false;
      for(const auto& adj:c[e]->features[f[e]].adjacent)if(dot(subtract(p,c[e]->vertices[adj.anchorVertex].position),adj.normal)<0){f[e]=*featureIndex(*c[e],adj.neighbor);moved=true;break;}
      if(moved)continue;
      Vec3 x=convert(v,e,anchor(e)),y=convert(v,e,end(e)),delta=subtract(y,x);
      const auto& vf=c[v]->features[f[v]];auto clip=clipToFeature(*c[v],vf,x,y,0,vf.adjacent.size());
      if(clip.lowNeighbor==clip.highNeighbor&&clip.lowNeighbor){f[v]=*clip.lowNeighbor;continue;}
      if(clip.lowNeighbor&&dot(subtract(along(x,y,clip.low),anchor(v)),delta)>0){f[v]=*clip.lowNeighbor;continue;}
      if(clip.highNeighbor&&dot(subtract(along(x,y,clip.high),anchor(v)),delta)<0){f[v]=*clip.highNeighbor;continue;}
      CollisionCore pcore=pointCore();pcore.vertices[0].position=anchor(v);
      auto r=v==0?closestPointSegmentCores(pcore,*pose[v],*c[e],*pose[e]):std::optional<CorePair>{};
      // 004ace20 needs only the selected edge, not the entire polyhedron.
      if(v==0){const auto& edge=c[e]->edges[c[e]->features[f[e]].ref.index];
        auto edgeCore=segmentCore(anchor(e),end(e));edgeCore.edges[0].direction=edge.direction;
        r=closestPointSegmentCores(pcore,*pose[v],edgeCore,*pose[e]);
      }else{const auto& edge=c[e]->edges[c[e]->features[f[e]].ref.index];
        auto edgeCore=segmentCore(anchor(e),end(e));edgeCore.edges[0].direction=edge.direction;
        r=closestPointSegmentCores(edgeCore,*pose[e],pcore,*pose[v]);}
      if(!r)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);return finish(*r);
    }
    if(k0==CoreFeatureKind::Vertex&&k1==CoreFeatureKind::Face) {
      const Vec3 p=convert(1,0,anchor(0));auto face=poly.faces[poly.features[f[1]].ref.index];
      float min=1;std::optional<std::size_t> next;
      for(const auto& adj:poly.features[f[1]].adjacent){float d=dot(subtract(p,poly.vertices[adj.anchorVertex].position),adj.normal);if(d<min){min=d;next=featureIndex(poly,adj.neighbor);}}
      if(min<0&&next){f[1]=*next;continue;}
      float d=dot(subtract(p,anchor(1)),face.normal);
      const auto& vertex=seg.vertices[seg.features[f[0]].ref.index];
      bool movedVertex=false;
      for(auto ei:vertex.edges){const auto& e=seg.edges[ei];auto other=e.vertices[0]==seg.features[f[0]].ref.index?e.vertices[1]:e.vertices[0];
        if(other>=seg.vertices.size())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
        float d2=dot(subtract(convert(1,0,seg.vertices[other].position),anchor(1)),face.normal);
        if(std::fabs(d2)<std::fabs(d)||d2*d<0){auto n=featureIndex(seg,{CoreFeatureKind::Edge,ei});if(!n)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);f[0]=*n;movedVertex=true;break;}}
      if(movedVertex)continue;
      if(d<=0){float best=-3.402823466e38f;std::optional<std::size_t> bestIndex;
        for(std::size_t i=0;i<poly.features.size();++i){auto r=poly.features[i].ref;if(r.kind!=CoreFeatureKind::Face||r.index>=poly.faces.size()||poly.faces[r.index].vertices.empty())continue;
          const auto& q=poly.faces[r.index];if(q.vertices[0]>=poly.vertices.size())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
          float dist=dot(subtract(p,poly.vertices[q.vertices[0]].position),q.normal);if(dist>best){best=dist;bestIndex=i;}}
        if(!bestIndex)return corePairUnavailable(PairSiteFile::CorePair,__LINE__); f[1]=*bestIndex; if(best>0)continue; face=poly.faces[poly.features[f[1]].ref.index]; d=best;}
      CorePair r;r.pointA=anchor(0);r.distance=d;
      for(int i=0;i<3;++i)r.pointB[i]=p[i]-d*face.normal[i];r.normal=matrixTransformRows(pp.rot,face.normal);return finish(r);
    }
    if(k0==CoreFeatureKind::Edge&&k1==CoreFeatureKind::Face) {
      const Vec3 x=convert(1,0,anchor(0)),y=convert(1,0,end(0));const auto& ff=poly.features[f[1]];
      auto clip=clipToFeature(poly,ff,x,y,0,ff.adjacent.size());
      if(clip.lowNeighbor || clip.highNeighbor) {
        if(clip.lowNeighbor==clip.highNeighbor || clip.high<clip.low) {
          // 004ae300 follows the face boundary from the first rejecting edge.
          const auto first=clip.lowNeighbor;
          if(!first||ff.adjacent.empty())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
          std::size_t current=0;
          while(current<ff.adjacent.size()&&featureIndex(poly,ff.adjacent[current].neighbor)!=first)++current;
          if(current==ff.adjacent.size())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
          std::optional<std::size_t> previous;
          bool rejected=false, accepted=false;
          std::optional<std::size_t> selected;
          for(std::size_t iterations=0;iterations<=ff.adjacent.size()*2;++iterations) {
            const auto currentEdge=featureIndex(poly,ff.adjacent[current].neighbor);
            if(!currentEdge||poly.features[*currentEdge].ref.kind!=CoreFeatureKind::Edge)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
            auto plane=clipToFeature(poly,ff,x,y,current,current+1);
            std::size_t next=(current+1)%ff.adjacent.size();
            if(!plane.lowNeighbor&&!plane.highNeighbor) {
              if(accepted){if(!previous)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);selected=featureIndex(poly,ff.adjacent[*previous].neighbor);break;}
              if(rejected){if(!previous)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);selected=featureIndex(poly,ff.adjacent[*previous].neighbor);break;}
            } else {
              rejected=true;
              float lo=0,hi=1;
              if(plane.lowNeighbor!=plane.highNeighbor) {
                if(!plane.lowNeighbor&&plane.highNeighbor)lo=plane.high;
                else if(plane.lowNeighbor)hi=plane.low;
              }
              const auto& edgeFeature=poly.features[*currentEdge];
              if(edgeFeature.adjacent.size()<2)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
              auto ends=clipToFeature(poly,edgeFeature,x,y,0,2,lo,hi);
              auto shares=[&](std::size_t index,std::optional<std::size_t> vertex) {
                auto e=featureIndex(poly,ff.adjacent[index].neighbor);
                if(!e||!vertex)return false;
                const auto& adj=poly.features[*e].adjacent;
                return adj.size()>=2&&(featureIndex(poly,adj[0].neighbor)==vertex||featureIndex(poly,adj[1].neighbor)==vertex);
              };
              auto steer=[&](std::optional<std::size_t> vertex){if(!shares(next,vertex))next=current?current-1:ff.adjacent.size()-1;};
              if(ends.lowNeighbor&&ends.lowNeighbor==ends.highNeighbor)steer(ends.lowNeighbor);
              else if(ends.high<ends.low) {
                if(accepted){if(!previous)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);selected=featureIndex(poly,ff.adjacent[*previous].neighbor);break;}
                steer(ends.lowNeighbor?ends.lowNeighbor:ends.highNeighbor);
              } else {
                accepted=true;
                auto gradient=[&](std::optional<std::size_t> v,float t){
                  if(!v)return 0.0f;auto ref=poly.features[*v].ref;
                  if(ref.kind!=CoreFeatureKind::Vertex||ref.index>=poly.vertices.size())return 0.0f;
                  return dot(subtract(along(x,y,t),poly.vertices[ref.index].position),subtract(y,x));
                };
                if(ends.lowNeighbor&&gradient(ends.lowNeighbor,ends.low)>0)steer(ends.lowNeighbor);
                else if(!ends.highNeighbor||gradient(ends.highNeighbor,ends.high)>=0){selected=currentEdge;break;}
                else steer(ends.highNeighbor);
              }
            }
            if(previous&&next==*previous){selected=featureIndex(poly,ff.adjacent[*previous].neighbor);break;}
            previous=current;current=next;
          }
          if(!selected)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
          f[1]=*selected;continue;
        }
      }
      const auto& face=poly.faces[ff.ref.index];const Vec3 delta=subtract(y,x);
      const float dl=dot(subtract(along(x,y,clip.low),anchor(1)),face.normal);
      const float dh=dot(subtract(along(x,y,clip.high),anchor(1)),face.normal);
      if(dl*dh<0) {
        const auto offset=subtract(x,anchor(1));
        const float d0=offset[2]*face.normal[2]+offset[1]*face.normal[1]+offset[0]*face.normal[0];
        const auto lastOffset=subtract(y,anchor(1));
        const float d1=lastOffset[2]*face.normal[2]+lastOffset[1]*face.normal[1]+lastOffset[0]*face.normal[0];
        const float denominator=delta[2]*face.normal[2]+delta[1]*face.normal[1]+delta[0]*face.normal[0];
        const float t=-d0/denominator;
        CorePair r;for(int i=0;i<3;++i)r.pointB[i]=t*delta[i]+(x[i]-anchor(1)[i])+anchor(1)[i];
        r.pointA=toLocal(ps,world(1,r.pointB));
        r.normal=matrixTransformRows(pp.rot,face.normal);
        r.distance=d0>0?d1:d0;
        return finish(r);
      }
      if(dot(delta,face.normal)*dl<0){if(clip.highNeighbor)f[1]=*clip.highNeighbor;else f[0]=*featureIndex(seg,{CoreFeatureKind::Vertex,seg.edges[0].vertices[1]});}
      else {if(clip.lowNeighbor)f[1]=*clip.lowNeighbor;else f[0]=*featureIndex(seg,{CoreFeatureKind::Vertex,seg.edges[0].vertices[0]});}
      continue;
    }
    if(k0==CoreFeatureKind::Edge&&k1==CoreFeatureKind::Edge) {
      bool moved=false;
      for(int side=0;side<2&&!moved;++side) {
        int other=1-side;Vec3 x=convert(side,other,anchor(other)),y=convert(side,other,end(other)),delta=subtract(y,x);
        const auto& feature=c[side]->features[f[side]];
        if(feature.adjacent.size()<2)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
        auto clip=clipToFeature(*c[side],feature,x,y,0,2);
        if(clip.lowNeighbor==clip.highNeighbor&&clip.lowNeighbor){f[side]=*clip.lowNeighbor;moved=true;break;}
        auto checkVertex=[&](std::optional<std::size_t> n,float t,bool low){
          if(!n)return false;const auto ref=c[side]->features[*n].ref;
          if(ref.kind!=CoreFeatureKind::Vertex||ref.index>=c[side]->vertices.size())return false;
          float d=dot(subtract(along(x,y,t),c[side]->vertices[ref.index].position),delta);
          return low?d>0:d<0;};
        if(checkVertex(clip.lowNeighbor,clip.low,true)){f[side]=*clip.lowNeighbor;moved=true;break;}
        if(checkVertex(clip.highNeighbor,clip.high,false)){f[side]=*clip.highNeighbor;moved=true;break;}
        if(feature.adjacent.size()>2){
          clip=clipToFeature(*c[side],feature,x,y,2,feature.adjacent.size());
          if(clip.lowNeighbor==clip.highNeighbor&&clip.lowNeighbor){f[side]=*clip.lowNeighbor;moved=true;break;}
          auto checkFace=[&](std::optional<std::size_t> n,float t,bool low){
            if(!n)return false;const auto ref=c[side]->features[*n].ref;
            if(ref.kind!=CoreFeatureKind::Face||ref.index>=c[side]->faces.size())return false;
            const auto& face=c[side]->faces[ref.index];if(face.vertices.empty()||face.vertices[0]>=c[side]->vertices.size())return false;
            float d=dot(delta,face.normal)*dot(subtract(along(x,y,t),c[side]->vertices[face.vertices[0]].position),face.normal);
            return low?d>0:d<0;};
          if(checkFace(clip.lowNeighbor,clip.low,true)){f[side]=*clip.lowNeighbor;moved=true;break;}
          if(checkFace(clip.highNeighbor,clip.high,false)){f[side]=*clip.highNeighbor;moved=true;break;}
        }
      }
      if(moved)continue;
      const auto& edge=poly.edges[poly.features[f[1]].ref.index];
      auto temp=segmentCore(anchor(1),end(1));temp.edges[0].direction=edge.direction;
      std::optional<std::size_t> sa=2,sb=2;
      auto r=segmentFirst ? closestSegmentCores(seg,ps,temp,pp,sa,sb)
                          : closestSegmentCores(temp,pp,seg,ps,sb,sa);
      if(!r||*sa!=2||*sb!=2)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      cs=f[0];cp=f[1];return r;
    }
    // The remaining clipped face cases have not yet been translated.
    return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  }
  return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
}
}

namespace physics {
std::optional<CorePair> closestPolyhedronCores(const CollisionCore& a,const State& pa,
    const CollisionCore& b,const State& pb,std::optional<std::size_t>& ca,
    std::optional<std::size_t>& cb) {
  if(a.faces.empty()||b.faces.empty()||a.features.empty()||b.features.empty())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  const CollisionCore* c[]={&a,&b};const State* pose[]={&pa,&pb};
  std::size_t f[]={ca.value_or(0),cb.value_or(0)};
  if(f[0]>=a.features.size()||f[1]>=b.features.size())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  auto valid=[&](int side) {const auto& core=*c[side];auto ref=core.features[f[side]].ref;
    if(ref.kind==CoreFeatureKind::Vertex)return ref.index<core.vertices.size();
    if(ref.kind==CoreFeatureKind::Edge)return ref.index<core.edges.size()&&core.edges[ref.index].vertices[0]<core.vertices.size()&&core.edges[ref.index].vertices[1]<core.vertices.size();
    return ref.index<core.faces.size()&&!core.faces[ref.index].vertices.empty()&&core.faces[ref.index].vertices[0]<core.vertices.size();};
  auto anchor=[&](int side)->Vec3{const auto& core=*c[side];auto ref=core.features[f[side]].ref;
    auto i=ref.kind==CoreFeatureKind::Vertex?ref.index:ref.kind==CoreFeatureKind::Edge?core.edges[ref.index].vertices[0]:core.faces[ref.index].vertices[0];return core.vertices[i].position;};
  auto end=[&](int side)->Vec3{const auto& core=*c[side];return core.vertices[core.edges[core.features[f[side]].ref.index].vertices[1]].position;};
  auto world=[&](int side,const Vec3& v){return transformPoint(pose[side]->pos,pose[side]->rot,v);};
  auto convert=[&](int side,int other,const Vec3& v){return toLocal(*pose[side],world(other,v));};
  auto finish=[&](CorePair r){ca=f[0];cb=f[1];return std::optional<CorePair>(r);};
  int orientation=0;
  for(std::size_t steps=0;steps<=a.features.size()+2*b.features.size();++steps) {
    if(!valid(0)||!valid(1))return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
    for(int s=0;s<2;++s)for(const auto& x:c[s]->features[f[s]].adjacent)
      if(x.anchorVertex>=c[s]->vertices.size()||!featureIndex(*c[s],x.neighbor))return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
    auto k0=c[0]->features[f[0]].ref.kind,k1=c[1]->features[f[1]].ref.kind;
    if(k0==CoreFeatureKind::Face&&k1==CoreFeatureKind::Face){
      if(c[0]->features[f[0]].adjacent.empty())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      f[0]=*featureIndex(a,c[0]->features[f[0]].adjacent[0].neighbor);continue;
    }
    if(k0==CoreFeatureKind::Vertex&&k1==CoreFeatureKind::Vertex){
      bool moved=false;for(int turn=0;turn<2&&!moved;++turn){int s=orientation^turn^1;Vec3 p=convert(s,1-s,anchor(1-s));
        for(const auto& adj:c[s]->features[f[s]].adjacent)if(dot(subtract(p,c[s]->vertices[adj.anchorVertex].position),adj.normal)<0){f[s]=*featureIndex(*c[s],adj.neighbor);moved=true;break;}}
      if(moved)continue;CorePair r;r.pointA=anchor(0);r.pointB=anchor(1);
      r.normal=subtract(world(0,r.pointA),world(1,r.pointB));
      r.distance=std::sqrt(r.normal[0]*r.normal[0]+r.normal[1]*r.normal[1]+r.normal[2]*r.normal[2]);
      if(r.distance>0){const float inv=1/r.distance;for(auto& v:r.normal)v*=inv;}return finish(r);
    }
    if((k0==CoreFeatureKind::Vertex&&k1==CoreFeatureKind::Edge)||(k0==CoreFeatureKind::Edge&&k1==CoreFeatureKind::Vertex)){
      int v=k0==CoreFeatureKind::Vertex?0:1,e=1-v;orientation=v;Vec3 point=convert(e,v,anchor(v));bool moved=false;
      for(const auto& adj:c[e]->features[f[e]].adjacent)if(dot(subtract(point,c[e]->vertices[adj.anchorVertex].position),adj.normal)<0){f[e]=*featureIndex(*c[e],adj.neighbor);moved=true;break;}
      if(moved)continue;
      Vec3 x=convert(v,e,anchor(e)),y=convert(v,e,end(e)),delta=subtract(y,x);
      const auto& vf=c[v]->features[f[v]];auto clip=clipToFeature(*c[v],vf,x,y,0,vf.adjacent.size());
      if(clip.lowNeighbor==clip.highNeighbor&&clip.lowNeighbor){f[v]=*clip.lowNeighbor;continue;}
      if(clip.lowNeighbor&&dot(subtract(along(x,y,clip.low),anchor(v)),delta)>0){f[v]=*clip.lowNeighbor;continue;}
      if(clip.highNeighbor&&dot(subtract(along(x,y,clip.high),anchor(v)),delta)<0){f[v]=*clip.highNeighbor;continue;}
      CollisionCore p=pointCore();p.vertices[0].position=anchor(v);auto edge=segmentCore(anchor(e),end(e));edge.edges[0].direction=c[e]->edges[c[e]->features[f[e]].ref.index].direction;
      auto r=v==0?closestPointSegmentCores(p,*pose[v],edge,*pose[e]):closestPointSegmentCores(edge,*pose[e],p,*pose[v]);
      if(!r)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);return finish(*r);
    }
    if((k0==CoreFeatureKind::Vertex&&k1==CoreFeatureKind::Face)||(k0==CoreFeatureKind::Face&&k1==CoreFeatureKind::Vertex)){
      int v=k0==CoreFeatureKind::Vertex?0:1,q=1-v;orientation=v;Vec3 point=convert(q,v,anchor(v));const auto& feature=c[q]->features[f[q]];
      float minimum=1;std::optional<std::size_t> next;
      for(const auto& adj:feature.adjacent){float d=dot(subtract(point,c[q]->vertices[adj.anchorVertex].position),adj.normal);if(d<minimum){minimum=d;next=featureIndex(*c[q],adj.neighbor);}}
      if(minimum<0&&next){f[q]=*next;continue;}
      CoreFace face=c[q]->faces[feature.ref.index];float d=dot(subtract(point,anchor(q)),face.normal);bool moved=false;
      const auto vi=c[v]->features[f[v]].ref.index;
      for(auto ei:c[v]->vertices[vi].edges){if(ei>=c[v]->edges.size())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);const auto& edge=c[v]->edges[ei];
        auto other=edge.vertices[0]==vi?edge.vertices[1]:edge.vertices[0];if(other>=c[v]->vertices.size())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
        float d2=dot(subtract(convert(q,v,c[v]->vertices[other].position),anchor(q)),face.normal);
        if(std::fabs(d2)<std::fabs(d)||d2*d<0){auto n=featureIndex(*c[v],{CoreFeatureKind::Edge,ei});if(!n)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);f[v]=*n;moved=true;break;}}
      if(moved)continue;
      if(d<=0){float best=-3.402823466e38f;std::optional<std::size_t> bestIndex;
        for(std::size_t i=0;i<c[q]->features.size();++i){auto ref=c[q]->features[i].ref;
          if(ref.kind!=CoreFeatureKind::Face||ref.index>=c[q]->faces.size())continue;
          const auto& candidate=c[q]->faces[ref.index];if(candidate.vertices.empty()||candidate.vertices[0]>=c[q]->vertices.size())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
          float value=dot(subtract(point,c[q]->vertices[candidate.vertices[0]].position),candidate.normal);
          if(value>best){best=value;bestIndex=i;}}
        if(!bestIndex)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);f[q]=*bestIndex;if(best>0)continue;face=c[q]->faces[c[q]->features[f[q]].ref.index];d=best;}
      CorePair r;r.distance=d;Vec3 projection;for(int i=0;i<3;++i)projection[i]=point[i]-d*face.normal[i];
      r.normal=matrixTransformRows(pose[q]->rot,face.normal);
      if(v==0){r.pointA=anchor(v);r.pointB=projection;}else{r.pointA=projection;r.pointB=anchor(v);for(auto& n:r.normal)n=-n;}
      return finish(r);
    }
    if(k0==CoreFeatureKind::Edge&&k1==CoreFeatureKind::Edge) {
      bool moved=false;
      for(int turn=0;turn<2&&!moved;++turn) {
        int side=orientation^turn;int other=1-side;Vec3 x=convert(side,other,anchor(other)),y=convert(side,other,end(other)),delta=subtract(y,x);
        const auto& feature=c[side]->features[f[side]];
        if(feature.adjacent.size()<2)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
        auto clip=clipToFeature(*c[side],feature,x,y,0,2);
        if(clip.lowNeighbor==clip.highNeighbor&&clip.lowNeighbor){f[side]=*clip.lowNeighbor;moved=true;break;}
        auto checkVertex=[&](std::optional<std::size_t> n,float t,bool low){
          if(!n)return false;const auto ref=c[side]->features[*n].ref;
          if(ref.kind!=CoreFeatureKind::Vertex||ref.index>=c[side]->vertices.size())return false;
          float d=dot(subtract(along(x,y,t),c[side]->vertices[ref.index].position),delta);
          return low?d>0:d<0;};
        if(checkVertex(clip.lowNeighbor,clip.low,true)){f[side]=*clip.lowNeighbor;moved=true;break;}
        if(checkVertex(clip.highNeighbor,clip.high,false)){f[side]=*clip.highNeighbor;moved=true;break;}
        if(feature.adjacent.size()>2){
          clip=clipToFeature(*c[side],feature,x,y,2,feature.adjacent.size());
          if(clip.lowNeighbor==clip.highNeighbor&&clip.lowNeighbor){f[side]=*clip.lowNeighbor;moved=true;break;}
          auto checkFace=[&](std::optional<std::size_t> n,float t,bool low){
            if(!n)return false;const auto ref=c[side]->features[*n].ref;
            if(ref.kind!=CoreFeatureKind::Face||ref.index>=c[side]->faces.size())return false;
            const auto& face=c[side]->faces[ref.index];if(face.vertices.empty()||face.vertices[0]>=c[side]->vertices.size())return false;
            float d=dot(delta,face.normal)*dot(subtract(along(x,y,t),c[side]->vertices[face.vertices[0]].position),face.normal);
            return low?d>0:d<0;};
          if(checkFace(clip.lowNeighbor,clip.low,true)){f[side]=*clip.lowNeighbor;moved=true;break;}
          if(checkFace(clip.highNeighbor,clip.high,false)){f[side]=*clip.highNeighbor;moved=true;break;}
        }
      }
      if(moved)continue;
      auto tempA=segmentCore(anchor(0),end(0));auto tempB=segmentCore(anchor(1),end(1));
      tempA.edges[0].direction=a.edges[a.features[f[0]].ref.index].direction;
      tempB.edges[0].direction=b.edges[b.features[f[1]].ref.index].direction;
      std::optional<std::size_t> sa=2,sb=2;
      auto r=closestSegmentCores(tempA,pa,tempB,pb,sa,sb);
      if(!r||*sa!=2||*sb!=2)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
      ca=f[0];cb=f[1];return r;
    }
    if((k0==CoreFeatureKind::Edge&&k1==CoreFeatureKind::Face)||(k0==CoreFeatureKind::Face&&k1==CoreFeatureKind::Edge)) {
      const int edgeSide=k0==CoreFeatureKind::Edge?0:1, faceSide=1-edgeSide;orientation=edgeSide;
      const auto& poly=*c[faceSide]; const auto& seg=*c[edgeSide];
      const Vec3 x=convert(faceSide,edgeSide,anchor(edgeSide)),y=convert(faceSide,edgeSide,end(edgeSide));const auto& ff=poly.features[f[faceSide]];
      auto clip=clipToFeature(poly,ff,x,y,0,ff.adjacent.size());
      if(clip.lowNeighbor || clip.highNeighbor) {
        if(clip.lowNeighbor==clip.highNeighbor || clip.high<clip.low) {
          // 004ae300 follows the face boundary from the first rejecting edge.
          const auto first=clip.lowNeighbor;
          if(!first||ff.adjacent.empty())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
          std::size_t current=0;
          while(current<ff.adjacent.size()&&featureIndex(poly,ff.adjacent[current].neighbor)!=first)++current;
          if(current==ff.adjacent.size())return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
          std::optional<std::size_t> previous;
          bool rejected=false, accepted=false;
          std::optional<std::size_t> selected;
          for(std::size_t iterations=0;iterations<=ff.adjacent.size()*2;++iterations) {
            const auto currentEdge=featureIndex(poly,ff.adjacent[current].neighbor);
            if(!currentEdge||poly.features[*currentEdge].ref.kind!=CoreFeatureKind::Edge)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
            auto plane=clipToFeature(poly,ff,x,y,current,current+1);
            std::size_t next=(current+1)%ff.adjacent.size();
            if(!plane.lowNeighbor&&!plane.highNeighbor) {
              if(accepted){if(!previous)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);selected=featureIndex(poly,ff.adjacent[*previous].neighbor);break;}
              if(rejected){if(!previous)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);selected=featureIndex(poly,ff.adjacent[*previous].neighbor);break;}
            } else {
              rejected=true;
              float lo=0,hi=1;
              if(plane.lowNeighbor!=plane.highNeighbor) {
                if(!plane.lowNeighbor&&plane.highNeighbor)lo=plane.high;
                else if(plane.lowNeighbor)hi=plane.low;
              }
              const auto& edgeFeature=poly.features[*currentEdge];
              if(edgeFeature.adjacent.size()<2)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
              auto ends=clipToFeature(poly,edgeFeature,x,y,0,2,lo,hi);
              auto shares=[&](std::size_t index,std::optional<std::size_t> vertex) {
                auto e=featureIndex(poly,ff.adjacent[index].neighbor);
                if(!e||!vertex)return false;
                const auto& adj=poly.features[*e].adjacent;
                return adj.size()>=2&&(featureIndex(poly,adj[0].neighbor)==vertex||featureIndex(poly,adj[1].neighbor)==vertex);
              };
              auto steer=[&](std::optional<std::size_t> vertex){if(!shares(next,vertex))next=current?current-1:ff.adjacent.size()-1;};
              if(ends.lowNeighbor&&ends.lowNeighbor==ends.highNeighbor)steer(ends.lowNeighbor);
              else if(ends.high<ends.low) {
                if(accepted){if(!previous)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);selected=featureIndex(poly,ff.adjacent[*previous].neighbor);break;}
                steer(ends.lowNeighbor?ends.lowNeighbor:ends.highNeighbor);
              } else {
                accepted=true;
                auto gradient=[&](std::optional<std::size_t> v,float t){
                  if(!v)return 0.0f;auto ref=poly.features[*v].ref;
                  if(ref.kind!=CoreFeatureKind::Vertex||ref.index>=poly.vertices.size())return 0.0f;
                  return dot(subtract(along(x,y,t),poly.vertices[ref.index].position),subtract(y,x));
                };
                if(ends.lowNeighbor&&gradient(ends.lowNeighbor,ends.low)>0)steer(ends.lowNeighbor);
                else if(!ends.highNeighbor||gradient(ends.highNeighbor,ends.high)>=0){selected=currentEdge;break;}
                else steer(ends.highNeighbor);
              }
            }
            if(previous&&next==*previous){selected=featureIndex(poly,ff.adjacent[*previous].neighbor);break;}
            previous=current;current=next;
          }
          if(!selected)return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
          f[faceSide]=*selected;continue;
        }
      }
      const auto& face=poly.faces[ff.ref.index];const Vec3 delta=subtract(y,x);
      const float dl=dot(subtract(along(x,y,clip.low),anchor(faceSide)),face.normal);
      const float dh=dot(subtract(along(x,y,clip.high),anchor(faceSide)),face.normal);
      if(dl*dh<0) {
        const auto offset=subtract(x,anchor(faceSide));
        const float d0=offset[2]*face.normal[2]+offset[1]*face.normal[1]+offset[0]*face.normal[0];
        const auto lastOffset=subtract(y,anchor(faceSide));
        const float d1=lastOffset[2]*face.normal[2]+lastOffset[1]*face.normal[1]+lastOffset[0]*face.normal[0];
        const float denominator=delta[2]*face.normal[2]+delta[1]*face.normal[1]+delta[0]*face.normal[0];
        const float t=-d0/denominator;
        CorePair r;Vec3 facePoint;for(int i=0;i<3;++i)facePoint[i]=t*delta[i]+(x[i]-anchor(faceSide)[i])+anchor(faceSide)[i];Vec3 edgePoint=toLocal(*pose[edgeSide],world(faceSide,facePoint));
        r.normal=matrixTransformRows(pose[faceSide]->rot,face.normal);
        if(edgeSide==0){r.pointA=edgePoint;r.pointB=facePoint;}
        else {r.pointA=facePoint;r.pointB=edgePoint;for(auto& n:r.normal)n=-n;}
        r.distance=d0>0?d1:d0;
        return finish(r);
      }
      if(dot(delta,face.normal)*dl<0){if(clip.highNeighbor)f[faceSide]=*clip.highNeighbor;else f[edgeSide]=*featureIndex(seg,{CoreFeatureKind::Vertex,seg.edges[seg.features[f[edgeSide]].ref.index].vertices[1]});}
      else {if(clip.lowNeighbor)f[faceSide]=*clip.lowNeighbor;else f[edgeSide]=*featureIndex(seg,{CoreFeatureKind::Vertex,seg.edges[seg.features[f[edgeSide]].ref.index].vertices[0]});}
      continue;
    }
    // The edge/face transition has not yet been translated here.
    return corePairUnavailable(PairSiteFile::CorePair,__LINE__);
  }
  // 004ae9c0 after features+2*features iterations: logs "Voronoi error
  // (infinite loop)", stores the current features and returns 1e6 (005f14dc),
  // which every caller treats as "too far": the pair is dropped or not made.
  std::fprintf(stderr, "Voronoi error (infinite loop)\n");
  corePairCountLoopCap();
  ca=f[0];cb=f[1];
  CorePair far;far.distance=1000000.0f;
  return far;
}
}

namespace physics {
namespace {
constexpr unsigned kSiteLines = 1024;
std::array<std::atomic<uint64_t>, 2 * kSiteLines> g_siteCounts{};
std::array<uint64_t, 2 * kSiteLines> g_siteReported{};
std::atomic<uint64_t> g_loopCaps{0};
uint64_t g_loopCapsReported = 0;
}  // namespace

std::optional<CorePair> corePairUnavailable(PairSiteFile file, unsigned line) {
  const unsigned index =
      static_cast<unsigned>(file) * kSiteLines + std::min(line, kSiteLines - 1);
  g_siteCounts[index].fetch_add(1, std::memory_order_relaxed);
  return std::nullopt;
}

void corePairCountLoopCap() {
  g_loopCaps.fetch_add(1, std::memory_order_relaxed);
}

std::string corePairDiagnosticsDelta() {
  std::string out;
  char piece[64];
  for (unsigned i = 0; i < g_siteCounts.size(); ++i) {
    const uint64_t now = g_siteCounts[i].load(std::memory_order_relaxed);
    if (now == g_siteReported[i])
      continue;
    std::snprintf(piece, sizeof piece, "%s%s:%u=%llu", out.empty() ? "" : " ",
                  i < kSiteLines ? "core_pair" : "contact_geometry",
                  i % kSiteLines, static_cast<unsigned long long>(now));
    out += piece;
    g_siteReported[i] = now;
  }
  const uint64_t caps = g_loopCaps.load(std::memory_order_relaxed);
  if (caps != g_loopCapsReported) {
    std::snprintf(piece, sizeof piece, "%svoronoi_loop_cap=%llu",
                  out.empty() ? "" : " ", static_cast<unsigned long long>(caps));
    out += piece;
    g_loopCapsReported = caps;
  }
  return out;
}
}  // namespace physics
