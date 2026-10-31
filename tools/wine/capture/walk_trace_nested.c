/* Exercise nested and simultaneous detours against a verified mapped stock image.
 * The private mapping's walk body is replaced with a synthetic recursive body;
 * this does not modify the executable on disk or assert stock physics results.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int walk_trace_install(HMODULE module, const char* image_path);
int walk_trace_flush(const char* path);
typedef float(__cdecl* Walk)(void*, void*, void*, void*, void*, void*, void*, void*, void*);
static Walk walk;
static volatile LONG arrived;

static float __cdecl recursive(void* level, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i) {
  int* n = level;
  float result;
  if (*n) {
    --*n;
    result = walk(level, b, c, d, e, f, g, h, i) + 1.0f;
    ++*n;
  } else {
    InterlockedIncrement(&arrived);
    while (InterlockedCompareExchange(&arrived, 0, 0) < 2) Sleep(0);
    result = 1.5f;
  }
  SetLastError(0x4545);
  return result;
}

static DWORD WINAPI worker(void* unused) {
  int depth = 2;
  unsigned char pose[0xd4] = {0}, shapes[2][60] = {{0}};
  void* cache[2] = {0};
  float out[3][3] = {{0}}, result;
  unsigned short previous, cw = 0x007f, observed;
  (void)unused;
  __asm__ volatile("fnstcw %0" : "=m"(previous));
  __asm__ volatile("fldcw %0" : : "m"(cw));
  SetLastError(0x1234);
  result = walk(&depth, pose, shapes[0], shapes[1], &cache[0], &cache[1], out[0], out[1], out[2]);
  __asm__ volatile("fnstcw %0" : "=m"(observed));
  __asm__ volatile("fldcw %0" : : "m"(previous));
  return result == 3.5f && depth == 2 && observed == cw && GetLastError() == 0x4545 ? 0 : 1;
}

int main(int argc, char** argv) {
  HMODULE image;
  unsigned char* body;
  DWORD old, ignored, a, b;
  int32_t displacement;
  HANDLE threads[2];
  if (argc != 3) return 2;
  image = LoadLibraryExA(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
  if (!image || !walk_trace_install(image, argv[1])) return 3;
  body = (unsigned char*)image + 0xae9c8;
  if (!VirtualProtect(body, 5, PAGE_EXECUTE_READWRITE, &old)) return 4;
  displacement = (int32_t)((uintptr_t)recursive - ((uintptr_t)body + 5));
  body[0] = 0xe9;
  memcpy(body + 1, &displacement, 4);
  FlushInstructionCache(GetCurrentProcess(), body, 5);
  VirtualProtect(body, 5, old, &ignored);
  walk = (Walk)((unsigned char*)image + 0xae9c0);
  threads[0] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
  threads[1] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
  if (!threads[0] || !threads[1]) return 5;
  if (WaitForMultipleObjects(2, threads, TRUE, 30000) != WAIT_OBJECT_0) return 6;
  GetExitCodeThread(threads[0], &a);
  GetExitCodeThread(threads[1], &b);
  CloseHandle(threads[0]);
  CloseHandle(threads[1]);
  if (a || b) return 7;
  return walk_trace_flush(argv[2]) == 6 ? 0 : 8;
}
