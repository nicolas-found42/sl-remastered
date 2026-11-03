#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

// A partial model: only fields read or written by the setters under comparison.
struct NativeWheel {
    std::array<float, 17> coefficients{};
    float input_scale = 0;
    float friction_x = 0, friction_y = 0, scaled_x = 0, scaled_y = 0;
};
struct OriginalWheel { std::array<std::uint8_t, 0x2b4> bytes{}; };

float read(const OriginalWheel& w, std::size_t offset) {
    float value;
    std::memcpy(&value, w.bytes.data() + offset, sizeof value);
    return value;
}
void write(OriginalWheel& w, std::size_t offset, float value) {
    std::memcpy(w.bytes.data() + offset, &value, sizeof value);
}
float factor(float scale) {
    const std::uint32_t bits = 0x3dccccd0;
    float minimum;
    std::memcpy(&minimum, &bits, sizeof minimum);
    return scale < 1.0f ? 1.0f - scale * scale * 0.6f : minimum;
}
void pacejka(OriginalWheel& w, int i, float v) {
    if (i >= 0 && i < 17) write(w, 0x1e8 + 4 * static_cast<std::size_t>(i), v);
}
void pacejka(NativeWheel& w, int i, float v) {
    if (i >= 0 && i < 17) w.coefficients[static_cast<std::size_t>(i)] = v;
}
void friction(OriginalWheel& w, float v) {
    write(w, 0x1f0, v); write(w, 0x64, v); write(w, 0x68, v);
    write(w, 0x6c, factor(read(w, 0x15c)) * v);
    write(w, 0x70, factor(read(w, 0x15c)) * v);
}
void friction(NativeWheel& w, float v) {
    w.coefficients[2] = v; w.friction_x = v; w.friction_y = v;
    w.scaled_x = factor(w.input_scale) * v; w.scaled_y = factor(w.input_scale) * v;
}
void compare(const OriginalWheel& a, const NativeWheel& b) {
    for (std::size_t i = 0; i < b.coefficients.size(); ++i)
        assert(read(a, 0x1e8 + 4*i) == b.coefficients[i]);
    assert(read(a, 0x64) == b.friction_x && read(a, 0x68) == b.friction_y);
    assert(read(a, 0x6c) == b.scaled_x && read(a, 0x70) == b.scaled_y);
}
int main() {
    OriginalWheel original; NativeWheel native;
    for (int i = -1; i <= 17; ++i) {
        pacejka(original, i, float(i + 2)); pacejka(native, i, float(i + 2));
        compare(original, native);
    }
    for (int index : {0, 2, 4, 11, 12, 14}) {
        // The named setter and setPacejka address the same field.
        write(original, 0x1e8 + 4*static_cast<std::size_t>(index), 42.0f);
        native.coefficients[static_cast<std::size_t>(index)] = 42.0f;
        compare(original, native);
        pacejka(original, index, 7.0f); pacejka(native, index, 7.0f);
        compare(original, native);
    }
    for (float scale : {0.0f, 0.5f, 1.0f, 2.0f}) {
        write(original, 0x15c, scale); native.input_scale = scale;
        friction(original, 3.0f); friction(native, 3.0f); compare(original, native);
        pacejka(original, 2, 8.0f); pacejka(native, 2, 8.0f); compare(original, native);
        assert(native.friction_x == 3.0f); // setPacejka does not refresh derived fields.
    }
    std::cout << "wheel comparison: ok\n";
}
