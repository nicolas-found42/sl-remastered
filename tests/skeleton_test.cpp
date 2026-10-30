#include "skeleton.hpp"
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

static bool near(float a, float b) { return std::fabs(a-b)<0.0002f; }
static bool inversePair(const SkeletonMatrix &a, const SkeletonMatrix &b) {
  for (size_t c=0;c<4;++c) for(size_t r=0;r<4;++r) {
    float sum=0;
    for(size_t k=0;k<4;++k) sum+=a[k*4+r]*b[c*4+k];
    if (!near(sum, c==r ? 1.0f : 0.0f)) return false;
  }
  return true;
}
int main() {
  const std::string path = ".scratch/2005-extract/Program Files/Street Legal Racing - Redline/humans/drivers/vin.skl";
  std::ifstream file(path, std::ios::binary);
  if (!file) return 1;
  const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  const auto vin=parseSkeleton(text);
  if (!vin.ok || vin.bones.size()!=20 || vin.bones[19].name!="Bip01" || vin.bones[19].parent!=-1 ||
      vin.bones[9].name!="Bip01 Head" || vin.bones[9].parent!=10 ||
      vin.bones[13].name!="Bip01 L Foot" || vin.bones[18].name!="Bip01 L Hand" ||
      !near(vin.bones[0].local[12], 0.9936633f) || !near(vin.bones[0].minimum[0], -0.261799f)) return 2;
  for (const auto &bone : vin.bones) if (!inversePair(bone.bind,bone.inverseBind)) return 3;
  const std::string child = "child\n1\n1,0,0\n0,1,0\n0,0,1\n10,0,0\n0,1\n0,1\n0,1\n";
  const std::string parent = "root\n-1\n0,1,0\n-1,0,0\n0,0,1\n0,20,0\n0,1\n0,1\n0,1\n";
  const auto tree=parseSkeleton(child+parent);
  if (!tree.ok || !near(tree.bones[0].bind[12],0) || !near(tree.bones[0].bind[13],3) ||
      !inversePair(tree.bones[0].bind,tree.bones[0].inverseBind)) return 4;
  const auto defaults=parseSkeleton("root\n-1\n1,0,0\n0,1,0\n0,0,1\n0,0,0\ninvalid\n0,1\n0,1\n");
  if (!defaults.ok || !near(defaults.bones[0].minimum[0],-3.14159265f) ||
      !near(defaults.bones[0].maximum[2],3.14159265f)) return 5;
  if (parseSkeleton("").ok || parseSkeleton("root\n-1\n").ok ||
      parseSkeleton(child+child).ok || parseSkeleton(child).ok ||
      parseSkeleton("root\n0\n1,0,0\n0,1,0\n0,0,1\n0,0,0\n0,1\n0,1\n0,1\n").ok ||
      parseSkeleton("root\n-1\n0,0,0\n0,1,0\n0,0,1\n0,0,0\n0,1\n0,1\n0,1\n").ok) return 6;
  return 0;
}
