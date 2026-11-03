#include "natives/registry.hpp"
#include "tufa.hpp"

#include <cmath>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

namespace {
int failures = 0;
#define CHECK(condition)                                           \
  do {                                                             \
    if (!(condition)) {                                            \
      std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #condition); \
      ++failures;                                                  \
    }                                                              \
  } while (false)
float number(const NativeResult& result) {
  const auto* value = std::get_if<VmValue>(&result);
  const auto* number = value ? std::get_if<float>(value) : nullptr;
  CHECK(number);
  return number ? *number : -1.0f;
}
float field(const VmObject& object, const char* name) {
  for (const auto& item : object.fields)
    if (item.name == name)
      return std::get<float>(item.value);
  CHECK(false);
  return -1;
}
} // namespace

int main() {
  const auto parsed = tufaLoadFile("ref/raw/parts/scripts/DynoData.class");
  CHECK(parsed.ok && !parsed.file.classes.empty());
  if (!parsed.ok || parsed.file.classes.empty())
    return 1;
  VmArena arena;
  auto owned = std::make_unique<VmObject>();
  owned->runtimeClass = &parsed.file.classes.front();
  // Published DynoData defaults use metres. Native arrays are intentionally
  // absent, as in the shipped Native-backed class, not synthetic curve input.
  owned->fields = {{"ptr", int32_t{0}, "I"},
                   {"cylinders", 4.0f, "F"},
                   {"bore", .082f, "F"},
                   {"stroke", .090f, "F"},
                   {"Vmin", .00005f, "F"},
                   {"in_min", .005f, "F"},
                   {"in_max", .8f, "F"},
                   {"out_min", .005f, "F"},
                   {"out_max", .8f, "F"},
                   {"time_in_open", .001f, "F"},
                   {"time_in_close", .25f, "F"},
                   {"time_out_open", .75f, "F"},
                   {"time_out_close", .99f, "F"},
                   {"time_spark_min", .48f, "F"},
                   {"time_spark_inc", -.05f, "F"},
                   {"time_spark_RPM0", 2000.0f, "F"},
                   {"time_spark_RPM1", 5000.0f, "F"},
                   {"time_burn", .003f, "F"},
                   {"RPM_limit", 7000.0f, "F"},
                   {"maxRPM", 7000.0f, "F"},
                   {"rpm_turbo_mul", 16.0f, "F"},
                   {"rpm_turbo_opt", 85000.0f, "F"},
                   {"rpm_turbo_range", 20000.0f, "F"},
                   {"P_turbo_max", 0.0f, "F"},
                   {"P_turbo_waste", 10.0f, "F"},
                   {"intercooling", 1.0f, "F"},
                   {"mixture_H", 1000000.0f, "F"},
                   {"mixture_ratio", 14.0f, "F"},
                   {"max_air_consumption", 0.0f, "F"},
                   {"max_fuel_consumption", 0.0f, "F"},
                   {"nitro_H", 1.0f, "F"},
                   {"nitro_cooling", 1.0f, "F"},
                   {"Displacement", 0.0f, "F"},
                   {"Compression", 0.0f, "F"},
                   {"maxTorque", 0.0f, "F"},
                   {"maxHP", 0.0f, "F"},
                   {"RPM_maxTorque", 0.0f, "F"},
                   {"RPM_maxHP", 0.0f, "F"}};
  const VmObject* receiver = owned.get();
  arena.objects.push_back(std::move(owned));
  const auto call = [&](const char* method, const char* descriptor, std::vector<VmValue> args = {}) {
    return invokeNative("java.game.parts.DynoData", method, descriptor, args, receiver, &arena);
  };
  CHECK(std::holds_alternative<std::monostate>(call("newNative", "()V")));
  CHECK(number(call("calcDyno", "(F)F", {16.0f})) == 0);
  // Newer 004f4640/00571143, source f44fb88..., conditional original
  // dyno-original-step-v2 case0 sample1. Platform powf fails these raw words.
  CHECK(std::bit_cast<uint32_t>(number(call("getTorque", "(FF)F", {437.0f, 0.0f}))) == 1118055576u);
  CHECK(std::bit_cast<uint32_t>(number(call("getHP", "(FF)F", {437.0f, 0.0f}))) == 1167419951u);

  CHECK(std::fabs(field(*receiver, "Displacement") - 0.0019011663971468806f) < 1e-9f);
  CHECK(std::fabs(field(*receiver, "Compression") - 10.50583267211914f) < 1e-6f);
  CHECK(number(call("getTorque", "(FF)F", {-1.0f, 0.0f})) == 0);
  CHECK(number(call("getTorque", "(FF)F", {6555.0f, 0.0f})) == 0);
  CHECK(number(call("getHP", "(FF)F", {6555.0f, 0.0f})) == 0);
  const float torque = number(call("getTorque", "(FF)F", {1748.0f, 0.0f}));
  CHECK(torque > 100);
  CHECK(number(call("getTorque", "(FF)F", {1748.0f, 1.0f})) == torque);
  // Despite its name, the shipped native getHP samples the watt table.
  CHECK(number(call("getHP", "(FF)F", {1748.0f, 0.0f})) > 10000);
  CHECK(std::holds_alternative<std::monostate>(call("deleteNative", "()V")));
  CHECK(number(call("getTorque", "(FF)F", {1748.0f, 0.0f})) == 0);
  CHECK(number(call("getHP", "(FF)F", {1748.0f, 0.0f})) == 0);
  // The older non-Native caller receives legacy arrays and its untruncated
  // script step. It must not acquire a hidden persistent native handle.
  auto torqueArray = std::make_unique<VmObject>();
  torqueArray->arrayDescriptor = "[F";
  torqueArray->arrayElements.assign(16, VmValue{-999.0f});
  auto powerArray = std::make_unique<VmObject>();
  powerArray->arrayDescriptor = "[F";
  powerArray->arrayElements.assign(16, VmValue{-999.0f});
  const auto* legacyTorque = torqueArray.get();
  const auto* legacyPower = powerArray.get();
  arena.objects.push_back(std::move(torqueArray));
  arena.objects.push_back(std::move(powerArray));
  receiver->fields.emplace_back("torquetable", legacyTorque, "[F");
  receiver->fields.emplace_back("HPtable", legacyPower, "[F");
  receiver->fields.emplace_back("table_stepsize", 0.0f, "F");
  receiver->fields.emplace_back("torque", 0.0f, "F");
  CHECK(number(call("calcDyno", "(F)F", {16.0f})) == 0);
  CHECK(field(*receiver, "table_stepsize") == 437.5f);
  CHECK(field(*receiver, "torque") == 1.0f);
  CHECK(std::get<float>(legacyTorque->arrayElements[0]) == 0);
  CHECK(std::get<float>(legacyTorque->arrayElements[4]) > 100);
  CHECK(std::get<float>(legacyPower->arrayElements[4]) > 10000);
  CHECK(number(call("getTorque", "(FF)F", {1748.0f, 0.0f})) == 0);
  CHECK(std::holds_alternative<std::monostate>(call("newNative", "()V")));
  CHECK(number(call("calcDyno", "(F)F", {16.0f})) == 0);
  CHECK(number(call("getTorque", "(FF)F", {1748.0f, 0.0f})) > 100);
  // Complete newer DynoData.java clear(), not a fabricated zero-geometry
  // engine. Intercooling is deliberately absent: clear retains its value.
  const auto assign = [&](const char* name, float value) {
    for (auto& item : receiver->fields) {
      if (item.name == name) {
        item.value = value;
        return;
      }
    }
    receiver->fields.emplace_back(name, value, "F");
  };
  for (const auto& [name, value] : {
           std::pair{"cylinders", 0.0f}, {"bore", .01f}, {"stroke", .01f}, {"Vmin", 100000.0f}, {"in_min", 1.0f}, {"out_min", 1.0f}, {"in_max", 1.0f}, {"out_max", 1.0f}, {"time_in_open", .001f}, {"time_in_close", .002f}, {"time_out_open", .003f}, {"time_out_close", .004f}, {"time_spark_min", .5f}, {"time_spark_inc", 0.0f}, {"time_spark_RPM0", 0.0f}, {"time_spark_RPM1", 10000.0f}, {"time_burn", 100000.0f}, {"RPM_limit", 10000.0f}, {"T_loss", 100.0f}, {"rpm_turbo_mul", 0.0f}, {"rpm_turbo_opt", 0.0f}, {"rpm_turbo_range", 0.0f}, {"P_turbo_max", 0.0f}, {"P_turbo_waste", 0.0f}, {"nitro_H", 1.0f}, {"nitro_cooling", 1.0f}, {"nitro_consumption", 0.0f}, {"mixture_ratio", 14.0f}, {"mixture_H", 1000000.0f}, {"max_fuel_consumption", 0.0f}, {"max_air_consumption", 0.0f}, {"torque", 0.0f}, {"torque2", 0.0f}, {"maxRPM", 0.0f}})
    assign(name, value);
  receiver->fields.emplace_back("flags", int32_t{0}, "I");
  CHECK(field(*receiver, "intercooling") == 1.0f);
  CHECK(number(call("calcDyno", "(F)F", {16.0f})) == 0);
  CHECK(field(*receiver, "Displacement") == 0);
  CHECK(field(*receiver, "Compression") == 1);
  CHECK(field(*receiver, "maxTorque") == 0 && field(*receiver, "maxHP") == 0);
  CHECK(field(*receiver, "maxRPM") > 0);
  CHECK(number(call("getTorque", "(FF)F", {1748.0f, 0.0f})) == 0);
  // The stored watt table still contains the native friction loss. clear()
  // does not fabricate zero power or fall back to the previous engine curve.
  CHECK(number(call("getHP", "(FF)F", {1748.0f, 0.0f})) < 0);
  CHECK(std::holds_alternative<std::monostate>(call("deleteNative", "()V")));
  return failures ? 1 : 0;
}
