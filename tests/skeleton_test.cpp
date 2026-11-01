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
  // Instance ownership, named lookup, target-driven chain solving and skin
  // palette bind compensation. Fault injection: disabling solve must fail 9.
  SkeletonPose pose(tree.bones);
  if (pose.findBone("root") != 1 || pose.findBone("missing") != -1) return 7;
  for (const auto &matrix : pose.palette()) {
    SkeletonMatrix identity{}; identity[0]=identity[5]=identity[10]=identity[15]=1;
    for (size_t i=0;i<16;++i) if (!near(matrix[i],identity[i])) return 8;
  }
  auto chain = parseSkeleton(
      "tip\n1\n1,0,0\n0,1,0\n0,0,1\n10,0,0\n-3.14159265,3.14159265\n-3.14159265,3.14159265\n-3.14159265,3.14159265\n"
      "root\n-1\n1,0,0\n0,1,0\n0,0,1\n0,0,0\n-3.14159265,3.14159265\n-3.14159265,3.14159265\n-3.14159265,3.14159265\n");
  if (!chain.ok) return 9;
  SkeletonPose constrained(chain.bones), independent(chain.bones);
  const int constraint = constrained.addConstraint("hand", "tip", 1, {0,0,0});
  if (constraint < 0 || constrained.addConstraint("hand", "missing", 1, {}) != constraint ||
      constrained.addConstraint("invalid", "missing", 1, {}) != -1 ||
      !constrained.setConstraintTarget(static_cast<size_t>(constraint), {0,1,0})) return 10;
  constrained.solve(8);
  if (!near(constrained.matrices()[0][12],0) || !near(constrained.matrices()[0][13],1) ||
      !near(independent.matrices()[0][12],1) || !near(independent.matrices()[0][13],0)) return 11;
  chain.bones[1].minimum[2] = chain.bones[1].maximum[2] = 0;
  SkeletonPose limited(chain.bones);
  const int limitedConstraint = limited.addConstraint("hand", "tip", 1, {});
  limited.setConstraintTarget(static_cast<size_t>(limitedConstraint), {0,1,0});
  limited.solve(8);
  if (!near(limited.matrices()[0][12],1) || !near(limited.matrices()[0][13],0)) return 12;
  SkeletonMatrix mount{}; mount[0]=mount[5]=mount[10]=mount[15]=1;
  constrained.setSteeringMount(mount, 0.2f, 0.03f);
  constrained.updateSteering(0);
  const auto *leftFrame = constrained.namedFrame("lhbone");
  const auto *directionFrame = constrained.namedFrame("sthbone");
  if (!leftFrame || !directionFrame || constrained.namedFrame("unknown") ||
      !near((*leftFrame)[12],-std::sin(1.0f)*0.185f) ||
      !near((*directionFrame)[13],0.215f/std::cos(1.0f))) return 13;
  const float initialX = (*leftFrame)[12];
  constrained.updateSteering(0.5f);
  if (near((*leftFrame)[12],initialX) ||
      !near((*leftFrame)[12],-std::sin(1.5f)*0.185f)) return 14;
  return 0;
}
