#include "natives/vector3.hpp"

#include <cmath>

namespace {
// Stock reads script float fields by name; a missing or non-float field only logs and yields 0.0,
// which the host cannot reproduce, so it is reported as an error instead.
VmField* floatField(const VmObject* object, const char* name) {
	if (!object) return nullptr;
	VmField* found = nullptr;
	for (auto& field : object->fields) if (field.name == name) {
		if (found || field.descriptor != "F" || !std::holds_alternative<float>(field.value)) return nullptr;
		found = &field;
	}
	return found;
}

struct Triple { VmField* a; VmField* b; VmField* c; };
bool triple(const VmObject* object, const char* a, const char* b, const char* c, Triple& out) {
	out = {floatField(object, a), floatField(object, b), floatField(object, c)};
	return out.a && out.b && out.c;
}
float get(const VmField* field) { return std::get<float>(field->value); }
const VmObject* object(const VmValue& value) {
	const auto* pointer = std::get_if<const VmObject*>(&value);
	return pointer ? *pointer : nullptr;
}
}

NativeResult vector3Length(const std::vector<VmValue>& args, const VmObject* receiver, VmArena*,
                           const std::vector<const TufaClass*>*) {
	Triple v;
	if (!args.empty() || !triple(receiver, "x", "y", "z", v)) return std::string{"invalid Vector3.length call"};
	const double x = get(v.a), y = get(v.b), z = get(v.c);
	return VmValue{static_cast<float>(std::sqrt((z * z + y * y) + x * x))};
}

NativeResult vector3Distance(const std::vector<VmValue>& args, const VmObject* receiver, VmArena*,
                             const std::vector<const TufaClass*>*) {
	Triple self, other;
	if (args.size() != 1 || !object(args[0]) || !triple(receiver, "x", "y", "z", self) ||
	    !triple(object(args[0]), "x", "y", "z", other)) return std::string{"invalid Vector3.distance call"};
	const double dx = static_cast<double>(get(self.a)) - get(other.a);
	const double dy = static_cast<double>(get(self.b)) - get(other.b);
	const double dz = static_cast<double>(get(self.c)) - get(other.c);
	return VmValue{static_cast<float>(std::sqrt((dx * dx + dy * dy) + dz * dz))};
}

NativeResult vector3Normalize(const std::vector<VmValue>& args, const VmObject* receiver, VmArena*,
                              const std::vector<const TufaClass*>*) {
	Triple v;
	if (!args.empty() || !triple(receiver, "x", "y", "z", v)) return std::string{"invalid Vector3.normalize call"};
	const float x = get(v.a), y = get(v.b), z = get(v.c);
	const double length = std::sqrt((static_cast<double>(y) * y + static_cast<double>(z) * z) + static_cast<double>(x) * x);
	// The stock compare treats 0.0 and NaN (unordered) as "leave the fields alone".
	if (length == 0.0 || std::isnan(length)) return std::monostate{};
	const double inverse = 1.0 / length;
	v.a->value = static_cast<float>(inverse * x);
	v.b->value = static_cast<float>(inverse * y);
	v.c->value = static_cast<float>(inverse * z);
	return std::monostate{};
}

NativeResult vector3Rotate(const std::vector<VmValue>& args, const VmObject* receiver, VmArena*,
                           const std::vector<const TufaClass*>*) {
	Triple v, angles;
	if (args.size() != 1 || !object(args[0]) || !triple(receiver, "x", "y", "z", v) ||
	    !triple(object(args[0]), "y", "p", "r", angles)) return std::string{"invalid Vector3.rotate call"};
	const double x = get(v.a), y = get(v.b), z = get(v.c);
	const double yaw = get(angles.a), pitch = get(angles.b), roll = get(angles.c);
	// 0054ecd0 keeps sin(yaw) and sin(pitch) unrounded and stores the other sines and cosines as floats.
	const double s0 = std::sin(yaw), s1 = std::sin(pitch);
	const float c0 = static_cast<float>(std::cos(yaw)), c1 = static_cast<float>(std::cos(pitch));
	const float s2 = static_cast<float>(std::sin(roll)), c2 = static_cast<float>(std::cos(roll));
	const float t1 = static_cast<float>(static_cast<double>(s2) * s1);
	const float t2 = static_cast<float>(static_cast<double>(c2) * s1);
	const float m0 = static_cast<float>((static_cast<double>(s2) * s1) * s0 + static_cast<double>(c2) * c0);
	const float m1 = static_cast<float>((static_cast<double>(c2) * s1) * s0 - static_cast<double>(s2) * c0);
	const float m2 = static_cast<float>(static_cast<double>(c1) * s0);
	const float m4 = static_cast<float>(static_cast<double>(s2) * c1);
	const float m5 = static_cast<float>(static_cast<double>(c2) * c1);
	const float m6 = static_cast<float>(-s1);
	const float m8 = static_cast<float>(static_cast<double>(t1) * c0 - static_cast<double>(c2) * s0);
	const float m9 = static_cast<float>(static_cast<double>(t2) * c0 + static_cast<double>(s2) * s0);
	const float m10 = static_cast<float>(static_cast<double>(c1) * c0);
	const float nx = static_cast<float>((static_cast<double>(m0) * x + static_cast<double>(m1) * y) + static_cast<double>(m2) * z);
	const float ny = static_cast<float>((static_cast<double>(m5) * y + static_cast<double>(m6) * z) + static_cast<double>(m4) * x);
	const float nz = static_cast<float>((static_cast<double>(m9) * y + static_cast<double>(m10) * z) + static_cast<double>(m8) * x);
	v.a->value = nx;
	v.b->value = ny;
	v.c->value = nz;
	return VmValue{receiver};
}

NativeResult yprFromVector3(const std::vector<VmValue>& args, const VmObject* receiver, VmArena*,
                            const std::vector<const TufaClass*>*) {
	Triple vector, angles;
	if (args.size() != 1 || !triple(object(args[0]), "x", "y", "z", vector) ||
	    !triple(receiver, "y", "p", "r", angles)) return std::string{"invalid Ypr constructor call"};
	const double x = get(vector.a), y = get(vector.b), z = get(vector.c);
	if ((x == 0.0 || std::isnan(x)) && (z == 0.0 || std::isnan(z))) {
		angles.a->value = 0.0f;
		angles.b->value = y > 0.0 ? 1.5707964f : -1.5707964f;
	} else {
		angles.a->value = static_cast<float>(std::atan2(-x, -z));
		angles.b->value = static_cast<float>(std::atan2(y, std::sqrt(x * x + z * z)));
	}
	angles.c->value = 0.0f;
	return std::monostate{};
}
