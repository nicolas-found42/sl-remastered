#include "native_lang_support.hpp"
#include "natives/lang_string.hpp"

#include <cmath>
#include <limits>

int main() {
	const auto stringClass = loadStockClass("String");
	if (!stringClass) return 1;
	std::vector<const TufaClass*> classes{stringClass.get()};
	VmArena arena;
	const auto str = [&](const std::string& text) { return addString(arena, *stringClass, text); };
	const auto text = [&](const NativeResult& result) -> std::string {
		const auto* t = textOf(arena, result);
		return t ? *t : std::string{"<not a string>"};
	};

	// Integer.toString, handler 00481e30: "%d" through the custom formatter at 00554170.
	const auto integer = [&](int32_t v) { return text(integerToString({v}, nullptr, &arena, &classes)); };
	CHECK(integer(0) == "0");
	CHECK(integer(606) == "606");
	CHECK(integer(-42) == "-42");
	CHECK(integer(2147483647) == "2147483647");
	CHECK(integer(std::numeric_limits<int32_t>::min()) == "-./,),(-*,(");
	CHECK(isError(integerToString({1.0f}, nullptr, &arena, &classes)));
	CHECK(isError(integerToString({}, nullptr, &arena, &classes)));
	CHECK(isError(integerToString({int32_t{1}}, nullptr, nullptr, &classes)));
	const auto sixSixSix = integerToString({int32_t{606}}, nullptr, &arena, &classes);
	CHECK(std::get<VmValue>(sixSixSix) == std::get<VmValue>(integerToString({int32_t{606}}, nullptr, &arena, &classes)));
	CHECK(stockFormatInt(-5, 5, ' ') == "   -5");
	CHECK(stockFormatInt(-5, 5, '0') == "-0005");
	CHECK(stockFormatInt(7, 4, '0') == "0007");
	CHECK(stockFormatInt(12345, 3, ' ') == "12345");

	// Float.toString, handler 00481dd0: the format string drives the custom %f at 00553d70.
	const auto flt = [&](float v, const char* format) {
		return text(floatToString({v, str(format)}, nullptr, &arena, &classes));
	};
	CHECK(flt(3.5f, "%f") == "3.5");
	CHECK(flt(2.0f, "%f") == "2.0");
	CHECK(flt(0.75f, "%.1f") == "0.7");
	CHECK(flt(0.25f, "%.1f") == "0.2");
	CHECK(flt(1.0f, "%.3f") == "1.000");
	CHECK(flt(2.7f, "%.0f") == "2");
	CHECK(flt(3.14159f, "%5.2f") == "    3.14");
	CHECK(flt(2.5f, "%05.1f") == "00002.5");
	CHECK(flt(-1.5f, "%f") == "-1.5");
	CHECK(flt(-0.0f, "%f") == "0.0");
	CHECK(flt(-2.0f, "%4f") == "-   2.0");
	CHECK(flt(1.5f, "x=%.2f%%") == "x=1.50%");
	CHECK(flt(0.1f, "%.10f") == "0.10000000");
	CHECK(flt(0.1f, "%f") == "0.1");
	CHECK(flt(123456.789f, "%.3f") == "123456.789");
	CHECK(flt(-7.25f, "%.1f") == "-7.2");
	CHECK(flt(0.00001f, "%f") == "0.00001");
	CHECK(flt(16777216.0f, "%f") == "16777216.0");
	CHECK(flt(1.5f, "%e") == "");
	CHECK(flt(1.5f, "%-3f") == "3f");
	CHECK(flt(1.5f, "plain") == "plain");
	CHECK(flt(1.5f, "%lf") == "1.5");
	CHECK(flt(std::numeric_limits<float>::quiet_NaN(), "%f") == "-0.0");
	CHECK(flt(std::numeric_limits<float>::infinity(), "%f") == "0.00000001");
	CHECK(flt(-std::numeric_limits<float>::infinity(), "%f") == "-0.00000001");
	CHECK(flt(1e10f, "%f") == "1410065408.00000001");
	CHECK(flt(3e9f, "%f") == "-1294967296.00000001");
	CHECK(flt(3.4028235e38f, "%f") == "0.00000001");
	CHECK(isError(floatToString({1.5f, str("%d")}, nullptr, &arena, &classes)));
	CHECK(isError(floatToString({1.5f, str("%s")}, nullptr, &arena, &classes)));
	CHECK(isError(floatToString({1.5f, str("%f %f")}, nullptr, &arena, &classes)));
	CHECK(isError(floatToString({1.5f, str("abc%")}, nullptr, &arena, &classes)));
	CHECK(isError(floatToString({1.5f, str("%5")}, nullptr, &arena, &classes)));
	CHECK(isError(floatToString({1.5f, str("%1048577f")}, nullptr, &arena, &classes)));
	CHECK(isError(floatToString({1.5f, str("%2000f")}, nullptr, &arena, &classes)));
	CHECK(isError(floatToString({1.5f, static_cast<const VmObject*>(nullptr)}, nullptr, &arena, &classes)));
	CHECK(isError(floatToString({int32_t{1}, str("%f")}, nullptr, &arena, &classes)));
	CHECK(isError(floatToString({1.5f}, nullptr, &arena, &classes)));
	std::string out, error;
	CHECK(stockFormatFloat("%1020f", 0.0f, out, error) && out.size() == 1022);
	CHECK(!stockFormatFloat("%1022f", 0.0f, out, error));

	// String.length, handler 00481fe0.
	const auto length = [&](const VmObject* s) { return stringLength({}, s, &arena, &classes); };
	CHECK(std::get<int32_t>(std::get<VmValue>(length(str("hello")))) == 5);
	CHECK(std::get<int32_t>(std::get<VmValue>(length(str("")))) == 0);
	CHECK(isError(length(nullptr)));
	VmObject stranger;
	CHECK(isError(length(&stranger)));
	CHECK(isError(stringLength({int32_t{1}}, str("x"), &arena, &classes)));

	// String.append(I), handler 00481f50.
	const auto append = [&](const std::string& base, int32_t code) {
		return stringAppend({code}, str(base), &arena, &classes);
	};
	CHECK(text(append("ab", 'c')) == "abc");
	CHECK(text(append("abc", 8)) == "ab");
	CHECK(text(append("", 8)) == "");
	CHECK(text(append("ab", 0x1f)) == "ab");
	CHECK(text(append("ab", -5)) == "ab");
	CHECK(text(append("ab", 0x141)) == "abA");
	CHECK(text(append("ab", 0x100)) == "ab");
	CHECK(text(append("ab", 0x20)) == "ab ");
	CHECK(text(append("ab", 0xe9)) == std::string("ab") + static_cast<char>(0xe9));
	const std::string longText(300, 'q');
	CHECK(text(append(longText, 0x1f)) == std::string(255, 'q'));
	CHECK(text(append(longText, 8)) == std::string(254, 'q'));
	CHECK(isError(append(longText, 'x')));
	CHECK(text(append(std::string(254, 'q'), 'x')) == std::string(254, 'q') + "x");
	const VmObject* same = str("same");
	CHECK(std::get<VmValue>(stringAppend({int32_t{0x1f}}, same, &arena, &classes)) == VmValue{same});
	CHECK(isError(stringAppend({1.0f}, same, &arena, &classes)));
	CHECK(isError(stringAppend({int32_t{65}}, nullptr, &arena, &classes)));

	// String.token(ILjava.lang.String;), handler 00481cf0 with strtok at 00554c70.
	const auto token = [&](const std::string& base, int32_t index, const std::string& delimiters) {
		return stringToken({index, str(delimiters)}, str(base), &arena, &classes);
	};
	CHECK(text(token("a,b;c", 0, ",;")) == "a");
	CHECK(text(token("a,b;c", 1, ",;")) == "b");
	CHECK(text(token("a,b;c", 2, ",;")) == "c");
	CHECK(isNull(token("a,b;c", 3, ",;")));
	CHECK(text(token(",,a,,b,", 1, ",")) == "b");
	CHECK(isNull(token(",,a,,b,", 2, ",")));
	CHECK(isNull(token("", 0, ",")));
	CHECK(isNull(token(",,,", 0, ",")));
	CHECK(text(token("abc", 0, "")) == "abc");
	CHECK(isNull(token("abc", 1, "")));
	CHECK(isNull(token("a b", -1, " ")));
	CHECK(text(token("hello world  foo", 2, " ")) == "foo");
	CHECK(text(token(longText + " z", 1, " ")) == "z");
	CHECK(text(token(longText + " z", 0, " ")) == longText);
	const VmObject* alpha = str("alpha");
	CHECK(std::get<VmValue>(token("alpha beta", 0, " ")) == VmValue{alpha});
	CHECK(isError(stringToken({int32_t{0}, static_cast<const VmObject*>(nullptr)}, str("a"), &arena, &classes)));
	CHECK(isError(stringToken({int32_t{0}, str(",")}, nullptr, &arena, &classes)));
	CHECK(isError(stringToken({int32_t{0}}, str("a"), &arena, &classes)));

	const auto number = [&](const char* input) { return stringIntValue({}, str(input), &arena, &classes); };
	CHECK(std::get<int32_t>(std::get<VmValue>(number("  -123tail"))) == -123);
	CHECK(std::get<int32_t>(std::get<VmValue>(number("0xFF"))) == 255);
	CHECK(std::get<int32_t>(std::get<VmValue>(number("4294967295"))) == -1);
	CHECK(isError(number("+1")) && isError(number("0x")) && isError(number("word")));

  // String.floatValue, handler 00481f10: sscanf(text, "%f", &value) on the
  // receiver's bytes; a non-numeric string is an error because the stock
  // result would be uninitialized.
  const auto floatNumber = [&](const char* input) { return stringFloatValue({}, str(input), &arena, &classes); };
  CHECK(std::get<float>(std::get<VmValue>(floatNumber("12.5"))) == 12.5f);
  CHECK(std::get<float>(std::get<VmValue>(floatNumber("  -2.75tail"))) == -2.75f);
  CHECK(isError(floatNumber("word")));
  CHECK(isError(floatNumber("")));
  CHECK(isError(stringFloatValue({int32_t{1}}, str("1.0"), &arena, &classes)));
  CHECK(isError(stringFloatValue({}, nullptr, &arena, &classes)));

	// The result needs the registered String class.
	std::vector<const TufaClass*> none;
	CHECK(isError(integerToString({int32_t{1}}, nullptr, &arena, &none)));
	VmArena small;
	small.limit = 0;
	CHECK(isError(integerToString({int32_t{1}}, nullptr, &small, &classes)));
	return failures ? 1 : 0;
}
