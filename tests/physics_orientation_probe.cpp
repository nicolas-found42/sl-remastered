#include "physics_body.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
static const physics::Mat34 matrices[] = {
 {1,0,0,0,0,1,0,0,0,0,1,0},
 {1,0,0,0,0,-1,0,0,0,0,-1,0},
 {-1,0,0,0,0,1,0,0,0,0,-1,0},
 {-1,0,0,0,0,-1,0,0,0,0,1,0},
 {0,-1,0,0,1,0,0,0,0,0,1,0},
 {0.1f,0.2f,0.3f,0,0.4f,0.5f,0.6f,0,0.7f,0.8f,0.9f,0},
 {-0.9f,0.2f,0.3f,0,0.4f,-0.8f,0.6f,0,0.7f,0.8f,-0.7f,0},
 {0.8253356f,-0.5646425f,0,0,0.5646425f,0.8253356f,0,0,0,0,1,0},
 {0.36f,-0.48f,0.8f,0,0.8f,0.60f,0,0,-0.48f,0.64f,0.60f,0},
 {-0.5f,0.8660254f,0,0,-0.8660254f,-0.5f,0,0,0,0,1,0},
 {0.0f,1,0,0,0,0,-1,0,-1,0,0,0}
};
template <typename T> void words(const T& v) { for(float f:v) { uint32_t u; std::memcpy(&u,&f,4); std::printf(" %08x",u); } }
int main() { for(unsigned i=0;i<sizeof matrices/sizeof matrices[0];i++) { auto q=physics::quatFromMatrix(matrices[i]); auto m=physics::matrixFromQuat(q); std::printf("case=%u input",i); words(matrices[i]); std::printf(" quat"); words(q); std::printf(" matrix"); words(m); std::puts(""); } }
