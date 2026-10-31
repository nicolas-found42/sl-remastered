/*
 * d3d9_proxy.c - in-process frame dumper for the UNCHANGED stock game under Wine.
 *
 * Placed next to StreetLegal_Redline.exe as d3d9.dll and selected with
 * WINEDLLOVERRIDES=d3d9=n,b. It exports Direct3DCreate9, loads Wine's own
 * builtin d3d9.dll by full path, and patches vtable entries in place (nothing
 * about the game is changed). In the Present hook it copies the back buffer
 * to system memory and writes a 24-bit BMP, so the game's own rendered pixels
 * are saved without any screen capture.
 *
 * Environment:
 *   SLRR_DUMP_DIR    directory for BMPs and d3d9proxy.log (default: next to the exe)
 *   SLRR_DUMP_EVERY  dump every N-th Present (default 60, 0 = only on demand)
 *   SLRR_DUMP_FIRST  also dump Present numbers 1..K (default 3)
 * On demand: create a file named dump.now in the dump directory; the next
 * Present dumps a frame and deletes the file.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef IDirect3D9 *(WINAPI *create9_fn)(UINT);
typedef HRESULT (WINAPI *create_device_fn)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT (WINAPI *present_fn)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT (WINAPI *reset_fn)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
typedef HRESULT (WINAPI *draw_prim_fn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (WINAPI *draw_idx_fn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (WINAPI *draw_prim_up_fn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT (WINAPI *draw_idx_up_fn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT, const void *, UINT);
typedef HRESULT (WINAPI *clear_fn)(IDirect3DDevice9 *, DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD);

static create_device_fn real_create_device;
static present_fn real_present;
static reset_fn real_reset;
static draw_prim_fn real_draw_prim;
static draw_idx_fn real_draw_idx;
static draw_prim_up_fn real_draw_prim_up;
static draw_idx_up_fn real_draw_idx_up;
static clear_fn real_clear;

static char dump_dir[MAX_PATH];
static char log_path[MAX_PATH];
static char walk_trace_path[MAX_PATH];
static int walk_trace_enabled;
int walk_trace_install(HMODULE module, const char *image_path);
int walk_trace_flush(const char *path);
static unsigned dump_every = 60, dump_first = 3;
static unsigned long presents, draws, draws_this_frame, clears, resets, dumps, dump_fail;
static unsigned long first_tick;
static HWND game_hwnd;
static unsigned long total_draws_at_last_log;

static void logf_(const char *fmt, ...) {
    FILE *f = fopen(log_path, "a");
    va_list ap;
    if (!f) return;
    fprintf(f, "[%8lu ms] ", GetTickCount() - first_tick);
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

static unsigned short fpu_control_word(void) {
    unsigned short cw;
    __asm__ volatile ("fnstcw %0" : "=m" (cw));
    return cw;
}

static void patch_vtable(void **vtbl, int index, void *hook, void **saved) {
    DWORD old;
    if (*saved) return; /* shared vtable already patched */
    if (!VirtualProtect(&vtbl[index], sizeof(void *), PAGE_READWRITE, &old)) {
        logf_("VirtualProtect failed for vtable slot %d, error %lu", index, GetLastError());
        return;
    }
    *saved = vtbl[index];
    vtbl[index] = hook;
    VirtualProtect(&vtbl[index], sizeof(void *), old, &old);
}

static int write_bmp(const char *path, const unsigned char *bgr, int w, int h) {
    FILE *f = fopen(path, "wb");
    unsigned rowbytes = ((unsigned)w * 3 + 3) & ~3u, size = rowbytes * (unsigned)h + 54, zero = 0;
    unsigned char hdr[54] = {'B', 'M'};
    int y;
    if (!f) return 0;
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    for (y = h - 1; y >= 0; --y) { /* BMP is bottom-up */
        fwrite(bgr + (size_t)y * (unsigned)w * 3, 1, (size_t)w * 3, f);
        if (rowbytes != (unsigned)w * 3) fwrite(&zero, 1, rowbytes - (unsigned)w * 3, f);
    }
    fclose(f);
    return 1;
}

