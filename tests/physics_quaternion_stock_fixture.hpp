// Exact original Stock 0054f810/004a7c10 result at step83, x87 CW0x007f.
// Captured under the declared four-face discovery fixture, not a whole-game proof.
// Original executable SHA256: 7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde.
// Retained raw trace: local/verify/parity-goal-20261102-092437/retained-query/
// eligibility-root/run-115cbd844c/full-loop-e22eec9688/work/first-0.jsonl
// Trace SHA256: 8c4a4cce0d49236cc653f1dfa41385821868e68a04c1e2012deedf9969f2d7e5.
// body index1: prepared current buffer1, previous destination buffer0;
// expected quaternion/rotation come from before-discovery83 destination buffer0.
// Fixture provenance and before/after evidence: retained-query/norm-proposal/.
#pragma once
#include "physics_body.hpp"
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
namespace stockQuaternion83 {
inline constexpr std::array<uint32_t,53> sourceWords = {
    1075128049u, 1050253741u, 1043173808u, 1034509512u, 3196532162u, 3181992472u,
    0u, 0u, 0u, 1068568844u, 3154607468u, 3276024945u,
    1007123270u, 0u, 0u, 1u, 1065353131u, 995116900u,
    917920440u, 0u, 3142600554u, 1065353047u, 995095588u, 0u,
    916194932u, 3142579242u, 1065353132u, 0u, 1070985505u, 3064205097u,
    1070985299u, 1068648687u, 3062413033u, 1068648514u, 0u, 0u,
    0u, 3134190664u, 877830717u, 3134211976u, 1065353175u, 1067030939u,
    794870400u, 2887778304u, 0u, 0u, 1067030940u, 2921873408u,
    0u, 2885681152u, 2945779712u, 1067030940u, 0u};
inline constexpr std::array<uint32_t,53> destinationBeforeWords = {
    1075128544u, 1049850202u, 1043165883u, 3183893878u, 3184399572u, 1036410810u,
    0u, 1014420930u, 0u, 1068429034u, 1129805599u, 3276023243u,
    3277289184u, 0u, 0u, 1u, 1065351780u, 1012293995u,
    951324998u, 0u, 3159777647u, 1065350344u, 1012289199u, 0u,
    951248196u, 3159772851u, 1065351781u, 0u, 1058549620u, 917519074u,
    1058549117u, 1056810092u, 915592642u, 1056809252u, 1113394719u, 3120955392u,
    1113394783u, 3151384842u, 873857644u, 3151389638u, 1065352498u, 1067030938u,
    814676992u, 2902458368u, 0u, 0u, 1067030938u, 820486144u,
    0u, 2910846976u, 2940395520u, 1067030939u, 0u};
inline constexpr std::array<uint32_t,4> expectedQuaternion = {
    1011495742u, 877415543u, 1011492864u, 1065350663u};
inline constexpr std::array<uint32_t,12> expectedRotation = {
    1065348108u, 3167363101u, 966778648u, 0u, 1019879459u, 1065342997u,
    3167365979u, 0u, 966752502u, 1019882337u, 1065348105u, 0u};
inline constexpr std::array<uint32_t,12> inverseWords = {
    1067030939u, 0u, 0u, 0u, 0u, 1067030939u,
    0u, 0u, 0u, 0u, 1067030939u, 0u};
inline constexpr std::array<uint32_t,12> inertiaWords = {
    1062557012u, 0u, 0u, 0u, 0u, 1062557012u,
    0u, 0u, 0u, 0u, 1062557012u, 0u};
inline void check() {
  physics::BodyProto proto;
  proto.invMass = std::bit_cast<float>(1028443341u);
  proto.flags = 1025;
  proto.linearDrag = std::bit_cast<float>(3184315596u);
  proto.angularDrag = std::bit_cast<float>(3192704204u);
  for (size_t i = 0; i < 12; ++i) {
    proto.invInertia[i] = std::bit_cast<float>(inverseWords[i]);
    proto.inertia[i] = std::bit_cast<float>(inertiaWords[i]);
  }
  const auto input = std::bit_cast<physics::State>(sourceWords);
  auto output = std::bit_cast<physics::State>(destinationBeforeWords);
  assert(physics::integrate(proto, 9u, input, output, std::bit_cast<float>(1015580809u)));
  const auto quat = std::bit_cast<std::array<uint32_t, 4>>(output.quat);
  const auto matrix = std::bit_cast<std::array<uint32_t, 12>>(output.rot);
  std::printf("quat=");
  for (auto word : quat) std::printf("%08x,", word);
  std::printf(" rotation=");
  for (auto word : matrix) std::printf("%08x,", word);
  std::puts("");
  std::fflush(stdout);
  assert(quat == expectedQuaternion);
  assert(matrix == expectedRotation);
}

} // namespace stockQuaternion83
