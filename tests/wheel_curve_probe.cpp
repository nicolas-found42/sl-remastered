#include "wheel.hpp"
#include "wheel_curve_cases.h"
#include <array>
#include <cstdio>
#include <cstring>
int main() {
 for(unsigned c=0;c<3;c++) for(unsigned s=0;s<6;s++) for(unsigned l=0;l<4;l++) {
  std::array<float,11> coefficients{}; std::memcpy(coefficients.data(),curve_cases[c],sizeof(curve_cases[c]));
  auto a=evaluateWheelCurve(coefficients,curve_slips[s],curve_loads[l],WheelCurveMode::Nonnegative);
  auto b=evaluateWheelCurve(coefficients,curve_slips[s],curve_loads[l],WheelCurveMode::Signed);
  if(!a || !b) return 1;
  unsigned x,y; std::memcpy(&x,&*a,4); std::memcpy(&y,&*b,4);
  std::printf("case=%u slip=%u load=%u %08x %08x\n",c,s,l,x,y);
 }
}
