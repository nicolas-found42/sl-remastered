#include "natives/lang_string.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
const std::string *stringBytes(const VmArena *arena, const VmObject *object) {
  if (!arena || !object)
    return nullptr;
  const std::string *found = nullptr;
  for (const auto &entry : arena->strings)
    if (entry.second == object) {
      if (found)
        return nullptr;
      found = &entry.first;
    }
  return found;
}

// Same interning rule as 004174a0: equal bytes give the same String object.
NativeResult internResult(const std::string &bytes, VmArena *arena,
                          const std::vector<const TufaClass *> *classes) {
  if (!arena || !classes)
    return std::string{"string result context unavailable"};
  const TufaClass *stringClass = nullptr;
  for (const auto *candidate : *classes)
    if (candidate && candidate->name == "java.lang.String")
      stringClass = candidate;
  if (!stringClass)
    return std::string{"string class not registered"};
  for (const auto &entry : arena->strings)
    if (entry.first == bytes && entry.second->runtimeClass == stringClass)
      return VmValue{entry.second};
  if (arena->objects.size() >= arena->limit)
    return std::string{"object arena full"};
  auto object = std::make_unique<VmObject>();
  object->runtimeClass = stringClass;
  const VmObject *identity = object.get();
  arena->objects.push_back(std::move(object));
  arena->strings.emplace_back(bytes, identity);
  return VmValue{identity};
}

// FISTP to 64 bits with truncation, then the low 32 bits, as __ftol at 005d6750
// returns in EAX.
int32_t ftolLow32(double value) {
  int64_t wide = std::numeric_limits<int64_t>::min();
  if (value >= -9223372036854775808.0 && value < 9223372036854775808.0)
    wide = static_cast<int64_t>(value);
  return static_cast<int32_t>(
      static_cast<uint32_t>(static_cast<uint64_t>(wide) & 0xffffffffu));
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
} // namespace

std::string stockFormatInt(int32_t value, int32_t width, char pad) {
  const bool negative = value < 0;
  int64_t rest = negative ? -static_cast<int64_t>(value) : value;
  // The stock code negates in 32 bits, so INT_MIN stays negative and yields
  // negative digits.
  if (negative && value == std::numeric_limits<int32_t>::min())
    rest = value;
  std::string digits;
  do {
    digits.insert(digits.begin(), static_cast<char>(rest % 10 + '0'));
    rest /= 10;
  } while (rest != 0);
  std::string out;
  const auto count = static_cast<int32_t>(digits.size());
  if (negative) {
    if (width > 0)
      --width;
    if (pad != '0' && count < width) {
      out.append(static_cast<size_t>(width - count), pad);
      width = count;
    }
    out += '-';
  }
  if (count < width)
    out.append(static_cast<size_t>(width - count), pad);
  return out + digits;
}

