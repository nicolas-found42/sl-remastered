#include "physics_mesh_mass.hpp"

#include <cmath>

namespace physics {
namespace {
struct Projection { double p1{}, pa{}, pb{}, paa{}, pbb{}, paaa{}, pbbb{}, pab{}, paab{}, pabb{}; };

Projection project(const std::vector<Vec3>& v, const MeshTriangle& t, int a, int b) {
  Projection p;
  for (int i = 0; i < 3; ++i) {
    const auto& x = v[t.indices[static_cast<size_t>(i)]];
    const auto& y = v[t.indices[static_cast<size_t>((i + 1) % 3)]];
    float a0=x[a], b0=x[b], a1=y[a], b1=y[b];
    float da=a1-a0, db=b1-b0, a0sq=a0*a0, a1sq=a1*a1;
    float a0cu=a0sq*a0, b0sq=b0*b0, b0cu=b0sq*b0, b1cu=b1*b1*b1;
    float c1=(a1+a0)*a1+a0sq, c2=c1*a1+a0cu;
    float twice=a1*a0+a1*a0;
    float ca=a1sq*3.0f+twice+a0sq, cb=a0sq*3.0f+twice+a1sq;
    float d1=(b1+b0)*b1+b0sq, d2=d1*b1+b0cu;
    p.p1=static_cast<float>((a1+a0)*db+static_cast<float>(p.p1));
    p.pa=static_cast<float>(c1*db+static_cast<float>(p.pa));
    p.paa=static_cast<float>(c2*db+static_cast<float>(p.paa));
    p.paaa=static_cast<float>((c2*a1+a0cu*a0)*db+static_cast<float>(p.paaa));
    p.pb=static_cast<float>(d1*da+static_cast<float>(p.pb));
    p.pbb=static_cast<float>(d2*da+static_cast<float>(p.pbb));
    p.pbbb=static_cast<float>((d2*b1+b0cu*b0)*da+static_cast<float>(p.pbbb));
    p.pab=static_cast<float>((cb*b0+ca*b1)*db+static_cast<float>(p.pab));
    p.paab=static_cast<float>(((a0cu*4.0f+cb*a1)*b0+(ca*a0+a1sq*a1*4.0f)*b1)*db+static_cast<float>(p.paab));
    float ba=b1*b0sq, bb=b1*b1*b0;
    p.pabb=static_cast<float>((((bb*3.0f+(ba+ba))+b1cu*4.0f+b0cu)*a1+((ba*3.0f+(bb+bb))+b0cu*4.0f+b1cu)*a0)*da+static_cast<float>(p.pabb));
  }
  p.p1 = static_cast<float>(p.p1 * 0.5);
  p.pa = static_cast<float>(p.pa * (1.0 / 6.0));
  p.pb = static_cast<float>(p.pb * (-1.0 / 6.0));
  p.paa = static_cast<float>(p.paa * (1.0 / 12.0));
  p.pbb = static_cast<float>(p.pbb * (-1.0 / 12.0));
  p.paaa = static_cast<float>(p.paaa * (1.0 / 20.0));
  p.pbbb = static_cast<float>(p.pbbb * (-1.0 / 20.0));
  p.pab = static_cast<float>(p.pab * (1.0 / 24.0));
  p.paab = static_cast<float>(p.paab * (1.0 / 60.0));
  p.pabb = static_cast<float>(p.pabb * (-1.0 / 60.0));
  return p;
}
}

bool applyMeshMassProperties(BodyProto& body, const std::vector<MeshTriangle>& triangles,
                             Vec3& center, std::string& error) {
  for (const auto& t : triangles) for (auto i : t.indices) if (i >= body.shape.vertices.size()) {
    error="mesh face references a missing vertex"; return false;
  }
  double volume=0, first[3]{}, second[3]{}, product[3]{};
  for (const auto& t : triangles) {
    const Vec3& n=t.normal;
    int c=std::abs(n[0])>std::abs(n[1]) && std::abs(n[0])>std::abs(n[2]) ? 0 :
          (std::abs(n[1])>std::abs(n[2]) ? 1 : 2);
    int a=(c+1)%3, b=(a+1)%3;
    if (n[c]==0) { error="mesh face has a zero normal"; return false; }
    const auto p=project(body.shape.vertices,t,a,b);
    const Vec3& v=body.shape.vertices[t.indices[0]];
    float w=((-n[0]*v[0])-n[1]*v[1])-n[2]*v[2];
    float k=1.0f/n[c], k2=k*k, k3=k2*k;
    float pa=static_cast<float>(p.pa), pb=static_cast<float>(p.pb), p1=static_cast<float>(p.p1);
    float paa=static_cast<float>(p.paa), pbb=static_cast<float>(p.pbb), pab=static_cast<float>(p.pab);
    float paaa=static_cast<float>(p.paaa), pbbb=static_cast<float>(p.pbbb);
    float paab=static_cast<float>(p.paab), pabb=static_cast<float>(p.pabb);
    double face1[3]; face1[a]=pa*k; face1[b]=pb*k;
    face1[c]=-(((n[a]*pa+n[b]*pb)+w*p1)*k2);
    double face2[3]; face2[a]=static_cast<float>(p.paa*static_cast<double>(k)); face2[b]=static_cast<float>(p.pbb*static_cast<double>(k));
    float cross=n[b]*n[a]; float sum=n[a]*pa+n[b]*pb;
    face2[c]=(((w*p1+(sum+sum))*w+n[b]*n[b]*pbb+n[a]*n[a]*paa)+(cross+cross)*pab)*k3;
    double face3[3]; face3[a]=static_cast<float>(p.paaa*static_cast<double>(k)); face3[b]=static_cast<float>(p.pbbb*static_cast<double>(k));
    float cubic=(n[a]*n[a]*n[b]*3.0f*paab+n[a]*n[a]*n[a]*paaa);
    cubic+=(n[b]*n[b]*n[a]*3.0f*pabb+n[b]*n[b]*n[b]*pbbb);
    cubic+=((n[b]*n[b]*pbb+n[a]*n[a]*paa)+(cross+cross)*pab)*(w*3.0f);
    cubic+=(sum*3.0f+w*p1)*(w*w);
    face3[c]=-((k3*k)*cubic);
    double faceprod[3]; faceprod[a]=static_cast<float>(p.paab*static_cast<double>(k));
    faceprod[b]=-(((n[a]*pabb+n[b]*pbbb)+w*pbb)*k2);
    float s=n[a]*paa+n[b]*pab;
    faceprod[c]=((n[b]*n[b]*pabb+n[a]*n[a]*paaa+(cross+cross)*paab)+(w*pa+(s+s))*w)*k3;
    volume=static_cast<float>(n[0]*static_cast<float>(face1[0])+static_cast<float>(volume));
    for (int j=0;j<3;++j) {
      first[j]=static_cast<float>(n[j]*static_cast<float>(face2[j])+static_cast<float>(first[j]));
      second[j]=static_cast<float>(n[j]*static_cast<float>(face3[j])+static_cast<float>(second[j]));
      product[j]=static_cast<float>(n[j]*static_cast<float>(faceprod[j])+static_cast<float>(product[j]));
    }
  }
  if (volume==0 || !std::isfinite(volume)) { error="mesh has zero or invalid signed volume"; return false; }
  double cm[3]; for(int i=0;i<3;++i) { first[i]=static_cast<float>(first[i])*0.5f; second[i]=static_cast<float>(second[i]*(1.0/3.0)); product[i]=static_cast<float>(product[i])*0.5f; cm[i]=static_cast<float>(first[i])/static_cast<float>(volume); center[i]=static_cast<float>(cm[i]); }
  Mat34 inertia{};
  float x=static_cast<float>(cm[0]), y=static_cast<float>(cm[1]), z=static_cast<float>(cm[2]);
  float inv=1.0f/static_cast<float>(volume);
  float sx=static_cast<float>(second[0]), sy=static_cast<float>(second[1]), sz=static_cast<float>(second[2]);
  inertia[0]=(sz+sy)*inv-(y*y+z*z);
  inertia[5]=(sx+sz)*inv-(x*x+z*z);
  inertia[10]=(sx+sy)*inv-(x*x+y*y);
  inertia[1]=inertia[4]=y*x-static_cast<float>(product[0])*inv;
  inertia[6]=inertia[9]=z*y-static_cast<float>(product[1])*inv;
  inertia[2]=inertia[8]=z*x-static_cast<float>(product[2])*inv;
  body.invInertia=matrixInverse(inertia);
  for (auto& x : body.invInertia) x*=body.invMass;
  body.inertia=matrixInverse(body.invInertia);
  for (auto& v : body.shape.vertices) for(int i=0;i<3;++i) v[i]-=center[i];
  for(int i=0;i<3;++i) body.shape.centerOffset[i]=center[i];
  body.shape.volume=static_cast<float>(volume); body.volume=body.shape.volume;
  body.massPropertiesSupported=true;
  return true;
}
}