/* Returns nonzero on success; fills nonblack with the count of non-black pixels. */
static int dump_backbuffer(IDirect3DDevice9 *dev, const char *reason) {
    IDirect3DSurface9 *bb = NULL, *sys = NULL;
    D3DSURFACE_DESC desc;
    D3DLOCKED_RECT lr;
    HRESULT hr;
    unsigned char *bgr = NULL;
    char path[MAX_PATH];
    int x, y, ok = 0;
    unsigned long nonblack = 0;

    hr = IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    if (FAILED(hr)) { logf_("GetBackBuffer failed hr=0x%08lx", (unsigned long)hr); return 0; }
    IDirect3DSurface9_GetDesc(bb, &desc);
    hr = IDirect3DDevice9_CreateOffscreenPlainSurface(dev, desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, NULL);
    if (FAILED(hr)) { logf_("CreateOffscreenPlainSurface failed hr=0x%08lx fmt=%d", (unsigned long)hr, (int)desc.Format); goto out; }
    hr = IDirect3DDevice9_GetRenderTargetData(dev, bb, sys);
    if (FAILED(hr)) { logf_("GetRenderTargetData failed hr=0x%08lx (multisample=%d)", (unsigned long)hr, (int)desc.MultiSampleType); goto out; }
    hr = IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY);
    if (FAILED(hr)) { logf_("LockRect failed hr=0x%08lx", (unsigned long)hr); goto out; }
    bgr = malloc((size_t)desc.Width * desc.Height * 3);
    if (!bgr) { IDirect3DSurface9_UnlockRect(sys); goto out; }
    for (y = 0; y < (int)desc.Height; ++y) {
        const unsigned char *row = (const unsigned char *)lr.pBits + (size_t)y * (unsigned)lr.Pitch;
        unsigned char *o = bgr + (size_t)y * desc.Width * 3;
        for (x = 0; x < (int)desc.Width; ++x) {
            unsigned r, g, b;
            if (desc.Format == D3DFMT_R5G6B5) {
                unsigned short p = ((const unsigned short *)row)[x];
                r = ((p >> 11) & 31) * 255 / 31; g = ((p >> 5) & 63) * 255 / 63; b = (p & 31) * 255 / 31;
            } else if (desc.Format == D3DFMT_X1R5G5B5 || desc.Format == D3DFMT_A1R5G5B5) {
                unsigned short p = ((const unsigned short *)row)[x];
                r = ((p >> 10) & 31) * 255 / 31; g = ((p >> 5) & 31) * 255 / 31; b = (p & 31) * 255 / 31;
            } else { /* X8R8G8B8 / A8R8G8B8: bytes are B,G,R,A in memory */
                b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2];
            }
            o[x * 3] = (unsigned char)b; o[x * 3 + 1] = (unsigned char)g; o[x * 3 + 2] = (unsigned char)r;
            if (r | g | b) nonblack++;
        }
    }
    IDirect3DSurface9_UnlockRect(sys);
    snprintf(path, sizeof path, "%s\\frame_%06lu_t%06lu.bmp", dump_dir, presents, (GetTickCount() - first_tick) / 1000);
    ok = write_bmp(path, bgr, (int)desc.Width, (int)desc.Height);
    logf_("dump %s: present=%lu %ux%u fmt=%d nonblack_pixels=%lu of %lu draws_since_last_present=%lu (%s) %s",
          ok ? "ok" : "WRITE FAILED", presents, desc.Width, desc.Height, (int)desc.Format, nonblack,
          (unsigned long)desc.Width * desc.Height, draws_this_frame, reason, path);
out:
    free(bgr);
    if (sys) IDirect3DSurface9_Release(sys);
    if (bb) IDirect3DSurface9_Release(bb);
    if (ok) dumps++; else dump_fail++;
    return ok;
}


