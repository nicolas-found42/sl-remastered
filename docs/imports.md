# Imports of the stock executable

The stock executable `ref/raw/StreetLegal_Redline.bak.exe` imports 194 functions from 10 Windows libraries. Ghidra's `list_imports` produced this list on the analyzed program (MD5 `e686099e7233bb7d7c461b1b35c5064c`).

| Library | Imports | Functions that matter for the port |
|---|---:|---|
| `KERNEL32.DLL` | 127 | Files (`CreateFileA`, `ReadFile`, `FindFirstFileA`), memory-mapped files (`CreateFileMappingA`, `MapViewOfFile`), threads, events, semaphores, critical sections, heaps, `VirtualAlloc`, `LoadLibraryA`, `GetProcAddress` |
| `USER32.DLL` | 31 | Window creation (`CreateWindowExA`, `RegisterClassA`), the message loop (`PeekMessageA`, `DispatchMessageA`), cursor control, `GetAsyncKeyState`, keyboard layout |
| `WINMM.DLL` | 15 | Timers (`timeGetTime`, `timeSetEvent`), media control (`mciSendCommandA`), WAV file reading (`mmioOpenA`, `mmioRead`, `mmioDescend`) |
| `OLE32.DLL` | 7 | COM (`CoInitialize`, `CoCreateInstance`, `CoTaskMemAlloc`) |
| `ADVAPI32.DLL` | 6 | Registry (`RegOpenKeyExA`, `RegQueryValueExA`, `RegSetValueExA`) |
| `OLEAUT32.DLL` | 3 | Ordinals 2, 6, and 9 |
| `GDI32.DLL` | 2 | `DeleteObject`, `GetStockObject` |
| `D3D9.DLL` | 1 | `Direct3DCreate9` |
| `DINPUT8.DLL` | 1 | `DirectInput8Create` |
| `DSOUND.DLL` | 1 | Ordinal 1 |

## Reading the list

The three DirectX libraries each export one function. Every other Direct3D 9, DirectInput 8, and DirectSound call goes through function tables on the objects those entry points return, so the list does not show them. `phase1.md` counts them. The executable also uses DirectShow, which it reaches through `CoCreateInstance`.

The `DSOUND.DLL` ordinal 1 is `DirectSoundCreate` and the `OLEAUT32.DLL` ordinals are string and variant helpers. These two identifications come from knowledge of the Windows libraries, not from the executable, and no one has checked them yet.

The executable imports nothing from a Winsock library. Its `LoadLibraryA` calls load only `d3d9.dll`, `d3d9d.dll`, `OleAut32.dll`, and `user32.dll`, so it has no networking code of that kind. `phase1.md` has the details.

## Regenerate

Open the stock executable in Ghidra as described in `ghidra.md`, then ask the agent to run `list_imports` with `limit` 500.
