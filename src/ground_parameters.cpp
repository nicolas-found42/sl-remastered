#include "ground_parameters.hpp"

#include <charconv>

namespace {
bool space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
void skip(std::string_view s, size_t& p) { while (p < s.size() && space(s[p])) ++p; }
bool comma(std::string_view s, size_t& p) {
  if (p == s.size() || s[p] != ',') return false;
  ++p;
  return true;
}
bool number(std::string_view s, size_t& p, float& out) {
  skip(s, p);
  const bool negative = p < s.size() && s[p] == '-';
  if (negative) ++p;
  size_t digits = p;
  uint32_t integer = 0;
  while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
    const uint32_t digit = static_cast<uint32_t>(s[p] - '0');
    if (integer > (8388607u - digit) / 10u) return false;
    integer = integer * 10u + digit;
    ++p;
  }
  if (p == digits) return false;
  float fraction = 0.0f;
  if (p < s.size() && s[p] == '.') {
    ++p;
    if (p < s.size() && s[p] == '5') {
      fraction = 0.5f;
      ++p;
    }
    while (p < s.size() && s[p] == '0') ++p;
    if (p < s.size() && s[p] >= '0' && s[p] <= '9') return false;
  }
  out = static_cast<float>(integer) + fraction;
  if (negative) out = -out;
  return true;
}
bool path(std::string_view s, size_t& p, std::string& out, bool last) {
  skip(s, p);
  size_t start = p;
  while (p < s.size() && s[p] != '\0' && !space(s[p]) && (last || s[p] != ',')) ++p;
  if (p - start > 255) return false;
  out = std::string(s.substr(start, p - start));
  return true;
}
}

bool parseGroundParameters(std::string_view text, GroundParameters& out, std::string& error) {
  GroundParameters value;
  size_t p = 0;
  skip(text, p);
  if (text.substr(p, 2) != "0x" && text.substr(p, 2) != "0X") {
    error = "invalid ground resource id";
    return false;
  }
  p += 2;
  const char* first = text.data() + p;
  auto result = std::from_chars(first, text.data() + text.size(), value.resourceId, 16);
  if (result.ec != std::errc{} || result.ptr == first) { error = "invalid ground resource id"; return false; }
  p = static_cast<size_t>(result.ptr - text.data());
  for (float& v : value.position) {
    if (!comma(text, p) || !number(text, p, v)) { error = "invalid ground position"; return false; }
  }
  for (float& v : value.extraScalars) {
    if (!comma(text, p) || !number(text, p, v)) { error = "invalid ground extra scalars"; return false; }
  }
  if (!comma(text, p) || !path(text, p, value.materialPath, false) ||
      (p != text.size() &&
       (!comma(text, p) || !path(text, p, value.roadPath, true))) ||
      p != text.size()) {
    error = "invalid ground paths";
    return false;
  }
  out = std::move(value);
  error.clear();
  return true;
}
