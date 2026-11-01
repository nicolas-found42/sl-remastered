#include "skeleton.hpp"
#include "car_config.hpp"
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


SkeletonPose::SkeletonPose(std::vector<SkeletonBone> bones)
    : bones_(std::move(bones)), local_(bones_.size()),
      world_(bones_.size()), palette_(bones_.size()) {
  // FUN_00532790: initialize independent instance transforms from bind locals.
  std::vector<bool> visited(bones_.size());
  std::function<void(size_t)> visit = [&](size_t i) {
    if (visited[i]) return;
    visited[i] = true;
    if (bones_[i].parent >= 0) visit(static_cast<size_t>(bones_[i].parent));
    local_[i] = bones_[i].local;
    order_.push_back(i);
  };
  for (size_t i = 0; i < bones_.size(); ++i) visit(i);
  refresh();
}
int SkeletonPose::findBone(std::string_view name) const {
  for (size_t i = 0; i < bones_.size(); ++i) {
    const auto &candidate = bones_[i].name;
    if (candidate.size() != name.size()) continue;
    bool match = true;
    for (size_t j = 0; j < name.size(); ++j)
      if (candidate[j] != name[j] && !(candidate[j] == ' ' && name[j] == '_')) {
        match = false; break;
      }
    if (match) return static_cast<int>(i);
  }
  return -1;
}
void SkeletonPose::refresh() {
  for (size_t i : order_) {
    const int parent = bones_[i].parent;
    world_[i] = parent < 0 ? local_[i] : multiply(world_[static_cast<size_t>(parent)], local_[i]);
    palette_[i] = multiply(world_[i], bones_[i].inverseBind);
  }
}
bool SkeletonPose::setLocal(size_t bone, const SkeletonMatrix &matrix) {
  if (bone >= bones_.size() || !std::all_of(matrix.begin(), matrix.end(),
      [](float v) { return std::isfinite(v); })) return false;
  local_[bone] = matrix;
  refresh();
  return true;
}
int SkeletonPose::addConstraint(std::string_view name, std::string_view bone,
                                unsigned ancestors, Point offset) {
  // FUN_00531950: names are unique, and the chain ends at the Nth ancestor.
  for (size_t i = 0; i < constraints_.size(); ++i)
    if (constraints_[i].name == name) return static_cast<int>(i);
  const int found = findBone(bone);
  if (found < 0) return -1;
  size_t root = static_cast<size_t>(found);
  while (ancestors-- && bones_[root].parent >= 0)
    root = static_cast<size_t>(bones_[root].parent);
  constraints_.push_back({std::string(name), static_cast<size_t>(found), root, offset, {}, 0});
  return static_cast<int>(constraints_.size() - 1);
}
bool SkeletonPose::setConstraintTarget(size_t i, Point target, float weight) {
  if (i >= constraints_.size() || !std::isfinite(weight) || weight < 0 ||
      !std::all_of(target.begin(), target.end(), [](float v) { return std::isfinite(v); })) return false;
  constraints_[i].target = target;
  constraints_[i].weight = weight;
  auto frame = identity();
  for (size_t k=0;k<3;++k) frame[12+k] = target[k];
  setNamedFrame(constraints_[i].name, frame);
  return true;
}
bool SkeletonPose::affects(size_t ancestor, size_t bone) const {
  for (;;) {
    if (ancestor == bone) return true;
    if (bones_[bone].parent < 0) return false;
    bone = static_cast<size_t>(bones_[bone].parent);
  }
}
SkeletonPose::Point SkeletonPose::endpoint(const Constraint &c) const {
  Point p{};
  const auto &m = world_[c.bone];
  for (size_t a = 0; a < 3; ++a)
    p[a] = m[12+a] + m[a]*c.offset[0] + m[4+a]*c.offset[1] + m[8+a]*c.offset[2];
  return p;
}
void SkeletonPose::solve(unsigned iterations) {
  // FUN_00533450: weighted coordinate descent, Z/Y/X order, reject steps
  // which increase squared target error. Unlike physical springs this solver
  // changes joint angles, not target positions or linear velocities.
  for (unsigned iteration = 0; iteration < iterations; ++iteration) {
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
      const size_t joint = *it;
      for (int axis = 2; axis >= 0; --axis) {
        const auto parent = bones_[joint].parent;
        const auto basis = parent < 0 ? identity() : world_[static_cast<size_t>(parent)];
        Point direction{basis[axis*4], basis[axis*4+1], basis[axis*4+2]};
        const Point pivot{world_[joint][12], world_[joint][13], world_[joint][14]};
        float sine = 0, cosine = 0, before = 0;
        for (const auto &c : constraints_) {
          if (c.weight == 0 || !affects(c.root, joint) || !affects(joint, c.bone)) continue;
          const auto p = endpoint(c);
          Point a{}, b{};
          float da = 0, db = 0;
          for (size_t k = 0; k < 3; ++k) {
            a[k] = p[k] - pivot[k]; b[k] = c.target[k] - pivot[k];
            da += a[k]*direction[k]; db += b[k]*direction[k];
            before += c.weight*(p[k]-c.target[k])*(p[k]-c.target[k]);
          }
          for (size_t k = 0; k < 3; ++k) { a[k] -= direction[k]*da; b[k] -= direction[k]*db; }
          const Point cross{a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
          for (size_t k = 0; k < 3; ++k) { sine += c.weight*cross[k]*direction[k]; cosine += c.weight*a[k]*b[k]; }
        }
        if (std::fabs(sine) + std::fabs(cosine) < 0.0001f) continue;
        const float angle = std::atan2(sine, cosine);
        SkeletonMatrix rotation = identity();
        const size_t a = static_cast<size_t>((axis+1)%3), b = static_cast<size_t>((axis+2)%3);
        rotation[a*4+a] = rotation[b*4+b] = std::cos(angle);
        rotation[a*4+b] = std::sin(angle); rotation[b*4+a] = -std::sin(angle);
        const auto previous = local_[joint];
        local_[joint] = multiply(rotation, previous);
        // FUN_00533450 uses wrapped angular intervals, including intervals
        // crossing -pi/pi; project to the nearest boundary, not numeric clamp.
        Point euler{std::atan2(local_[joint][6], local_[joint][10]),
                    std::asin(std::clamp(-local_[joint][2], -1.0f, 1.0f)),
                    std::atan2(local_[joint][1], local_[joint][0])};
        auto wrap = [](float value) {
          while (value < -pi) value += 2*pi;
          while (value > pi) value -= 2*pi;
          return value;
        };
        for (size_t k = 0; k < 3; ++k) {
          const float lo = bones_[joint].minimum[k], hi = bones_[joint].maximum[k];
          const float value = wrap(euler[k]);
          const bool inside = lo <= hi ? value >= lo && value <= hi : value >= lo || value <= hi;
          if (!inside) euler[k] = std::fabs(wrap(value-lo)) < std::fabs(wrap(value-hi)) ? lo : hi;
        }
        const float sx = std::sin(euler[0]), cx = std::cos(euler[0]);
        const float sy = std::sin(euler[1]), cy = std::cos(euler[1]);
        const float sz = std::sin(euler[2]), cz = std::cos(euler[2]);
        local_[joint] = {{cz*cy, sz*cy, -sy, 0,
          cz*sy*sx-sz*cx, sz*sy*sx+cz*cx, cy*sx, 0,
          cz*sy*cx+sz*sx, sz*sy*cx-cz*sx, cy*cx, 0, 0, 0, 0, 1}};
        // Rotation is about the joint, not about the parent's origin.
        for (size_t k = 0; k < 3; ++k) local_[joint][12+k] = previous[12+k];
        refresh();
        float after = 0;
        for (const auto &c : constraints_) {
          if (c.weight == 0 || !affects(c.root, joint) || !affects(joint, c.bone)) continue;
          const auto p = endpoint(c);
          for (size_t k = 0; k < 3; ++k) after += c.weight*(p[k]-c.target[k])*(p[k]-c.target[k]);
        }
        if (after > before) { local_[joint] = previous; refresh(); }
      }
    }
  }
}

