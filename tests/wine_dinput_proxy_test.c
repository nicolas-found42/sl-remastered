/* Drive the actual proxy queue and hooks with fake DirectInput methods. */
#include "../tools/wine/capture/dinput8_proxy.c"

static int overflow;
static HRESULT WINAPI fake_data(void *self, DWORD cb, DIDEVICEOBJECTDATA *od, DWORD *count, DWORD flags) {
    (void)self; (void)cb; (void)od; (void)flags; *count = 0; return overflow ? DI_BUFFEROVERFLOW : DI_OK;
}
static HRESULT WINAPI fake_state(void *self, DWORD cb, LPVOID data) {
    (void)self; memset(data, 0, cb); return DI_OK;
}
int main(void) {
    void *vtbl[11] = {0}; void **obj = vtbl;
    DIDEVICEOBJECTDATA data[1]; DWORD count = 1;
    unsigned char state[20];
    snprintf(log_path, sizeof log_path, "Z:\\out\\proxy-test.log");
    FILE *f = fopen(log_path, "w"); if (!f) return 1; fclose(f);
    first_tick = GetTickCount(); InitializeCriticalSection(&cs); logf_("input_telemetry=v1");
    devs[0].dev = &obj; devs[0].kind = KIND_MOUSE; ndevs = 1;
    patches[0].vtbl = vtbl; patches[0].slot = 10; patches[0].orig = fake_data;
    patches[1].vtbl = vtbl; patches[1].slot = 9; patches[1].orig = fake_state; npatches = 2;
    for (int i = 0; i < 257; ++i) queue_event(KIND_MOUSE, 12, 0x80);
    if (npending != 256) return 2;
    if (hook_get_data(&obj, sizeof data[0], data, &count, 0) != DI_OK || count != 1 || data[0].dwOfs != 12 || data[0].dwData != 0x80 || npending != 255) return 3;
    overflow = 1; count = 0;
    if (hook_get_data(&obj, sizeof data[0], data, &count, 0) != DI_BUFFEROVERFLOW) return 6;
    inj_btn[0] = 1;
    if (hook_get_state(&obj, sizeof state, state) != DI_OK || !(state[12] & 0x80)) return 4;
    if (hook_get_state(&obj, sizeof state, state) != DI_OK) return 5;
    DeleteCriticalSection(&cs);
    return 0;
}
