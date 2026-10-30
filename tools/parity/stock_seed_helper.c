/* Isolated stock Math.randomize code with synthetic clock and seed sink. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static double clock_value;
static uint32_t observed_seed;
static unsigned seed_calls;
static double __cdecl fake_clock(void) { return clock_value; }
static void __cdecl observe_seed(uint32_t seed) { observed_seed = seed; ++seed_calls; }

static void patch_call(unsigned char *at, void *target) {
    int32_t delta = (int32_t)((uintptr_t)target - ((uintptr_t)at + 5));
    memcpy(at + 1, &delta, sizeof delta);
}

int main(int argc, char **argv) {
    static const unsigned char expected[] = {0xe8,0xeb,0x4e,0x0d,0,0xe8,0x76,0x9f,0x15,0,0x50,0xe8,0x1b,0xac,0x15,0,0x59,0xc3};
    static const double cases[] = {0.0, 1.5, 1234.9, -1.5, 2147483648.0};
    static const uint32_t expected_seeds[] = {0, 1, 1234, 0xffffffffu, 0x80000000u};
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryExA(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!module) { fprintf(stderr, "load error %lu\n", GetLastError()); return 3; }
    unsigned char *image = (unsigned char *)module;
    IMAGE_NT_HEADERS32 *pe = (IMAGE_NT_HEADERS32 *)(image + ((IMAGE_DOS_HEADER *)image)->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->OptionalHeader.ImageBase != 0x400000 ||
        memcmp(image + 0x7c7d0, expected, sizeof expected)) {
        fprintf(stderr, "stock code mismatch\n"); return 4;
    }
    DWORD old;
    if (!VirtualProtect(image + 0x7c7d0, sizeof expected, PAGE_EXECUTE_READWRITE, &old)) return 5;
    patch_call(image + 0x7c7d0, fake_clock);
    patch_call(image + 0x7c7db, observe_seed);
    FlushInstructionCache(GetCurrentProcess(), image + 0x7c7d0, sizeof expected);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        clock_value = cases[i]; seed_calls = 0;
        ((void (__cdecl *)(void))(image + 0x7c7d0))();
        printf("clock=%.17g seed=%u calls=%u\n", clock_value, observed_seed, seed_calls);
        if (seed_calls != 1 || observed_seed != expected_seeds[i]) return 6;
    }
    return 0;
}