std::vector<std::string> configureSeatedDriver(SkeletonPose &pose, const CarConfig &config) {
  // FUN_00448f20: head/neck chain length 2; feet 2; hands 3. Offsets
  // and wheel contact points are stock constants, not a canned seated clip.
  std::vector<std::string> missing;
  const auto target = [&](std::string_view name, std::string_view bone,
                          unsigned chain, SkeletonPose::Point offset,
                          SkeletonPose::Point point) {
    const int constraint = pose.addConstraint(name, bone, chain, offset);
    if (constraint < 0) { missing.emplace_back(bone); return; }
    pose.setConstraintTarget(static_cast<size_t>(constraint), point);
  };
  pose.setNamedFrame("bone00", identity());
  const auto &seat = config.seat;
  // Targets are expressed in the driver's seat-local coordinates.
  // Seat orientation conversion mirrors the render instance's YPR basis.
  const float cy=std::cos(seat[3]), sy=std::sin(seat[3]);
  const float cp=std::cos(seat[4]), sp=std::sin(seat[4]);
  const float cr=std::cos(seat[5]), sr=std::sin(seat[5]);
  const float basis[9]{cy*cr+sy*sp*sr, cp*sr, -sy*cr+cy*sp*sr,
                       -cy*sr+sy*sp*cr, cp*cr, sy*sr+cy*sp*cr,
                       sy*cp, -sp, cy*cp};
  const auto local = [&](SkeletonPose::Point point) {
    SkeletonPose::Point out{};
    for (size_t i=0;i<3;++i)
      for (size_t j=0;j<3;++j) out[i] += basis[i*3+j]*(point[j]-seat[j]);
    return out;
  };
  const int head = pose.findBone("Bip01_Head");
  if (head >= 0) {
    const auto &matrix = pose.matrices()[static_cast<size_t>(head)];
    target("head", "Bip01_Neck", 2, {0.7f,0,0}, {matrix[12],matrix[13],matrix[14]});
  } else missing.emplace_back("Bip01_Head");
  target("lfbone", "Bip01_L_Foot", 2, {}, local({config.pedals[0][0],config.pedals[0][1],config.pedals[0][2]}));
  target("rfbone", "Bip01_R_Foot", 2, {}, local({config.pedals[2][0],config.pedals[2][1],config.pedals[2][2]}));
  if (config.steeringBody >= 0 && static_cast<size_t>(config.steeringBody) < config.bodies.size()) {
    const auto &wheel = config.bodies[static_cast<size_t>(config.steeringBody)].transform;
    const float radius = config.steeringRadius[0]-0.015f;
    const auto wheelPoint = [&](float angle) {
      const SkeletonPose::Point contact{-std::sin(angle)*radius,
                                         std::cos(angle)*radius,
                                         config.steeringRadius[1]};
      SkeletonPose::Point point = wheel.position;
      for (size_t i=0;i<3;++i)
        for (size_t j=0;j<3;++j) point[i] += wheel.rotation[i*3+j]*contact[j];
      return local(point);
    };
    target("lhbone", "Bip01_L_Hand", 3, {0.6f,0,-0.3f}, wheelPoint(1));
    target("rhbone", "Bip01_R_Hand", 3, {0.6f,0,-0.3f}, wheelPoint(-1));
    const float extendedRadius = (config.steeringRadius[0]+0.015f)/std::cos(1.0f);
    const float handDirection = std::sin(1.0f)*extendedRadius*10.0f;
    target("sthbone_left", "Bip01_L_Hand", 0, {1.2f,-handDirection,0}, wheelPoint(0));
    target("sthbone_right", "Bip01_R_Hand", 0, {1.2f,handDirection,0}, wheelPoint(0));
    SkeletonMatrix mount = identity();
    const auto origin = local(wheel.position);
    for (size_t i=0;i<3;++i) {
      mount[12+i] = origin[i];
      for (size_t j=0;j<3;++j) {
        mount[j*4+i] = 0;
        for (size_t k=0;k<3;++k) mount[j*4+i] += basis[i*3+k]*wheel.rotation[k*3+j];
      }
    }
    pose.setSteeringMount(mount, config.steeringRadius[0], config.steeringRadius[1]);
    pose.updateSteering(0);
  }
  auto gearFrame = identity();
  const auto gearPoint = local({config.shifter[0],config.shifter[1],config.shifter[2]});
  for (size_t i=0;i<3;++i) gearFrame[12+i] = gearPoint[i];
  pose.setNamedFrame("gsbone", gearFrame);
  pose.solve(8);
  return missing;
}

