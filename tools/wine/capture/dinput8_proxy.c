/*
 * dinput8_proxy.c - logs how the UNCHANGED stock game uses DirectInput and can
 * inject synthetic mouse-button / key state into the game's DirectInput reads.
 *
 * Placed next to StreetLegal_Redline.exe as dinput8.dll and selected with
 * WINEDLLOVERRIDES=dinput8=n,b. It exports DirectInput8Create, loads Wine's own
 * builtin dinput8.dll by full path and patches vtable entries in place.
 *
 * Log: <dump dir>\dinput8proxy.log. Commands: append lines to <dump dir>\dinput.cmd
 *   mbtn N down|up      mouse button N (0 = left) held or released
 *   mclick N [ms]       press and release mouse button N
 *   kdown SCAN | kup SCAN | key SCAN [ms]    scan code in decimal or 0x hex (Enter=0x1C, Esc=1, Up=0xC8, Down=0xD0)
 *   sleep MS
 */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef HRESULT (WINAPI *create_fn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT (WINAPI *create_device_fn)(void *, REFGUID, void **, LPUNKNOWN);
typedef HRESULT (WINAPI *get_state_fn)(void *, DWORD, LPVOID);
typedef HRESULT (WINAPI *get_data_fn)(void *, DWORD, DIDEVICEOBJECTDATA *, DWORD *, DWORD);
typedef HRESULT (WINAPI *acquire_fn)(void *);
typedef HRESULT (WINAPI *set_fmt_fn)(void *, const DIDATAFORMAT *);
typedef HRESULT (WINAPI *set_coop_fn)(void *, HWND, DWORD);

static char log_path[MAX_PATH], cmd_path[MAX_PATH];
static unsigned long first_tick;

static void logf_(const char *fmt, ...) {
    FILE *f = fopen(log_path, "a");
    va_list ap;
    if (!f) return;
    fprintf(f, "[%8lu ms] ", GetTickCount() - first_tick);
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

/* ---- patch registry: remember the original per (vtable, slot) ---- */
static struct { void **vtbl; int slot; void *orig; } patches[64];
static int npatches;
static void patch(void **vtbl, int slot, void *hook) {
    DWORD old; int i;
    for (i = 0; i < npatches; ++i) if (patches[i].vtbl == vtbl && patches[i].slot == slot) return;
    if (npatches >= 64 || !VirtualProtect(&vtbl[slot], sizeof(void *), PAGE_READWRITE, &old)) return;
    patches[npatches].vtbl = vtbl; patches[npatches].slot = slot; patches[npatches].orig = vtbl[slot]; npatches++;
    vtbl[slot] = hook;
    VirtualProtect(&vtbl[slot], sizeof(void *), old, &old);
}
static void *orig_of(void *self, int slot) {
    void **vtbl = *(void ***)self; int i;
    for (i = 0; i < npatches; ++i) if (patches[i].vtbl == vtbl && patches[i].slot == slot) return patches[i].orig;
    return NULL;
}

/* ---- device registry ---- */
enum { KIND_OTHER = 0, KIND_MOUSE = 1, KIND_KEYBOARD = 2 };
static struct { void *dev; int kind; unsigned long state_calls, data_calls, acquire_calls; } devs[16];
static int ndevs;
static int kind_of(void *dev) { int i; for (i = 0; i < ndevs; ++i) if (devs[i].dev == dev) return devs[i].kind; return KIND_OTHER; }
static int index_of(void *dev) { int i; for (i = 0; i < ndevs; ++i) if (devs[i].dev == dev) return i; return -1; }

static const GUID G_MOUSE = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID G_KEYBOARD = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

/* ---- injected state and queued buffered events ---- */
static volatile unsigned char inj_btn[8];
static volatile unsigned char inj_key[256];
static volatile LONG seen_btn[8], seen_key[256];
static struct { int kind; DWORD ofs, data; } pending[256];
static volatile int npending;
static CRITICAL_SECTION cs;
/* Optional bounded native Return witness. Recording occurs before injected bits
 * are merged. A failed write terminates this diagnostic run rather than allow a
 * successful capture to conceal a missing transition. No input is generated. */
static HANDLE return_file = INVALID_HANDLE_VALUE;
static int return_enabled;
static unsigned char return_known[16], return_last[16];
static unsigned long return_sequence;
static void native_return(int device, unsigned char value, unsigned long call) {
    DWORD saved_error = GetLastError(), written;
    char line[256]; int length;
    if (!return_enabled) return;
    EnterCriticalSection(&cs);
    value &= 0x80;
    if (!return_known[device] || return_last[device] != value) {
        if (++return_sequence > 64) TerminateProcess(GetCurrentProcess(), 86);
        length = snprintf(line, sizeof line,
            "{\"sequence\":%lu,\"source\":\"native\",\"device\":%d,\"offset\":28,\"data\":%u,\"state_call\":%lu,\"tick\":%lu}\n",
            return_sequence, device, (unsigned)value, call, GetTickCount());
        if (length <= 0 || (size_t)length >= sizeof line
                || !WriteFile(return_file, line, (DWORD)length, &written, NULL)
                || written != (DWORD)length || !FlushFileBuffers(return_file))
            TerminateProcess(GetCurrentProcess(), 86);
        return_known[device] = 1;
        return_last[device] = value;
    }
    LeaveCriticalSection(&cs);
    SetLastError(saved_error);
}
static void queue_event(int kind, DWORD ofs, DWORD data) {
    int queued, depth;
    EnterCriticalSection(&cs);
    queued = npending < 256;
    if (queued) { pending[npending].kind = kind; pending[npending].ofs = ofs; pending[npending].data = data; npending++; }
    depth = npending;
    LeaveCriticalSection(&cs);
    logf_("input_queue: %s kind=%d offset=%lu data=%lu depth=%d", queued ? "queued" : "dropped", kind, (unsigned long)ofs, (unsigned long)data, depth);
}

static LONG native_mouse_last[16][8];
static unsigned char native_mouse_known[16][8];
static HRESULT WINAPI hook_get_state(void *self, DWORD cb, LPVOID data) {
    HRESULT hr = ((get_state_fn)orig_of(self, 9))(self, cb, data);
    int k = kind_of(self), i = index_of(self);
    if (i >= 0) devs[i].state_calls++;
    if (SUCCEEDED(hr) && data && k == KIND_KEYBOARD && cb >= 256 && i >= 0)
        native_return(i, ((unsigned char *)data)[28], devs[i].state_calls);
    if (SUCCEEDED(hr) && data && k == KIND_MOUSE && cb >= 16 && i >= 0) {
        int button, count = cb >= 20 ? 8 : 4;
        for (button = 0; button < count; ++button) {
            LONG value = ((unsigned char *)data)[12 + button] & 0x80;
            if (!native_mouse_known[i][button] || native_mouse_last[i][button] != value) {
                logf_("input_poll: kind=1 offset=%d data=%ld source=native state_call=%lu", 12 + button, value, devs[i].state_calls);
                native_mouse_last[i][button] = value;
                native_mouse_known[i][button] = 1;
            }
        }
    }
    if (SUCCEEDED(hr) && data) {
        if (k == KIND_MOUSE && cb >= 16) {
            unsigned char *b = (unsigned char *)data + 12; int n = cb >= 20 ? 8 : 4;
            for (i = 0; i < n; ++i) if (inj_btn[i]) {
                b[i] |= 0x80;
                if (!InterlockedCompareExchange(&seen_btn[i], 1, 0)) logf_("input_state: observed_down kind=%d offset=%d", k, 12 + i);
            }
        } else if (k == KIND_KEYBOARD && cb >= 256) {
            for (i = 0; i < 256; ++i) if (inj_key[i]) {
                ((unsigned char *)data)[i] |= 0x80;
                if (!InterlockedCompareExchange(&seen_key[i], 1, 0)) logf_("input_state: observed_down kind=%d offset=%d", k, i);
            }
        }
    }
    return hr;
}
static HRESULT WINAPI hook_get_data(void *self, DWORD cb, DIDEVICEOBJECTDATA *od, DWORD *inout, DWORD flags) {
    DWORD cap = inout ? *inout : 0;
    struct { DWORD ofs, data; } delivered[256];
    int delivered_count = 0;
    HRESULT hr = ((get_data_fn)orig_of(self, 10))(self, cb, od, inout, flags);
    int k = kind_of(self), i = index_of(self);
    if (i >= 0) devs[i].data_calls++;
    if (hr == DI_BUFFEROVERFLOW) logf_("input_buffered: source_overflow kind=%d", k);
    if (SUCCEEDED(hr) && od && inout && cap && !(flags & DIGDD_PEEK) && (k == KIND_MOUSE || k == KIND_KEYBOARD)) {
        int j = 0;
        EnterCriticalSection(&cs);
        while (j < npending && *inout < cap) {
            if (pending[j].kind == k) {
                DIDEVICEOBJECTDATA *o = (DIDEVICEOBJECTDATA *)((unsigned char *)od + (size_t)*inout * cb);
                memset(o, 0, cb);
                o->dwOfs = pending[j].ofs; o->dwData = pending[j].data; o->dwTimeStamp = GetTickCount();
                delivered[delivered_count].ofs = pending[j].ofs;
                delivered[delivered_count++].data = pending[j].data;
                (*inout)++;
                memmove(&pending[j], &pending[j + 1], (size_t)(npending - j - 1) * sizeof pending[0]);
                npending--;
            } else j++;
        }
        LeaveCriticalSection(&cs);
        for (j = 0; j < delivered_count; ++j)
            logf_("input_buffered: delivered kind=%d offset=%lu data=%lu", k, (unsigned long)delivered[j].ofs, (unsigned long)delivered[j].data);
    }
    return hr;
}
static HRESULT WINAPI hook_acquire(void *self) {
    HRESULT hr = ((acquire_fn)orig_of(self, 7))(self);
    int i = index_of(self);
    if (i >= 0) devs[i].acquire_calls++;
    if (i >= 0 && devs[i].acquire_calls <= 3) logf_("Acquire dev=%p kind=%d -> hr=0x%08lx", self, devs[i].kind, (unsigned long)hr);
    return hr;
}
static HRESULT WINAPI hook_set_fmt(void *self, const DIDATAFORMAT *f) {
    HRESULT hr = ((set_fmt_fn)orig_of(self, 11))(self, f);
    logf_("SetDataFormat dev=%p kind=%d dwSize=%lu objSize=%lu flags=0x%lx dataSize=%lu numObjs=%lu -> hr=0x%08lx", self, kind_of(self),
          f ? (unsigned long)f->dwSize : 0, f ? (unsigned long)f->dwObjSize : 0, f ? (unsigned long)f->dwFlags : 0,
          f ? (unsigned long)f->dwDataSize : 0, f ? (unsigned long)f->dwNumObjs : 0, (unsigned long)hr);
    return hr;
}
static HRESULT WINAPI hook_set_coop(void *self, HWND hwnd, DWORD flags) {
    HRESULT hr = ((set_coop_fn)orig_of(self, 13))(self, hwnd, flags);
    logf_("SetCooperativeLevel dev=%p kind=%d hwnd=%p flags=0x%lx -> hr=0x%08lx", self, kind_of(self), (void *)hwnd, (unsigned long)flags, (unsigned long)hr);
    return hr;
}
typedef HRESULT (WINAPI *set_prop_fn)(void *, REFGUID, const DIPROPHEADER *);
static HRESULT WINAPI hook_set_prop(void *self, REFGUID g, const DIPROPHEADER *h) {
    HRESULT hr = ((set_prop_fn)orig_of(self, 6))(self, g, h);
    logf_("SetProperty dev=%p kind=%d prop=%p size=%lu -> hr=0x%08lx%s", self, kind_of(self), (const void *)g, h ? (unsigned long)h->dwSize : 0, (unsigned long)hr,
          (h && h->dwSize >= 20) ? "" : "");
    return hr;
}

static HRESULT WINAPI hook_create_device(void *self, REFGUID guid, void **out, LPUNKNOWN outer) {
    typedef HRESULT (WINAPI *cd_t)(void *, REFGUID, void **, LPUNKNOWN);
    HRESULT hr = ((cd_t)orig_of(self, 3))(self, guid, out, outer);
    int kind = IsEqualGUID(guid, &G_MOUSE) ? KIND_MOUSE : IsEqualGUID(guid, &G_KEYBOARD) ? KIND_KEYBOARD : KIND_OTHER;
    logf_("CreateDevice guid=%08lx kind=%d -> hr=0x%08lx dev=%p", guid->Data1, kind, (unsigned long)hr, out ? *out : NULL);
    if (SUCCEEDED(hr) && out && *out && ndevs < 16) {
        void **vt = *(void ***)*out;
        devs[ndevs].dev = *out; devs[ndevs].kind = kind; ndevs++;
        patch(vt, 6, hook_set_prop);
        patch(vt, 7, hook_acquire);
        patch(vt, 9, hook_get_state);
        patch(vt, 10, hook_get_data);
        patch(vt, 11, hook_set_fmt);
        patch(vt, 13, hook_set_coop);
    }
    return hr;
}

/* ---- command thread ---- */
static void send_button(int n, int down) {
    if (n < 0 || n > 7) return;
    if (down) InterlockedExchange(&seen_btn[n], 0);
    inj_btn[n] = (unsigned char)down;
    queue_event(KIND_MOUSE, 12 + (DWORD)n, down ? 0x80 : 0);
}
static void send_key(int scan, int down) {
    if (scan < 0 || scan > 255) return;
    if (down) InterlockedExchange(&seen_key[scan], 0);
    inj_key[scan] = (unsigned char)down;
    queue_event(KIND_KEYBOARD, (DWORD)scan, down ? 0x80 : 0);
}
static void run_cmd(char *line) {
    char cmd[16] = "", a[32] = "", b[32] = "";
    int n = sscanf(line, "%15s %31s %31s", cmd, a, b);
    if (n < 1 || cmd[0] == '#') return;
    logf_("cmd: %s", line);
    if (!_stricmp(cmd, "sleep")) Sleep((DWORD)atoi(a));
    else if (!_stricmp(cmd, "mbtn") && n >= 3) send_button(atoi(a), !_stricmp(b, "down"));
    else if (!_stricmp(cmd, "mclick") && n >= 2) { send_button(atoi(a), 1); Sleep(n >= 3 ? (DWORD)atoi(b) : 150); send_button(atoi(a), 0); }
    else if (!_stricmp(cmd, "kdown") && n >= 2) send_key((int)strtol(a, NULL, 0), 1);
    else if (!_stricmp(cmd, "kup") && n >= 2) send_key((int)strtol(a, NULL, 0), 0);
    else if (!_stricmp(cmd, "key") && n >= 2) { int s = (int)strtol(a, NULL, 0); send_key(s, 1); Sleep(n >= 3 ? (DWORD)atoi(b) : 150); send_key(s, 0); }
}
static DWORD WINAPI cmd_thread(LPVOID unused) {
    long offset = 0; (void)unused;
    for (;;) {
        FILE *f = fopen(cmd_path, "rb");
        if (f) {
            char line[256]; fseek(f, offset, SEEK_SET);
            while (fgets(line, sizeof line, f)) {
                size_t len = strlen(line);
                if (!len || line[len - 1] != '\n') break;
                offset += (long)len; line[len - 1] = 0;
                if (len > 1 && line[len - 2] == '\r') line[len - 2] = 0;
                run_cmd(line);
            }
            fclose(f);
        }
        Sleep(50);
    }
    return 0;
}
static DWORD WINAPI stats_thread(LPVOID unused) {
    (void)unused;
    for (;;) {
        int i; Sleep(5000);
        for (i = 0; i < ndevs; ++i)
            logf_("stats dev=%p kind=%d GetDeviceState calls=%lu GetDeviceData calls=%lu Acquire calls=%lu", devs[i].dev, devs[i].kind,
                  devs[i].state_calls, devs[i].data_calls, devs[i].acquire_calls);
    }
    return 0;
}

__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN outer) {
    static HMODULE real_dll;
    static create_fn real_create;
    HRESULT hr;
    if (!real_dll) {
        char path[MAX_PATH], dir[MAX_PATH];
        GetSystemDirectoryA(path, sizeof path);
        strncat(path, "\\dinput8.dll", sizeof path - strlen(path) - 1);
        first_tick = GetTickCount();
        InitializeCriticalSection(&cs);
        if (GetEnvironmentVariableA("SLRR_DUMP_DIR", dir, sizeof dir) == 0) {
            GetModuleFileNameA(NULL, dir, sizeof dir);
            *strrchr(dir, '\\') = 0;
            strncat(dir, "\\dumps", sizeof dir - strlen(dir) - 1);
        }
        CreateDirectoryA(dir, NULL);
        snprintf(log_path, sizeof log_path, "%s\\dinput8proxy.log", dir);
        logf_("clock_origin_tick=%lu", first_tick);
        logf_("input_telemetry=v1");
        snprintf(cmd_path, sizeof cmd_path, "%s\\dinput.cmd", dir);
        {
            char enabled[8], return_path[MAX_PATH];
            return_enabled = GetEnvironmentVariableA("SLRR_NATIVE_RETURN_OBSERVER", enabled, sizeof enabled) == 1
                && enabled[0] == '1';
            if (return_enabled) {
                int length = snprintf(return_path, sizeof return_path, "%s\\native-return.jsonl", dir);
                if (length <= 0 || (size_t)length >= sizeof return_path)
                    TerminateProcess(GetCurrentProcess(), 86);
                return_file = CreateFileA(return_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                          CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
                if (return_file == INVALID_HANDLE_VALUE) TerminateProcess(GetCurrentProcess(), 86);
            }
        }
        real_dll = LoadLibraryA(path);
        logf_("proxy loaded; real dinput8 path=%s handle=%p", path, (void *)real_dll);
        if (!real_dll) return E_FAIL;
        real_create = (create_fn)(void *)GetProcAddress(real_dll, "DirectInput8Create");
        if (!real_create) return E_FAIL;
        CreateThread(NULL, 0, cmd_thread, NULL, 0, NULL);
        CreateThread(NULL, 0, stats_thread, NULL, 0, NULL);
    }
    hr = real_create(inst, ver, riid, out, outer);
    logf_("DirectInput8Create ver=0x%lx iid=%08lx -> hr=0x%08lx obj=%p", (unsigned long)ver, riid->Data1, (unsigned long)hr, out ? *out : NULL);
    if (SUCCEEDED(hr) && out && *out) patch(*(void ***)*out, 3, hook_create_device);
    return hr;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) { (void)h; (void)reason; (void)r; return TRUE; }
