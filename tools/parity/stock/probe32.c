#include <windows.h>
#include <stdio.h>
int main(void){BOOL wow=FALSE;printf("pointer_bytes=%u pid=%lu wow64_query=%d ",(unsigned)sizeof(void*),GetCurrentProcessId(),IsWow64Process(GetCurrentProcess(),&wow));printf("wow64=%d\n",wow);return sizeof(void*)==4?0:1;}