/* ---------- synthetic input, driven by a command file ----------
 * The proxy runs inside the game process, so SendInput works on whichever
 * Wine desktop the game is on. A thread tails <dump dir>\input.cmd and runs
 * each new line. Commands (one per line):
 *   move X Y | click [X Y] | dclick [X Y] | mdown | mup | mevent | postclick CX CY | focus | where
 *   key NAME [holdMs] | keydown NAME | keyup NAME | sleep MS | log TEXT
 * NAME: a-z 0-9 enter escape space tab up down left right f1-f12 lshift lctrl
 */
static struct { const char *name; WORD vk; int ext; } keymap[] = {
    {"enter", VK_RETURN, 0}, {"escape", VK_ESCAPE, 0}, {"esc", VK_ESCAPE, 0}, {"space", VK_SPACE, 0}, {"tab", VK_TAB, 0},
    {"up", VK_UP, 1}, {"down", VK_DOWN, 1}, {"left", VK_LEFT, 1}, {"right", VK_RIGHT, 1},
    {"lshift", VK_LSHIFT, 0}, {"lctrl", VK_LCONTROL, 0}, {"pgup", VK_PRIOR, 1}, {"pgdn", VK_NEXT, 1},
    {"backspace", VK_BACK, 0}, {"f1", VK_F1, 0}, {"f2", VK_F2, 0}, {"f3", VK_F3, 0}, {"f4", VK_F4, 0}, {"f5", VK_F5, 0},
    {"f6", VK_F6, 0}, {"f7", VK_F7, 0}, {"f8", VK_F8, 0}, {"f9", VK_F9, 0}, {"f10", VK_F10, 0}, {"f11", VK_F11, 0}, {"f12", VK_F12, 0},
};
static int lookup_key(const char *n, WORD *vk, int *ext) {
    unsigned i;
    *ext = 0;
    if (strlen(n) == 1 && ((n[0] >= 'a' && n[0] <= 'z') || (n[0] >= '0' && n[0] <= '9'))) {
        *vk = (WORD)(n[0] >= 'a' ? n[0] - 'a' + 'A' : n[0]);
        return 1;
    }
    for (i = 0; i < sizeof keymap / sizeof keymap[0]; ++i)
        if (!_stricmp(n, keymap[i].name)) { *vk = keymap[i].vk; *ext = keymap[i].ext; return 1; }
    return 0;
}
static void send_key(WORD vk, int ext, int up) {
    INPUT in; memset(&in, 0, sizeof in);
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (ext ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof in);
}
static void send_mouse(DWORD flags, int x, int y) {
    INPUT in; memset(&in, 0, sizeof in);
    in.type = INPUT_MOUSE;
    in.mi.dx = x; in.mi.dy = y; in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof in);
}
static void mouse_to(int x, int y) {
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SetCursorPos(x, y);
    send_mouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, (int)((x * 65535L + sw / 2) / sw), (int)((y * 65535L + sh / 2) / sh));
}
static void run_cmd(char *line) {
    char cmd[32] = "", a[64] = "", b[64] = "";
    int n = sscanf(line, "%31s %63s %63s", cmd, a, b);
    WORD vk; int ext;
    if (n < 1 || cmd[0] == '#') return;
    logf_("input: %s", line);
    if (!_stricmp(cmd, "sleep")) Sleep((DWORD)atoi(a));
    else if (!_stricmp(cmd, "move") && n >= 3) { mouse_to(atoi(a), atoi(b)); }
    else if (!_stricmp(cmd, "click") || !_stricmp(cmd, "dclick")) {
        int times = _stricmp(cmd, "dclick") ? 1 : 2, i;
        if (n >= 3) { mouse_to(atoi(a), atoi(b)); Sleep(200); }
        for (i = 0; i < times; ++i) {
            send_mouse(MOUSEEVENTF_LEFTDOWN, 0, 0); Sleep(100);
            send_mouse(MOUSEEVENTF_LEFTUP, 0, 0); Sleep(100);
        }
    }
    else if (!_stricmp(cmd, "focus")) {
        BOOL ok = SetForegroundWindow(game_hwnd);
        logf_("input: SetForegroundWindow(%p)=%d foreground now=%p", (void *)game_hwnd, ok, (void *)GetForegroundWindow());
    }
    else if (!_stricmp(cmd, "where")) {
        POINT pt; RECT rc; GetCursorPos(&pt); GetWindowRect(game_hwnd, &rc);
        logf_("input: cursor=(%ld,%ld) game window rect=(%ld,%ld,%ld,%ld) screen=%dx%d", pt.x, pt.y, rc.left, rc.top, rc.right, rc.bottom,
              GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    }
    else if (!_stricmp(cmd, "postclick") && n >= 3) { /* window messages, client coordinates */
        LPARAM lp = MAKELPARAM(atoi(a), atoi(b));
        PostMessageA(game_hwnd, WM_MOUSEMOVE, 0, lp); Sleep(100);
        PostMessageA(game_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp); Sleep(150);
        PostMessageA(game_hwnd, WM_LBUTTONUP, 0, lp);
    }
    else if (!_stricmp(cmd, "mevent")) { /* legacy mouse_event API */
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); Sleep(150);
        logf_("input: GetAsyncKeyState(VK_LBUTTON)=0x%04x while down", (unsigned)GetAsyncKeyState(VK_LBUTTON));
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    }
    else if (!_stricmp(cmd, "mdown")) { send_mouse(MOUSEEVENTF_LEFTDOWN, 0, 0); Sleep(50); logf_("input: GetAsyncKeyState(VK_LBUTTON)=0x%04x after mdown", (unsigned)GetAsyncKeyState(VK_LBUTTON)); }
    else if (!_stricmp(cmd, "mup")) send_mouse(MOUSEEVENTF_LEFTUP, 0, 0);
    else if ((!_stricmp(cmd, "key") || !_stricmp(cmd, "keydown") || !_stricmp(cmd, "keyup")) && n >= 2) {
        if (!lookup_key(a, &vk, &ext)) { logf_("input: unknown key %s", a); return; }
        if (!_stricmp(cmd, "keyup")) send_key(vk, ext, 1);
        else {
            send_key(vk, ext, 0);
            if (!_stricmp(cmd, "key")) { Sleep(n >= 3 ? (DWORD)atoi(b) : 120); send_key(vk, ext, 1); }
        }
    }
}
static DWORD WINAPI input_thread(LPVOID unused) {
    char path[MAX_PATH];
    long offset = 0;
    DWORD last_flush = 0;
    (void)unused;
    snprintf(path, sizeof path, "%s\\input.cmd", dump_dir);
    for (;;) {
        FILE *f = fopen(path, "rb");
        if (f) {
            char line[256];
            fseek(f, offset, SEEK_SET);
            while (fgets(line, sizeof line, f)) {
                size_t len = strlen(line);
                if (len == 0 || line[len - 1] != '\n') break; /* partial line: retry next poll */
                offset += (long)len;
                line[len - 1] = 0;
                if (len > 1 && line[len - 2] == '\r') line[len - 2] = 0;
                run_cmd(line);
            }
            fclose(f);
        }
        if (walk_trace_enabled && GetTickCount() - last_flush >= 1000) {
            if (walk_trace_flush(walk_trace_path) < 0) logf_("walk trace flush failed");
            last_flush = GetTickCount();
        }
        Sleep(100);
    }
    return 0;
}

