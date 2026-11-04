#include <cassert>
#include <cstdio>

int main() {
  int evaluations = 0;
  assert(++evaluations == 1);
  if (evaluations != 1) {
    std::fputs("test assertions are disabled\n", stderr);
    return 1;
  }
  return 0;
}
