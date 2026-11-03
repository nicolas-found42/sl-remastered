/* Private guest launcher: wait on the actual game handle, never terminate it. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR args, int show) {
    STARTUPINFOA si;
    (void)instance; (void)previous; (void)args; (void)show;
    PROCESS_INFORMATION pi;
    DWORD code, waited;
    FILE *f;
    char command[] = "Z:\\work\\game\\StreetLegal_Redline.exe";
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(command, command, NULL, NULL, FALSE, 0, NULL,
                        "Z:\\work\\game", &si, &pi)) return 70;
    f = fopen("Z:\\work\\game-process-start.json.tmp", "wb");
    if (!f) return 71;
    fprintf(f, "{\"pid\":%lu,\"tick\":%lu}\n", pi.dwProcessId, GetTickCount());
    fclose(f);
    if (!MoveFileA("Z:\\work\\game-process-start.json.tmp", "Z:\\work\\game-process-start.json")) return 75;
    CloseHandle(pi.hThread);
    waited = WaitForSingleObject(pi.hProcess, INFINITE);
    if (waited != WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess, &code)) return 72;
    f = fopen("Z:\\work\\game-process-exit.json.tmp", "wb");
    if (!f) return 73;
    fprintf(f, "{\"pid\":%lu,\"wait_result\":%lu,\"exit_code\":%lu,\"tick\":%lu}\n",
            pi.dwProcessId, waited, code, GetTickCount());
    fclose(f);
    if (!MoveFileA("Z:\\work\\game-process-exit.json.tmp", "Z:\\work\\game-process-exit.json")) return 76;
    CloseHandle(pi.hProcess);
    return code == 0 ? 0 : 74;
}
