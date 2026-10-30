/* Isolated original 0049b670 with synthetic bodies, observers and cleanup stubs. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void *recorded[8];
static int recorded_count;
static void __cdecl record_free(void *p) { recorded[recorded_count++] = p; }
static void __cdecl no_op(void) {}
static void patch(unsigned char *p, void *target) {
  int32_t delta = (int32_t)((uintptr_t)target - ((uintptr_t)p + 5));
  p[0] = 0xe9;
  memcpy(p + 1, &delta, 4);
}
static void link_observer(unsigned char *body, unsigned char *observer, void *next) {
  *(void **)observer = body;
  *(void **)(observer + 4) = next;
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  HMODULE m = LoadLibraryExA(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
  if (!m) return 3;
  unsigned char *image = (unsigned char *)m;
  IMAGE_NT_HEADERS32 *pe = (void *)(image + ((IMAGE_DOS_HEADER *)image)->e_lfanew);
  if (pe->Signature != IMAGE_NT_SIGNATURE || pe->OptionalHeader.ImageBase != 0x400000) return 4;
  FILE *file = fopen(argv[1], "rb"); if (!file) return 5;
  IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(pe), *sec = NULL;
  unsigned start = 0x9b670, len = 0x1bd;
  for (unsigned i = 0; i < pe->FileHeader.NumberOfSections; ++i)
    if (start >= sections[i].VirtualAddress && start + len <= sections[i].VirtualAddress + sections[i].SizeOfRawData) sec = &sections[i];
  if (!sec || fseek(file, sec->PointerToRawData + start - sec->VirtualAddress, SEEK_SET)) return 6;
  for (unsigned i = 0; i < len; ++i) if (fgetc(file) != image[start+i]) return 7;
  fclose(file);
  DWORD old;
  if (!VirtualProtect(image, pe->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &old)) return 8;
  patch(image + 0x14f5b0, record_free);
  patch(image + 0x776f0, no_op);
  patch(image + 0x5f880, no_op);
  FlushInstructionCache(GetCurrentProcess(), image, pe->OptionalHeader.SizeOfImage);
  unsigned char world[0x100] = {0}, parent[0x218] = {0}, childA[0x218] = {0}, childB[0x218] = {0};
  unsigned char oParent[8] = {0}, oA1[8] = {0}, oA2[8] = {0}, oB[8] = {0};
  *(void **)(parent + 0x24) = childB;
  *(void **)(childB + 0x20) = parent;
  *(void **)(childB + 0x28) = childA;
  *(void **)(childA + 0x20) = parent;
  *(void **)parent = oParent; *(int *)(parent + 4) = 1;
  *(void **)childA = oA1; *(int *)(childA + 4) = 2;
  *(void **)childB = oB; *(int *)(childB + 4) = 1;
  link_observer(parent, oParent, NULL);
  link_observer(childA, oA1, oA2); link_observer(childA, oA2, NULL);
  link_observer(childB, oB, NULL);
  *(int *)(world + 0x54) = 3;
  typedef int (__attribute__((thiscall)) *destroy_fn)(void *, void *);
  int result = ((destroy_fn)(image + start))(world, parent);
  int clean = !*(void **)oParent && !*(void **)oA1 && !*(void **)oA2 && !*(void **)oB &&
    !*(void **)(oA1 + 4) && !*(void **)(childA + 0x20) && !*(void **)(childB + 0x20);
  int order = recorded_count == 3 && recorded[0] == childB && recorded[1] == childA && recorded[2] == parent;
  printf("result=%d observers_cleared=%d newest_child_first=%d freed=%d world_count=%d\n",
         result, clean, order, recorded_count, *(int *)(world + 0x54));
  return result == 1 && clean && order && *(int *)(world + 0x54) == 0 ? 0 : 9;
}