static HRESULT WINAPI hook_present(IDirect3DDevice9 *dev, const RECT *s, const RECT *d, HWND w, const RGNDATA *r) {
    char trig[MAX_PATH];
    const char *reason = NULL;
    unsigned short entry_cw = fpu_control_word();
    presents++;
    if (presents == 1 || presents % 120 == 0)
        logf_("FPU Present entry #%lu thread=%lu cw=0x%04x", presents, GetCurrentThreadId(), entry_cw);
    snprintf(trig, sizeof trig, "%s\\dump.now", dump_dir);
    if (presents <= dump_first) reason = "first";
    else if (dump_every && presents % dump_every == 0) reason = "periodic";
    if (GetFileAttributesA(trig) != INVALID_FILE_ATTRIBUTES) { reason = "on-demand"; DeleteFileA(trig); }
    if (reason) dump_backbuffer(dev, reason);
    if (presents % 120 == 0)
        logf_("stats: presents=%lu draws=%lu (+%lu) clears=%lu resets=%lu dumps=%lu dump_fail=%lu",
              presents, draws, draws - total_draws_at_last_log, clears, resets, dumps, dump_fail);
    if (presents % 120 == 0) total_draws_at_last_log = draws;
    if (presents % 600 == 0)
        logf_("focus: game=%p foreground=%p active=%p focus=%p visible=%d",
              (void *)game_hwnd, (void *)GetForegroundWindow(), (void *)GetActiveWindow(), (void *)GetFocus(), IsWindowVisible(game_hwnd));
    draws_this_frame = 0;
    return real_present(dev, s, d, w, r);
}