bool stockFormatFloat(std::string_view format, float value, std::string &out,
                      std::string &error) {
  out.clear();
  const double argument = static_cast<double>(value);
  bool consumed = false;
  size_t i = 0;
  const auto fail = [&](const char *message) {
    error = message;
    return false;
  };
  char c = 0;
  const auto next = [&]() {
    if (i >= format.size())
      return false;
    c = format[i++];
    return true;
  };
  while (i < format.size()) {
    c = format[i++];
    if (c != '%') {
      out += c;
      continue;
    }
    if (!next())
      return fail("Float.toString format ends inside a conversion");
    int64_t width = 0, precision = -1;
    char pad = ' ';
    if (c == '0') {
      pad = '0';
      if (!next())
        return fail("Float.toString format ends inside a conversion");
    }
    while (isDigit(c)) {
      width = width * 10 + (c - '0');
      if (width > (1 << 20))
        return fail("Float.toString width too large");
      if (!next())
        return fail("Float.toString format ends inside a conversion");
    }
    if (c == '.') {
      precision = 0;
      if (!next())
        return fail("Float.toString format ends inside a conversion");
      while (isDigit(c)) {
        precision = precision * 10 + (c - '0');
        if (precision > (1 << 20))
          return fail("Float.toString precision too large");
        if (!next())
          return fail("Float.toString format ends inside a conversion");
      }
    }
    if (c == 'h' || c == 'l' || c == 'L') {
      if (!next())
        return fail("Float.toString format ends inside a conversion");
    }
    switch (c) {
    case '%':
      out += '%';
      break;
    case 'f': {
      if (consumed)
        return fail("Float.toString format reads past its single argument");
      consumed = true;
      double magnitude = argument;
      if (argument < 0.0 || std::isnan(argument)) {
        magnitude = -magnitude;
        out += '-';
      }
      const int32_t whole = ftolLow32(magnitude);
      out += stockFormatInt(whole, static_cast<int32_t>(width), pad);
      if (precision == 0)
        break;
      out += '.';
      const double scaled =
          (magnitude - static_cast<double>(whole)) * 100000000.0;
      int32_t digits = ftolLow32(scaled);
      const double remainder = scaled - static_cast<double>(digits);
      if (remainder >= 0.5)
        digits = static_cast<int32_t>(static_cast<uint32_t>(digits) + 1u);
      const std::string text = stockFormatInt(digits, 8, '0');
      int64_t count = precision;
      if (count > 8)
        count = 8;
      else if (count < 0) {
        count = 1;
        for (int idx = 7; idx > 0; --idx) {
          const char ch = static_cast<size_t>(idx) < text.size()
                              ? text[static_cast<size_t>(idx)]
                              : '\0';
          if (ch != '0') {
            count = idx + 1;
            break;
          }
        }
      }
      const auto copied =
          std::min<size_t>(static_cast<size_t>(count), text.size());
      out.append(text, 0, copied);
      out.append(static_cast<size_t>(count) - copied, '0');
      break;
    }
    case 'd':
    case 'i':
    case 'c':
    case 's':
    case 'x':
    case 'X':
      return fail(
          "Float.toString format reads a stack argument the host cannot model");
    default:
      break;
    }
  }
  if (out.size() > 1023)
    return fail("Float.toString result overflows the 1024 byte stock buffer");
  return true;
}

// Default float formatting for VM string concatenation: stock FUN_00417da0
// converts a float operand to String with the "%.3f" format at 0x60ba9c via
// FUN_00551220 -> FUN_00553d70 case 0x66. The int operand uses "%d" at
// 0x60ba84.
bool stockFormatFloatConcat(float value, std::string &out, std::string &error) {
  return stockFormatFloat("%.3f", value, out, error);
}

NativeResult integerToString(const std::vector<VmValue> &args, const VmObject *,
                             VmArena *arena,
                             const std::vector<const TufaClass *> *classes) {
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid Integer.toString call"};
  return internResult(stockFormatInt(std::get<int32_t>(args[0]), 0, ' '), arena,
                      classes);
}

NativeResult floatToString(const std::vector<VmValue> &args, const VmObject *,
                           VmArena *arena,
                           const std::vector<const TufaClass *> *classes) {
  if (args.size() != 2 || !std::holds_alternative<float>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]))
    return std::string{"invalid Float.toString call"};
  const std::string *format =
      stringBytes(arena, std::get<const VmObject *>(args[1]));
  if (!format)
    return std::string{"Float.toString format is not a known String"};
  std::string text, error;
  if (!stockFormatFloat(*format, std::get<float>(args[0]), text, error))
    return error;
  return internResult(text, arena, classes);
}

