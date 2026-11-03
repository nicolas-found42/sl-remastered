#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

int main(void) {
	uintptr_t hints[] = {0x10000000u, 0x40000000u, 0x70000000u};
	int ok = 0;
	for (int i = 0; i < 3; i++) {
		void* p = mmap((void*)hints[i], 1u << 20, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
		int below = p != MAP_FAILED && (uintptr_t)p < 0x100000000ull;
		printf("hint 0x%lx -> %p %s\n", (unsigned long)hints[i], p, below ? "BELOW 4GB" : "above 4GB or failed");
		ok += below;
	}
	return ok == 3 ? 0 : 1;
}