static HRESULT WINAPI hook_reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp) {
    HRESULT hr;
    resets++;
    logf_("Reset #%lu requested: %ux%u fmt=%d windowed=%d", resets, pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0,
          pp ? (int)pp->BackBufferFormat : 0, pp ? (int)pp->Windowed : 0);
    hr = real_reset(dev, pp);
    logf_("Reset returned hr=0x%08lx", (unsigned long)hr);
    return hr;
}
static HRESULT WINAPI hook_draw_prim(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT a, UINT b) { draws++; draws_this_frame++; return real_draw_prim(d, t, a, b); }
static HRESULT WINAPI hook_draw_idx(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, INT a, UINT b, UINT c, UINT e, UINT f) { draws++; draws_this_frame++; return real_draw_idx(d, t, a, b, c, e, f); }
static HRESULT WINAPI hook_draw_prim_up(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT a, const void *b, UINT c) { draws++; draws_this_frame++; return real_draw_prim_up(d, t, a, b, c); }
static HRESULT WINAPI hook_draw_idx_up(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT a, UINT b, UINT c, const void *e, D3DFORMAT f, const void *g, UINT h) { draws++; draws_this_frame++; return real_draw_idx_up(d, t, a, b, c, e, f, g, h); }
static HRESULT WINAPI hook_clear(IDirect3DDevice9 *d, DWORD a, const D3DRECT *b, DWORD c, D3DCOLOR e, float f, DWORD g) { clears++; return real_clear(d, a, b, c, e, f, g); }

static HRESULT WINAPI hook_create_device(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out) {
    HRESULT hr;
    logf_("CreateDevice adapter=%u type=%d focus=%p flags=0x%lx", adapter, (int)type, (void *)focus, (unsigned long)flags);
    if (pp)
        logf_("  params: %ux%u fmt=%d backbuffers=%u msaa=%d swap=%d hwnd=%p windowed=%d autoDS=%d dsfmt=%d flags=0x%lx refresh=%u interval=0x%x",
              pp->BackBufferWidth, pp->BackBufferHeight, (int)pp->BackBufferFormat, pp->BackBufferCount, (int)pp->MultiSampleType,
              (int)pp->SwapEffect, (void *)pp->hDeviceWindow, (int)pp->Windowed, (int)pp->EnableAutoDepthStencil,
              (int)pp->AutoDepthStencilFormat, (unsigned long)pp->Flags, pp->FullScreen_RefreshRateInHz, pp->PresentationInterval);
    {
        unsigned short before = fpu_control_word(), after;
        hr = real_create_device(d3d, adapter, type, focus, flags, pp, out);
        after = fpu_control_word();
        logf_("FPU CreateDevice thread=%lu before=0x%04x after=0x%04x", GetCurrentThreadId(), before, after);
    }
    logf_("CreateDevice returned hr=0x%08lx device=%p", (unsigned long)hr, out ? (void *)*out : NULL);
    game_hwnd = (pp && pp->hDeviceWindow) ? pp->hDeviceWindow : focus;
    if (SUCCEEDED(hr) && out && *out) {
        void **vt = (void **)(*out)->lpVtbl;
        patch_vtable(vt, 16, hook_reset, (void **)&real_reset);
        patch_vtable(vt, 17, hook_present, (void **)&real_present);
        patch_vtable(vt, 43, hook_clear, (void **)&real_clear);
        patch_vtable(vt, 81, hook_draw_prim, (void **)&real_draw_prim);
        patch_vtable(vt, 82, hook_draw_idx, (void **)&real_draw_idx);
        patch_vtable(vt, 83, hook_draw_prim_up, (void **)&real_draw_prim_up);
        patch_vtable(vt, 84, hook_draw_idx_up, (void **)&real_draw_idx_up);
        logf_("device vtable patched (Reset, Present, Clear, DrawPrimitive x4)");
    }
    return hr;
}

