#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

// Column-major affine matrices; translation is in elements 12..14.
using SkeletonMatrix = std::array<float, 16>;
struct SkeletonBone {
  std::string name;
  int32_t parent = -1;
  SkeletonMatrix local{}, bind{}, inverseBind{};
  std::array<float, 3> minimum{}, maximum{};
};
struct SkeletonResult {
  bool ok = false;
  std::vector<SkeletonBone> bones;
  std::string error;
};
// 00531e90: each record is a name, parent, four triples and three limit pairs.
// Translation is converted by 0.1. Non-six-value limits default to +/-pi.
// Invalid hierarchy, singular matrices, truncation and non-finite values fail.
SkeletonResult parseSkeleton(std::string_view text);

// Per-render-instance pose; shared resource bind data is never mutated.
class SkeletonPose {
public:
  using Point = std::array<float, 3>;
  explicit SkeletonPose(std::vector<SkeletonBone> bones);
  int findBone(std::string_view name) const;
  const std::vector<SkeletonMatrix> &matrices() const { return world_; }
  const std::vector<SkeletonMatrix> &palette() const { return palette_; }
  bool setLocal(size_t bone, const SkeletonMatrix &matrix);
  int addConstraint(std::string_view name, std::string_view bone,
                    unsigned ancestors, Point offset);
  bool setConstraintTarget(size_t constraint, Point target, float weight = 1);
  void solve(unsigned iterations);
  void setSteeringMount(const SkeletonMatrix &wheelToSeat, float radius, float depth);
  void updateSteering(float angle);
  const SkeletonMatrix *namedFrame(std::string_view name) const;
  void setNamedFrame(std::string_view name, const SkeletonMatrix &matrix);
private:
  struct Constraint {
    std::string name;
    size_t bone = 0, root = 0;
    Point offset{}, target{};
    float weight = 0;
  };
  void refresh();
  bool affects(size_t ancestor, size_t bone) const;
  Point endpoint(const Constraint &constraint) const;
  std::vector<SkeletonBone> bones_;
  std::vector<SkeletonMatrix> local_, world_, palette_;
  std::vector<size_t> order_;
  std::vector<Constraint> constraints_;
  std::vector<std::pair<std::string, SkeletonMatrix>> frames_;
  SkeletonMatrix wheelToSeat_{};
  std::array<SkeletonMatrix, 3> steeringFrames_{};
  float steeringRadius_ = 0, steeringDepth_ = 0;
  bool steeringMounted_ = false;
};

struct CarConfig;
// FUN_00448f20 target setup in car-local meters. Returns missing required bones.
std::vector<std::string> configureSeatedDriver(SkeletonPose &pose, const CarConfig &config);
