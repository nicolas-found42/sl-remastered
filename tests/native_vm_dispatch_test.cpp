#include "natives/registry.hpp"
#include "tufa.hpp"
#include "vm.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

// Each native runs through vmExecute with a synthetic caller tree. The inventory printed at the end
// lists a row only when every check made for it passed and at least one call reached its handler.

namespace {

struct Row {
	std::string className, method, descriptor;
	int checks = 0, failures = 0, calls = 0;
	bool vmGap = false;
};

std::vector<Row> rows;
Row* current = nullptr;
int totalFailures = 0;

void check(bool ok, int line, const char* text) {
	if (current) ++current->checks;
	if (ok) return;
	if (current) ++current->failures;
	++totalFailures;
	std::printf("FAIL line %d (%s): %s\n", line, current ? current->method.c_str() : "-", text);
}
#define CHECK(cond) check((cond), __LINE__, #cond)

const VmObject* const kNull = nullptr;

struct Library {
	std::map<std::string, std::unique_ptr<TufaClass>> classes;
	std::vector<const TufaClass*> list;
	VmArena arena;

	bool load(const std::string& name) {
		const auto parsed = tufaLoadFile("ref/raw/system/Scripts/lang/" + name + ".class");
		if (!parsed.ok || parsed.file.classes.empty()) {
			std::printf("cannot load %s.class\n", name.c_str());
			return false;
		}
		classes[name] = std::make_unique<TufaClass>(parsed.file.classes[0]);
		list.push_back(classes[name].get());
		return true;
	}
	const TufaClass& cls(const std::string& name) const { return *classes.at(name); }

