#include "skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace {
constexpr float pi = 3.14159265358979323846f;
SkeletonMatrix identity() { SkeletonMatrix m{}; m[0]=m[5]=m[10]=m[15]=1; return m; }
SkeletonMatrix multiply(const SkeletonMatrix &a, const SkeletonMatrix &b) {
  SkeletonMatrix m{};
  for (size_t c=0; c<4; ++c) for (size_t r=0; r<4; ++r)
    for (size_t k=0; k<4; ++k) m[c*4+r] += a[k*4+r]*b[c*4+k];
  return m;
}
bool inverse(const SkeletonMatrix &m, SkeletonMatrix &out) {
  double a[4][8]{};
  for (size_t r=0;r<4;++r) for (size_t c=0;c<4;++c) {
    a[r][c]=m[c*4+r]; a[r][c+4] = r==c ? 1.0 : 0.0;
  }
  for (size_t c=0;c<4;++c) {
    size_t pivot=c;
    for (size_t r=c+1;r<4;++r) if (std::fabs(a[r][c])>std::fabs(a[pivot][c])) pivot=r;
    if (a[pivot][c] == 0) return false;
    for (size_t j=0;j<8;++j) std::swap(a[c][j],a[pivot][j]);
    const double d=a[c][c];
    for (double &v : a[c]) v/=d;
    for (size_t r=0;r<4;++r) if (r!=c) {
      const double scale=a[r][c];
      for (size_t j=0;j<8;++j) a[r][j]-=scale*a[c][j];
    }
  }
  for (size_t r=0;r<4;++r) for (size_t c=0;c<4;++c) {
    out[c*4+r]=static_cast<float>(a[r][c+4]);
    if (!std::isfinite(out[c*4+r])) return false;
  }
  return true;
}
bool triple(const std::string &line, float &a, float &b, float &c) {
  return std::sscanf(line.c_str(), " %f , %f , %f", &a, &b, &c)==3 &&
         std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
}
}

SkeletonResult parseSkeleton(std::string_view text) {
  SkeletonResult result;
  std::istringstream input{std::string(text)};
  std::string line;
  std::unordered_set<std::string> names;
  while (std::getline(input,line)) {
    if (!line.empty() && line.back()=='\r') line.pop_back();
    if (line.empty()) { result.error="empty bone name"; return result; }
    if (result.bones.size() >= 4096 || !names.insert(line).second) {
      result.error="too many or duplicate bones"; return result;
    }
    SkeletonBone bone;
    bone.name=line;
    if (!std::getline(input,line)) { result.error="missing parent"; return result; }
    std::istringstream parentInput(line);
    long long parent=0;
    if (!(parentInput >> parent) || parent < -1 || parent > std::numeric_limits<int32_t>::max()) {
      result.error="invalid parent"; return result;
    }
    bone.parent=static_cast<int32_t>(parent);
    bone.local=identity();
    for (size_t row=0;row<4;++row) {
      float a,b,c;
      if (!std::getline(input,line) || !triple(line,a,b,c)) { result.error="invalid transform"; return result; }
      const float scale=row==3 ? 0.1f : 1.0f;
      bone.local[row*4]=a*scale; bone.local[row*4+1]=b*scale; bone.local[row*4+2]=c*scale;
    }
    int count=0;
    for (size_t i=0;i<3;++i) {
      if (!std::getline(input,line)) { result.error="missing limits"; return result; }
      float lo=0, hi=0;
      const int got=std::sscanf(line.c_str(), " %f , %f", &lo, &hi);
      if ((got>0 && !std::isfinite(lo)) || (got>1 && !std::isfinite(hi))) { result.error="invalid limits"; return result; }
      count+=std::max(got,0);
      bone.minimum[i]=lo; bone.maximum[i]=hi;
    }
    if (count!=6) { bone.minimum.fill(-pi); bone.maximum.fill(pi); }
    result.bones.push_back(std::move(bone));
  }
  if (result.bones.empty()) { result.error="empty skeleton"; return result; }
  std::vector<uint8_t> visit(result.bones.size());
  std::function<bool(size_t)> build = [&](size_t i) {
    if (visit[i]==2) return true;
    if (visit[i]==1) return false;
    visit[i]=1;
    auto &bone=result.bones[i];
    if (bone.parent >= 0) {
      const auto parent=static_cast<size_t>(bone.parent);
      if (parent>=result.bones.size() || !build(parent)) return false;
      bone.bind=multiply(result.bones[parent].bind,bone.local);
    } else bone.bind=bone.local;
    if (!inverse(bone.bind,bone.inverseBind)) return false;
    visit[i]=2;
    return true;
  };
  for (size_t i=0;i<result.bones.size();++i) if (!build(i)) {
    result.error="invalid hierarchy or singular transform"; result.bones.clear(); return result;
  }
  result.ok=true;
  return result;
}