NativeResult stringLength(const std::vector<VmValue> &args,
                          const VmObject *receiver, VmArena *arena,
                          const std::vector<const TufaClass *> *) {
  const std::string *bytes = stringBytes(arena, receiver);
  if (!args.empty() || !bytes ||
      bytes->size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
    return std::string{"invalid String.length call"};
  return VmValue{static_cast<int32_t>(bytes->size())};
}

NativeResult stringAppend(const std::vector<VmValue> &args,
                          const VmObject *receiver, VmArena *arena,
                          const std::vector<const TufaClass *> *classes) {
  const std::string *bytes = stringBytes(arena, receiver);
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]) || !bytes)
    return std::string{"invalid String.append call"};
  const int32_t code = std::get<int32_t>(args[0]);
  // 00551120 copies at most 255 bytes and always terminates the 256 byte stack
  // buffer.
  std::string buffer = bytes->substr(0, 255);
  if (code == 8) {
    if (!buffer.empty())
      buffer.pop_back();
  } else if (code >= 0x20) {
    if (buffer.size() == 255)
      return std::string{
          "String.append would write past the stock 256 byte buffer"};
    const char appended = static_cast<char>(static_cast<uint8_t>(code & 0xff));
    if (appended != '\0')
      buffer += appended;
  }
  return internResult(buffer, arena, classes);
}

NativeResult stringToken(const std::vector<VmValue> &args,
                         const VmObject *receiver, VmArena *arena,
                         const std::vector<const TufaClass *> *classes) {
  const std::string *bytes = stringBytes(arena, receiver);
  if (args.size() != 2 || !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]) || !bytes)
    return std::string{"invalid String.token call"};
  const std::string *delimiters =
      stringBytes(arena, std::get<const VmObject *>(args[1]));
  if (!delimiters)
    return std::string{"String.token delimiters are not a known String"};
  bool delimiter[256] = {};
  for (unsigned char byte : *delimiters)
    delimiter[byte] = true;
  // A strtok that stores its position, as 00554c70 does.
  size_t position = 0;
  const auto nextToken = [&](std::string &token) {
    const auto isDelimiter = [&](size_t index) {
      return index >= bytes->size() ||
             delimiter[static_cast<unsigned char>((*bytes)[index])];
    };
    while (position < bytes->size() && isDelimiter(position))
      ++position;
    const size_t start = position;
    while (position < bytes->size() && !isDelimiter(position))
      ++position;
    const bool found = position > start;
    token = bytes->substr(start, position - start);
    if (position < bytes->size())
      ++position;
    return found;
  };
  std::string token;
  bool found = nextToken(token);
  int32_t remaining = std::get<int32_t>(args[0]);
  while (found) {
    const int32_t before = remaining--;
    if (before == 0)
      break;
    found = nextToken(token);
  }
  if (!found)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  return internResult(token, arena, classes);
}

NativeResult internStringResult(const std::string &bytes, VmArena *arena,
                                const std::vector<const TufaClass *> *classes) {
  return internResult(bytes, arena, classes);
}

NativeResult stringIntValue(const std::vector<VmValue> &args,
                            const VmObject *receiver, VmArena *arena,
                            const std::vector<const TufaClass *> *) {
  if (!args.empty())
    return std::string{"invalid String.intValue call"};
  const auto *bytes = stringBytes(arena, receiver);
  if (!bytes)
    return std::string{"String.intValue: string unavailable"};
  size_t i = 0;
  while (i < bytes->size() &&
         ((*bytes)[i] == ' ' || ((*bytes)[i] >= '\t' && (*bytes)[i] <= '\r')))
    ++i;
  uint32_t value = 0;
  bool negative = false, any = false;
  if (i + 1 < bytes->size() && (*bytes)[i] == '0' &&
      ((*bytes)[i + 1] == 'x' || (*bytes)[i + 1] == 'X')) {
    i += 2;
    for (; i < bytes->size(); ++i) {
      const char c = (*bytes)[i];
      const int digit = c >= '0' && c <= '9'   ? c - '0'
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                               : -1;
      if (digit < 0)
        break;
      value = value * 16u + static_cast<uint32_t>(digit);
      any = true;
    }
  } else {
    if (i < bytes->size() && (*bytes)[i] == '-') {
      negative = true;
      ++i;
    }
    while (i < bytes->size() && (*bytes)[i] >= '0' && (*bytes)[i] <= '9') {
      value = value * 10u + static_cast<uint32_t>((*bytes)[i++] - '0');
      any = true;
    }
    if (negative)
      value = 0u - value;
  }
  if (!any)
    return std::string{
        "String.intValue: stock result is uninitialized for invalid input"};
  int32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return VmValue{result};
}