	const VmObject* make(const TufaClass* runtimeClass) {
		auto object = std::make_unique<VmObject>();
		object->runtimeClass = runtimeClass;
		if (runtimeClass)
			for (const auto& fields : runtimeClass->fields) for (const auto& field : fields) {
				if (field.words[0] & 8) continue;
				const auto& type = runtimeClass->constants[field.words[2]].text;
				VmValue value = type == "F" ? VmValue{0.0f} : type == "I" ? VmValue{int32_t{0}} : VmValue{kNull};
				object->fields.push_back({runtimeClass->constants[field.words[1]].text, value, type});
			}
		arena.objects.push_back(std::move(object));
		return arena.objects.back().get();
	}
	const VmObject* str(const std::string& text) {
		const VmObject* object = make(&cls("String"));
		arena.strings.emplace_back(text, object);
		return object;
	}
	const VmObject* array(const std::string& descriptor, std::vector<VmValue> elements) {
		auto object = std::make_unique<VmObject>();
		object->arrayDescriptor = descriptor;
		object->arrayElements = std::move(elements);
		arena.objects.push_back(std::move(object));
		return arena.objects.back().get();
	}
	const VmObject* vec(float x, float y, float z) {
		const VmObject* object = make(&cls("Vector3"));
		for (auto& field : object->fields) field.value = field.name == "x" ? x : field.name == "y" ? y : z;
		return object;
	}
	const VmObject* ypr(float yaw, float pitch, float roll) {
		const VmObject* object = make(&cls("Ypr"));
		for (auto& field : object->fields) field.value = field.name == "y" ? yaw : field.name == "p" ? pitch : roll;
		return object;
	}
	const std::string* text(const VmValue& value) const {
		const auto* object = std::get_if<const VmObject*>(&value);
		if (!object || !*object) return nullptr;
		for (const auto& entry : arena.strings) if (entry.second == *object) return &entry.first;
		return nullptr;
	}
};

Library lib;

TufaNode literal(int32_t value) {
	uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	return {0x27, 0, true, bits};
}
TufaNode local(uint32_t index) { return {0x01, 0, true, index}; }

void begin(const char* className, const char* method, const char* descriptor) {
	rows.push_back({className, method, descriptor});
	current = &rows.back();
	for (const auto& entry : nativeRegistry())
		if (entry.className == className && entry.method == method && entry.descriptor == descriptor) return;
	check(false, __LINE__, "row is registered");
}

// Appends a method reference for `method` to a copy of the declaring class and runs a caller tree
// that pushes the arguments and calls it. An instance native gets `receiver` as the current `this`.
VmResult call(const std::string& shortName, const std::string& method, const std::string& descriptor,
              const VmObject* receiver, const std::vector<VmValue>& args,
              bool continueAfterVoid = false) {
	TufaClass& host = const_cast<TufaClass&>(lib.cls(shortName));
	uint32_t name = 0, signature = 0, flags = 0;
	for (const auto& list : host.methods) for (const auto& entry : list)
		if (host.constants[entry.words[1]].text == method && host.constants[entry.words[2]].text == descriptor) {
			name = entry.words[1];
			signature = entry.words[2];
			flags = entry.words[0];
		}
	if (!name) { check(false, __LINE__, "native is declared by the stock class"); return {}; }
	const auto add = [&](uint32_t kind, const std::string& text, uint32_t a, uint32_t b) {
		host.constants.push_back({kind, text, a, b});
		return static_cast<uint32_t>(host.constants.size() - 1);
	};
	const uint32_t owner = add(4, {}, add(0, host.name, 0, 0), 0);
	const uint32_t ref = add(5, {}, owner, add(7, {}, name, signature));
	std::vector<TufaNode> tree;
	std::vector<VmValue> locals;
	const bool isStatic = (flags & 8) != 0;
	if (!isStatic) locals.push_back(receiver);
	for (const auto& arg : args) {
		if (const auto* i = std::get_if<int32_t>(&arg)) tree.push_back(literal(*i));
		else if (const auto* f = std::get_if<float>(&arg)) {
			uint32_t bits;
			std::memcpy(&bits, f, sizeof(bits));
			tree.push_back({0x07, 0, true, 28});
			tree.push_back({0x0a, 0, true, bits});
		} else if (const auto* o = std::get_if<const VmObject*>(&arg)) {
			if (*o) {
				tree.push_back(local(static_cast<uint32_t>(locals.size())));
				locals.push_back(*o);
			} else {
				tree.push_back({0x07, 0, true, 28});
				tree.push_back({0x0c, 0, false, 0});
			}
		}
	}
	tree.push_back({0x27, 0, true, static_cast<uint32_t>(args.size())});
	tree.push_back({0x24, 0, false, 0});
	tree.push_back({0x1a, 0, true, ref});
	if (continueAfterVoid) tree.push_back(literal(99));
	tree.push_back(descriptor.back() == 'V' && !continueAfterVoid ? TufaNode{0x2a, 0, false, 0} : TufaNode{0x2b, 0, false, 0});
	const auto result = vmExecute(host, tree, locals, &nativeRegistry(), &lib.list, nullptr, &lib.arena);
	if (current && result.ok) ++current->calls;
	if (!result.ok && std::getenv("DBG")) std::printf("  err[%s]: %s\n", method.c_str(), result.error.c_str());
	return result;
}

bool isInt(const VmResult& r, int32_t expected) {
	const auto* v = std::get_if<int32_t>(&r.value);
	return r.ok && !r.voidReturn && v && *v == expected;
}
bool isFloat(const VmResult& r, float expected) {
	const auto* v = std::get_if<float>(&r.value);
	return r.ok && !r.voidReturn && v && *v == expected;
}
bool isText(const VmResult& r, const std::string& expected) {
	const auto* t = r.ok ? lib.text(r.value) : nullptr;
	return t && *t == expected;
}
bool isNullRef(const VmResult& r) {
	const auto* o = std::get_if<const VmObject*>(&r.value);
	return r.ok && o && !*o;
}
bool isVoid(const VmResult& r) { return r.ok && r.voidReturn; }
float field(const VmObject* object, const char* name) {
	for (const auto& f : object->fields) if (f.name == name) return std::get<float>(f.value);
	return std::nanf("");
}

const std::string kStr = "Ljava.lang.String;";

void integerToString() {
	begin("java.lang.Integer", "toString", "(I)Ljava.lang.String;");
	const auto run = [&](int32_t v) { return call("Integer", "toString", "(I)Ljava.lang.String;", lib.make(&lib.cls("Integer")), {v}); };
	CHECK(isText(run(0), "0"));
	CHECK(isText(run(606), "606"));
	CHECK(isText(run(-42), "-42"));
	CHECK(isText(run(2147483647), "2147483647"));
	CHECK(isText(run(std::numeric_limits<int32_t>::min()), "-./,),(-*,("));
	const auto a = run(606), b = run(606);
	CHECK(a.ok && b.ok && std::get<const VmObject*>(a.value) == std::get<const VmObject*>(b.value));
	CHECK(isText(call("Integer", "toString", "(I)Ljava.lang.String;", lib.make(&lib.cls("Integer")), {1.5f}), "1"));
}

void floatToString() {
	begin("java.lang.Float", "toString", "(FLjava.lang.String;)Ljava.lang.String;");
	const auto run = [&](float v, const char* format) {
		return call("Float", "toString", "(FLjava.lang.String;)Ljava.lang.String;", lib.make(&lib.cls("Float")), {v, lib.str(format)});
	};
	CHECK(isText(run(3.5f, "%f"), "3.5"));
	CHECK(isText(run(0.75f, "%.1f"), "0.7"));
	CHECK(isText(run(1.5f, "x=%.2f%%"), "x=1.50%"));
	CHECK(isText(run(-1.5f, "%f"), "-1.5"));
	CHECK(!run(1.5f, "%d").ok);
	CHECK(!call("Float", "toString", "(FLjava.lang.String;)Ljava.lang.String;", lib.make(&lib.cls("Float")), {1.5f, kNull}).ok);
}

void stringLength() {
	begin("java.lang.String", "length", "()I");
	const auto run = [&](const VmObject* s) { return call("String", "length", "()I", s, {}); };
	CHECK(isInt(run(lib.str("hello")), 5));
	CHECK(isInt(run(lib.str("")), 0));
	CHECK(isInt(run(lib.str(std::string(1000, 'z'))), 1000));
	CHECK(!run(kNull).ok);
	CHECK(!run(lib.make(&lib.cls("String"))).ok);
	CHECK(!run(lib.make(&lib.cls("Vector3"))).ok);
}

void stringAppend() {
	begin("java.lang.String", "append", "(I)Ljava.lang.String;");
	const auto run = [&](const std::string& base, int32_t code) {
		return call("String", "append", "(I)Ljava.lang.String;", lib.str(base), {code});
	};
	CHECK(isText(run("ab", 'c'), "abc"));
	CHECK(isText(run("abc", 8), "ab"));
	CHECK(isText(run("", 8), ""));
	CHECK(isText(run("ab", 0x1f), "ab"));
	CHECK(isText(run("ab", 0x141), "abA"));
	CHECK(isText(run("ab", 0x20), "ab "));
	const std::string longText(300, 'q');
	CHECK(isText(run(longText, 0x1f), std::string(255, 'q')));
	CHECK(isText(run(std::string(254, 'q'), 'x'), std::string(254, 'q') + "x"));
	CHECK(!run(longText, 'x').ok);
	const VmObject* same = lib.str("same");
	const auto kept = call("String", "append", "(I)Ljava.lang.String;", same, {int32_t{0x1f}});
	CHECK(kept.ok && std::get<const VmObject*>(kept.value) == same);
	CHECK(isText(call("String", "append", "(I)Ljava.lang.String;", same, {65.5f}), "sameA"));
	CHECK(!call("String", "append", "(I)Ljava.lang.String;", kNull, {int32_t{65}}).ok);
}

void stringToken() {
	begin("java.lang.String", "token", "(ILjava.lang.String;)Ljava.lang.String;");
	const auto run = [&](const std::string& base, int32_t index, const std::string& delimiters) {
		return call("String", "token", "(ILjava.lang.String;)Ljava.lang.String;", lib.str(base), {index, lib.str(delimiters)});
	};
	CHECK(isText(run("a,b;c", 0, ",;"), "a"));
	CHECK(isText(run("a,b;c", 1, ",;"), "b"));
	CHECK(isText(run("a,b;c", 2, ",;"), "c"));
	CHECK(isNullRef(run("a,b;c", 3, ",;")));
	CHECK(isText(run(",,a,,b,", 1, ","), "b"));
	CHECK(isNullRef(run("", 0, ",")));
	CHECK(isNullRef(run("a b", -1, " ")));
	CHECK(isText(run("hello world  foo", 2, " "), "foo"));
	CHECK(isText(run("abc", 0, ""), "abc"));
	const VmObject* alpha = lib.str("alpha");
	const auto again = run("alpha beta", 0, " ");
	CHECK(again.ok && std::get<const VmObject*>(again.value) == alpha);
	CHECK(!call("String", "token", "(ILjava.lang.String;)Ljava.lang.String;", lib.str("a"), {int32_t{0}, kNull}).ok);
	CHECK(!call("String", "token", "(ILjava.lang.String;)Ljava.lang.String;", lib.str("a"), {int32_t{0}, int32_t{1}}).ok);
	CHECK(!call("String", "token", "(ILjava.lang.String;)Ljava.lang.String;", kNull, {int32_t{0}, lib.str(",")}).ok);
}

void vector3Length() {
	begin("java.lang.Vector3", "length", "()F");
	const auto run = [&](const VmObject* v) { return call("Vector3", "length", "()F", v, {}); };
	CHECK(isFloat(run(lib.vec(3, 4, 0)), 5.0f));
	CHECK(isFloat(run(lib.vec(1, 2, 2)), 3.0f));
	CHECK(isFloat(run(lib.vec(0, 0, 0)), 0.0f));
	CHECK(isFloat(run(lib.vec(0, 0, -7)), 7.0f));
	const auto inf = run(lib.vec(std::numeric_limits<float>::infinity(), 0, 0));
	CHECK(inf.ok && std::isinf(std::get<float>(inf.value)));
	CHECK(!run(kNull).ok);
	CHECK(!run(lib.make(&lib.cls("Ypr"))).ok);
}

void vector3Distance() {
	begin("java.lang.Vector3", "distance", "(Ljava.lang.Vector3;)F");
	const auto run = [&](const VmObject* a, const VmValue& b) { return call("Vector3", "distance", "(Ljava.lang.Vector3;)F", a, {b}); };
	CHECK(isFloat(run(lib.vec(1, 2, 3), lib.vec(4, 6, 3)), 5.0f));
	CHECK(isFloat(run(lib.vec(1, 2, 3), lib.vec(1, 2, 3)), 0.0f));
	CHECK(isFloat(run(lib.vec(1, 2, 2), lib.vec(-1, -2, -2)), 6.0f));
	CHECK(!run(lib.vec(1, 2, 3), kNull).ok);
	CHECK(!run(lib.vec(1, 2, 3), lib.ypr(0, 0, 0)).ok);
	CHECK(!run(lib.vec(1, 2, 3), int32_t{1}).ok);
}

void vector3Normalize() {
	begin("java.lang.Vector3", "normalize", "()V");
	const VmObject* n = lib.vec(0, 3, 4);
	CHECK(isVoid(call("Vector3", "normalize", "()V", n, {})));
	CHECK(field(n, "x") == 0.0f && field(n, "y") == 0.6f && field(n, "z") == 0.8f);
	const VmObject* unit = lib.vec(2, 0, 0);
	CHECK(isVoid(call("Vector3", "normalize", "()V", unit, {})));
	CHECK(field(unit, "x") == 1.0f && field(unit, "y") == 0.0f);
	const VmObject* zero = lib.vec(0, 0, 0);
	CHECK(isVoid(call("Vector3", "normalize", "()V", zero, {})));
	CHECK(field(zero, "x") == 0.0f && field(zero, "y") == 0.0f && field(zero, "z") == 0.0f);
	const VmObject* nan = lib.vec(std::nanf(""), 1, 1);
	CHECK(isVoid(call("Vector3", "normalize", "()V", nan, {})));
	CHECK(std::isnan(field(nan, "x")) && field(nan, "y") == 1.0f && field(nan, "z") == 1.0f);
	CHECK(!call("Vector3", "normalize", "()V", kNull, {}).ok);
}

void vector3Rotate() {
	begin("java.lang.Vector3", "rotate", "(Ljava.lang.Ypr;)Ljava.lang.Vector3;");
	const auto run = [&](const VmObject* v, const VmValue& angles) {
		return call("Vector3", "rotate", "(Ljava.lang.Ypr;)Ljava.lang.Vector3;", v, {angles});
	};
	const VmObject* r = lib.vec(1, 2, 3);
	const auto zeroTurn = run(r, lib.ypr(0, 0, 0));
	CHECK(zeroTurn.ok && std::get<const VmObject*>(zeroTurn.value) == r);
	CHECK(field(r, "x") == 1.0f && field(r, "y") == 2.0f && field(r, "z") == 3.0f);
	const float quarter = 1.5707964f;
	const VmObject* q = lib.vec(1, 2, 3);
	CHECK(run(q, lib.ypr(quarter, 0, 0)).ok);
	CHECK(std::fabs(field(q, "x") - 3.0f) < 1e-6f && std::fabs(field(q, "y") - 2.0f) < 1e-6f && std::fabs(field(q, "z") + 1.0f) < 1e-6f);
	const VmObject* p = lib.vec(1, 2, 3);
	CHECK(run(p, lib.ypr(0, quarter, 0)).ok);
	CHECK(std::fabs(field(p, "y") + 3.0f) < 1e-6f);
	const VmObject* kept = lib.vec(1, 2, 3);
	CHECK(run(kept, lib.ypr(0.3f, -0.7f, 1.1f)).ok);
	const float x = field(kept, "x"), y = field(kept, "y"), z = field(kept, "z");
	CHECK(std::fabs(std::sqrt(x * x + y * y + z * z) - std::sqrt(14.0f)) < 1e-5f);
	CHECK(!run(lib.vec(1, 2, 3), kNull).ok);
	CHECK(!run(lib.vec(1, 2, 3), lib.vec(0, 0, 0)).ok);
	CHECK(!run(kNull, lib.ypr(0, 0, 0)).ok);
}

void systemArraycopy() {
	begin("java.lang.System", "arraycopy", "(Ljava.lang.Object;ILjava.lang.Object;II)V");
	const std::string d = "(Ljava.lang.Object;ILjava.lang.Object;II)V";
	const auto ints = [&](std::initializer_list<int32_t> values) {
		std::vector<VmValue> elements;
		for (auto v : values) elements.push_back(v);
		return lib.array("[I", elements);
	};
	const auto contents = [](const VmObject* a) {
		std::vector<int32_t> out;
		for (const auto& v : a->arrayElements) out.push_back(std::get<int32_t>(v));
		return out;
	};
	const auto run = [&](const VmObject* s, int32_t sp, const VmObject* t, int32_t tp, int32_t n) {
		return call("System", "arraycopy", d, kNull, {s, sp, t, tp, n});
	};
	const VmObject* src = ints({1, 2, 3, 4});
	const VmObject* dst = ints({9, 9, 9, 9});
	const auto probe = run(src, 0, dst, 0, 1);
	if (!probe.ok && probe.error.rfind("native argument type mismatch", 0) == 0) {
		current->vmGap = true;
		std::fprintf(stderr, "gap: the VM refuses an array for a java.lang.Object parameter, so System.arraycopy has no VM-dispatch row yet (%s)\n", probe.error.c_str());
		return;
	}
	CHECK(isVoid(run(src, 1, dst, 0, 3)));
	CHECK((contents(dst) == std::vector<int32_t>{2, 3, 4, 9}));
	CHECK((contents(src) == std::vector<int32_t>{1, 2, 3, 4}));
	const VmObject* same = ints({1, 2, 3, 4, 5});
	CHECK(isVoid(run(same, 0, same, 1, 4)));
	CHECK((contents(same) == std::vector<int32_t>{1, 1, 2, 3, 4}));
	CHECK(isVoid(run(same, 1, same, 0, 4)));
	CHECK((contents(same) == std::vector<int32_t>{1, 2, 3, 4, 4}));
	CHECK(isVoid(run(src, 0, dst, 0, 0)));
	CHECK(!run(src, 2, dst, 0, 3).ok);
	CHECK(!run(src, 0, dst, 2, 3).ok);
	CHECK(!run(src, -1, dst, 0, 1).ok);
	CHECK(!run(src, 0, dst, 0, -1).ok);
	CHECK(!run(kNull, 0, dst, 0, 1).ok);
	CHECK(!run(src, 0, lib.array("[F", {1.0f}), 0, 1).ok);
	const VmObject* word = lib.str("x");
	const VmObject* refs = lib.array("[Ljava.lang.String;", {word, kNull});
	const VmObject* out = lib.array("[Ljava.lang.String;", {kNull, kNull});
	CHECK(isVoid(run(refs, 0, out, 0, 2)));
	CHECK(std::get<const VmObject*>(out->arrayElements[0]) == word && !std::get<const VmObject*>(out->arrayElements[1]));
}

void systemExit() {
	begin("java.lang.System", "exit", "()V");
	CHECK(!lib.arena.exitRequested);
	CHECK(isVoid(call("System", "exit", "()V", kNull, {})));
	CHECK(lib.arena.exitRequested);
	CHECK(isVoid(call("System", "exit", "()V", kNull, {})));
	CHECK(isInt(call("System", "exit", "()V", kNull, {}, true), 99));
	begin("java.lang.System", "exit", "(Ljava.lang.String;)V");
	const std::string d = "(Ljava.lang.String;)V";
	CHECK(isVoid(call("System", "exit", d, kNull, {lib.str("bye")})));
	CHECK(isVoid(call("System", "exit", d, kNull, {kNull})));
	CHECK(!call("System", "exit", d, kNull, {int32_t{1}}).ok);
	CHECK(!call("System", "exit", d, kNull, {lib.make(&lib.cls("Vector3"))}).ok);
}

void systemLog() {
	begin("java.lang.System", "log", "(Ljava.lang.String;)V");
	const std::string d = "(Ljava.lang.String;)V";
	CHECK(isVoid(call("System", "log", d, kNull, {lib.str("message")})));
	CHECK(isVoid(call("System", "log", d, kNull, {kNull})));
	CHECK(!call("System", "log", d, kNull, {1.5f}).ok);
}

std::string json(const std::string& value) {
	std::string out = "\"";
	for (char c : value) {
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	return out + "\"";
}

}  // namespace

int main() {
	for (const char* name : {"String", "Integer", "Float", "Vector3", "Ypr", "System"})
		if (!lib.load(name)) return 1;
	rows.reserve(32);
	integerToString();
	floatToString();
	stringLength();
	stringAppend();
	stringToken();
	vector3Length();
	vector3Distance();
	vector3Normalize();
	vector3Rotate();
	systemArraycopy();
	systemExit();
	systemLog();
	if (totalFailures) {
		std::printf("%d failure(s)\n", totalFailures);
		return 1;
	}
	std::string out = "[";
	for (const auto& row : rows) {
		if (row.vmGap) continue;
		if (row.failures || row.calls == 0 || row.checks == 0) {
			std::printf("row without a passing VM call: %s.%s\n", row.className.c_str(), row.method.c_str());
			return 1;
		}
		const NativeEntry* entry = nullptr;
		for (const auto& candidate : nativeRegistry())
			if (candidate.className == row.className && candidate.method == row.method && candidate.descriptor == row.descriptor) entry = &candidate;
		if (!entry) return 1;
		if (out.size() > 1) out += ",";
		out += "{\"class\":" + json(row.className) + ",\"method\":" + json(row.method) + ",\"descriptor\":" + json(row.descriptor) +
		       ",\"handler\":" + json(std::string(entry->handlerAddress)) + ",\"coverage\":\"vm-dispatch\"}";
	}
	std::puts((out + "]").c_str());
	return 0;
}
