#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
int main(void) {
    MCI_OPEN_PARMSA open = {0}; MCI_SET_PARMS set = {0}; MCI_STATUS_PARMS status = {0};
    MCI_GENERIC_PARMS close = {0}; MCIERROR error; unsigned i;
    printf("drive D: type %u\n", GetDriveTypeA("D:\\"));
    open.lpstrDeviceType = (const char *)(DWORD_PTR)MCI_DEVTYPE_CD_AUDIO;
    open.lpstrElementName = "D:";
    error = mciSendCommandA(0, MCI_OPEN, MCI_WAIT | MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID | MCI_OPEN_ELEMENT, (DWORD_PTR)&open);
    printf("open error %lu\n", (unsigned long)error); if (error) return 1;
    set.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
    error = mciSendCommandA(open.wDeviceID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&set);
    printf("set error %lu\n", (unsigned long)error); if (error) return 2;
    status.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
    error = mciSendCommandA(open.wDeviceID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status);
    printf("count error %lu value %lu\n", (unsigned long)error, (unsigned long)status.dwReturn); if (error) return 3;
    for (i = 1; i <= status.dwReturn; ++i) {
        MCI_STATUS_PARMS track = {0}; track.dwItem = MCI_STATUS_LENGTH; track.dwTrack = i;
        error = mciSendCommandA(open.wDeviceID, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK, (DWORD_PTR)&track);
        printf("track %u error %lu value %lu\n", i, (unsigned long)error, (unsigned long)track.dwReturn); if (error) return 4;
    }
    status.dwItem = MCI_STATUS_LENGTH;
    error = mciSendCommandA(open.wDeviceID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status);
    printf("total error %lu value %lu\n", (unsigned long)error, (unsigned long)status.dwReturn);
    printf("play error %lu\n", (unsigned long)mciSendCommandA(open.wDeviceID, MCI_PLAY, 0, 0));
    mciSendCommandA(open.wDeviceID, MCI_CLOSE, 0, (DWORD_PTR)&close);
    return error ? 5 : 0;
}