__declspec(dllexport) IDirect3D9 *WINAPI Direct3DCreate9(UINT version) {
    static HMODULE real_dll;
    static create9_fn real_create9;
    IDirect3D9 *d3d;
    if (!real_dll) {
        char path[MAX_PATH], env[MAX_PATH];
        GetSystemDirectoryA(path, sizeof path);
        strncat(path, "\\d3d9.dll", sizeof path - strlen(path) - 1);
        first_tick = GetTickCount();
        if (GetEnvironmentVariableA("SLRR_DUMP_DIR", dump_dir, sizeof dump_dir) == 0) {
            GetModuleFileNameA(NULL, dump_dir, sizeof dump_dir);
            *strrchr(dump_dir, '\\') = 0;
            strncat(dump_dir, "\\dumps", sizeof dump_dir - strlen(dump_dir) - 1);
        }
        CreateDirectoryA(dump_dir, NULL);
        snprintf(log_path, sizeof log_path, "%s\\d3d9proxy.log", dump_dir);
        logf_("clock_origin_tick=%lu", first_tick);
        if (GetEnvironmentVariableA("SLRR_WALK_TRACE", env, sizeof env) && !strcmp(env, "1")) {
            char exe_path[MAX_PATH];
            DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof exe_path);
            int n = snprintf(walk_trace_path, sizeof walk_trace_path, "%s\\stock-walk.jsonl", dump_dir);
            if (len && len < sizeof exe_path && n >= 0 && n < (int)sizeof walk_trace_path)
                walk_trace_enabled = walk_trace_install(GetModuleHandleA(NULL), exe_path);
            logf_("stock walk trace %s", walk_trace_enabled ? "installed" : "refused");
        }
        if (GetEnvironmentVariableA("SLRR_DUMP_EVERY", env, sizeof env)) dump_every = (unsigned)atoi(env);
        if (GetEnvironmentVariableA("SLRR_DUMP_FIRST", env, sizeof env)) dump_first = (unsigned)atoi(env);
        real_dll = LoadLibraryA(path);
        logf_("proxy loaded; real d3d9 path=%s handle=%p dump_dir=%s every=%u first=%u", path, (void *)real_dll, dump_dir, dump_every, dump_first);
        if (!real_dll) return NULL;
        CreateThread(NULL, 0, input_thread, NULL, 0, NULL);
        real_create9 = (create9_fn)(void *)GetProcAddress(real_dll, "Direct3DCreate9");
        if (!real_create9) return NULL;
    }
    d3d = real_create9(version);
    logf_("Direct3DCreate9(%u) -> %p", version, (void *)d3d);
    if (d3d) patch_vtable((void **)d3d->lpVtbl, 16, hook_create_device, (void **)&real_create_device);
    return d3d;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) { (void)h; (void)reason; (void)r; return TRUE; }
