#pragma once
#include <windows.h>

/* Experimental observer ABI. Bind with GetProcAddress outside loader lock;
 * never load either proxy from a hot callback. The owner is control_tape.c,
 * including its phase counter and stop/drain protocol. No replay is provided.
 *
 * QUALIFIED SCOPE: owned console qualification only. Explicit registration and
 * retirement are NOT a substitute for observed COM creation/QI/Release, format
 * revisions or native thread incarnations. The Stock runner's explicit
 * --input-observer mode is experimental and does not extend qualification.
 * Installation requires quiescent owned vtables; concurrent installation and
 * exception/unwind through a method are not qualified.
 */
#define PARITY_INPUT_VERSION 1u
#define PARITY_INPUT_IDENTITY 0x50494e31u
enum parity_input_kind { PARITY_INPUT_KEYBOARD = 1, PARITY_INPUT_MOUSE = 2 };
struct parity_input_observer_v1 {
  DWORD version, size, identity;
  const volatile LONG* phase_source; /* diagnostic identity, not a replay key */
  /* Validate the entire owned configuration before publishing any vtable slot.
   * device_id/incarnation are nonzero. Reuse requires explicit retirement and
   * a strictly increasing incarnation. Device has IDirectInputDevice8 slots.
   * Only keyboard256 / mouse16 state, acquire and buffered20/count1..16/flags0 (record format7)
   * are observed. Unknown shapes still invoke the saved target and emit gaps.
   */
  int (__cdecl *register_device)(void* device, DWORD device_id, DWORD incarnation, DWORD kind);
  int (__cdecl *retire_device)(void* device, DWORD device_id, DWORD incarnation);
};
typedef const struct parity_input_observer_v1* (__cdecl *parity_input_getter)(DWORD version, DWORD size);
/* Exported only by CONTROL_TAPE_INPUT builds. Null for ABI mismatch/not ready. */
const struct parity_input_observer_v1* __cdecl parity_input_observer(DWORD version, DWORD size);
/* Cold binding only. Finds an already-loaded owner and validates function-table
 * origin; never LoadLibrary, never a private recorder fallback. */
const struct parity_input_observer_v1* __cdecl parity_input_bind(const char* owner_name);

/* Separate opt-in owned COM boundary. The state-only table is unchanged.
 * QI aliases require the same already-published vtable; unknown interface
 * layouts are gaps. Zero Release retires observed aliases without accessing
 * the released object. Concurrent callers and interface publication are not
 * qualified. This table is not bound by the existing state-only client. */
#define PARITY_INPUT_COM_VERSION 1u
#define PARITY_INPUT_COM_IDENTITY 0x50494331u
struct parity_input_com_observer_v1 {
  DWORD version, size, identity;
  const volatile LONG* phase_source;
  int (__cdecl *register_device)(void* device, DWORD device_id, DWORD incarnation, DWORD kind);
  int (__cdecl *retire_device)(void* device, DWORD device_id, DWORD incarnation);
};
const struct parity_input_com_observer_v1* __cdecl parity_input_com_observer(DWORD version, DWORD size);

/* Separate ABI for observed Device8 A/W aliases with different vtables.
 * Preparation is cold/quiescent and requires a caller-observed IID/layout;
 * no QI, discovery, publication or allocation happens in a hot callback.
 * IUnknown is admitted only when the real QI result uses a prepared Device8
 * layout. A distinct three-slot IUnknown/tear-off is not qualified.
 */
#define PARITY_INPUT_COM_VERSION2 2u
#define PARITY_INPUT_COM_IDENTITY2 0x50494332u
struct parity_input_com_observer_v2 {
  DWORD version, size, identity;
  const volatile LONG* phase_source;
  int (__cdecl *register_device)(void* device, DWORD device_id, DWORD incarnation, DWORD kind);
  int (__cdecl *prepare_interface)(void* observed_interface, const GUID* iid);
  /* Explicit cold binding of a caller-observed QI result, before real A/W
   * delegation can enter that sibling interface. Not identity discovery. */
  int (__cdecl *bind_interface)(void* registered_base, void* observed_alias, DWORD device_id, DWORD incarnation);
  int (__cdecl *retire_device)(void* device, DWORD device_id, DWORD incarnation);
};
const struct parity_input_com_observer_v2* __cdecl parity_input_com_observer2(DWORD version, DWORD size);
/* Same already-loaded owner/origin checks as the v1 binding client. */
const struct parity_input_com_observer_v2* __cdecl parity_input_com_bind2(const char* owner_name);
