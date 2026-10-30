/* Synthetic MCI metadata driver for an isolated Wine prefix; no CD audio. */
#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD count, total, tracks[128];
static int load_metadata(void) {
    const char *path = getenv("SLRR_SYNTHETIC_CD_METADATA");
    FILE *f; unsigned long n, length, value; unsigned i; char trailing;
    if (!path || !(f = fopen(path, "r"))) return 0;
    if (fscanf(f, "%lu %lu", &n, &length) != 2 || !n || n > 128 || !length) { fclose(f); return 0; }
    for (i = 0; i < n; ++i) {
        if (fscanf(f, "%lu", &value) != 1) { fclose(f); return 0; }
        tracks[i] = (DWORD)value;
    }
    if (fscanf(f, " %c", &trailing) == 1) { fclose(f); return 0; }
    fclose(f); count = (DWORD)n; total = (DWORD)length;
    fprintf(stderr, "synthetic-cd: EMULATED metadata, %lu tracks, total %lu ms\n", n, length);
    return 1;
}

__declspec(dllexport) LRESULT CALLBACK DriverProc(DWORD_PTR id, HDRVR driver, UINT msg, LPARAM flags, LPARAM data) {
    (void)driver;
    switch (msg) {
    case DRV_LOAD: case DRV_FREE: case DRV_ENABLE: case DRV_DISABLE: return 1;
    case DRV_OPEN: {
        MCI_OPEN_DRIVER_PARMS *p = (MCI_OPEN_DRIVER_PARMS *)data;
        /* Wine 8 passes no parameters here (observed: both lParams are 0). */
        if (!load_metadata()) return 0;
        if (p) {
            p->wCustomCommandTable = MCI_TABLE_NOT_PRESENT;
            p->wType = MCI_DEVTYPE_CD_AUDIO;
            return p->wDeviceID;
        }
        return 1;
    }
    case DRV_CLOSE: return 0;
    case MCI_OPEN_DRIVER: {
        const MCI_OPEN_PARMSW *p = (const MCI_OPEN_PARMSW *)data;
        if (!p || !(flags & MCI_OPEN_ELEMENT) || !p->lpstrElementName ||
            (p->lpstrElementName[0] != L'D' && p->lpstrElementName[0] != L'd') ||
            p->lpstrElementName[1] != L':' || p->lpstrElementName[2] != L'\0') return MCIERR_INVALID_FILE;
        fprintf(stderr, "synthetic-cd: opened synthetic D: device id=%lu\n", (unsigned long)id);
        return 0;
    }
    case MCI_CLOSE_DRIVER: return 0;
    case MCI_SET: {
        const MCI_SET_PARMS *p = (const MCI_SET_PARMS *)data;
        if (!p || !(flags & MCI_SET_TIME_FORMAT) || p->dwTimeFormat != MCI_FORMAT_MILLISECONDS)
            return MCIERR_UNSUPPORTED_FUNCTION;
        return 0;
    }
    case MCI_STATUS: {
        MCI_STATUS_PARMS *p = (MCI_STATUS_PARMS *)data;
        if (!p || !(flags & MCI_STATUS_ITEM)) return MCIERR_UNSUPPORTED_FUNCTION;
        if (p->dwItem == MCI_STATUS_NUMBER_OF_TRACKS && !(flags & MCI_TRACK)) p->dwReturn = count;
        else if (p->dwItem == MCI_STATUS_LENGTH && !(flags & MCI_TRACK)) p->dwReturn = total;
        else if (p->dwItem == MCI_STATUS_LENGTH && (flags & MCI_TRACK) && p->dwTrack >= 1 && p->dwTrack <= count)
            p->dwReturn = tracks[p->dwTrack - 1];
        else return MCIERR_UNSUPPORTED_FUNCTION;
        fprintf(stderr, "synthetic-cd: status item=%lu track=%lu value=%lu ms-or-count\n", (unsigned long)p->dwItem,
                (unsigned long)((flags & MCI_TRACK) ? p->dwTrack : 0), (unsigned long)p->dwReturn);
        return 0;
    }
    default: return MCIERR_UNSUPPORTED_FUNCTION;
    }
}