void SkeletonPose::setSteeringMount(const SkeletonMatrix &wheelToSeat, float radius, float depth) {
  wheelToSeat_ = wheelToSeat;
  steeringRadius_ = radius;
  steeringDepth_ = depth;
  steeringMounted_ = true;
}
void SkeletonPose::updateSteering(float angle) {
  if (!steeringMounted_ || !std::isfinite(angle)) return;
  // FUN_00448f20/FUN_00541da0: the three named wheel frames share a
  // moving parent. Updating that parent moves both hand contacts and the
  // secondary direction targets, not only the render root.
  const float radius = steeringRadius_-0.015f;
  const float extended = (steeringRadius_+0.015f)/std::cos(1.0f);
  const float frameAngles[3]{1.0f+angle,-1.0f+angle,angle};
  const char *names[3]{"lhbone","rhbone","sthbone"};
  for (size_t i=0;i<3;++i) {
    auto &frame = steeringFrames_[i];
    auto local = identity();
    const float contactRadius = i == 2 ? extended : radius;
    local[12] = -std::sin(frameAngles[i])*contactRadius;
    local[13] = std::cos(frameAngles[i])*contactRadius;
    local[14] = steeringDepth_;
    frame = multiply(wheelToSeat_,local);
    const Point point{frame[12],frame[13],frame[14]};
    for (auto &constraint : constraints_)
      if (constraint.name == names[i] || (i == 2 &&
          (constraint.name == "sthbone_left" || constraint.name == "sthbone_right")))
        constraint.target = point;
  }
}
const SkeletonMatrix *SkeletonPose::namedFrame(std::string_view name) const {
  if (steeringMounted_) {
    if (name == "lhbone") return &steeringFrames_[0];
    if (name == "rhbone") return &steeringFrames_[1];
    if (name == "sthbone") return &steeringFrames_[2];
  }
  for (const auto &frame : frames_) if (frame.first == name) return &frame.second;
  const int bone = findBone(name);
  return bone < 0 ? nullptr : &world_[static_cast<size_t>(bone)];
}
void SkeletonPose::setNamedFrame(std::string_view name, const SkeletonMatrix &matrix) {
  for (auto &constraint : constraints_)
    if (constraint.name == name)
      constraint.target = {matrix[12],matrix[13],matrix[14]};
  for (auto &frame : frames_) if (frame.first == name) { frame.second = matrix; return; }
  frames_.emplace_back(std::string(name), matrix);
}
