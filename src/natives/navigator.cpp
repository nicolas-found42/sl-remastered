#include "natives/navigator.hpp"
#include "run_log.hpp"

// Navigator.updateNavigator(GameRef, int) is FUN_00482d30. It places the
// minimap camera over the followed object (bone00 matrix, offsetX/offsetZ
// clamped to the map range) and rewrites the matrices of the marker and
// dynamic marker symbols. All of that only drives the minimap HUD: nothing
// else reads the map camera's matrices or the script's offset fields. The HUD
// is not rendered yet, so the update is not applied; the call is accepted
// after the same argument checks and the gap is reported once to the run log.
NativeResult navigatorUpdate(const std::vector<VmValue> &args, const VmObject *receiver,
                             VmArena *arena, const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 2 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]))
    return std::string{"Navigator.updateNavigator: invalid call"};
  static bool noted = false;
  if (!noted) {
    noted = true;
    runlog::event(runlog::EventKind::Note,
                  "Navigator.updateNavigator 00482d30: minimap placement not applied "
                  "(unsupported until the HUD map is rendered)");
  }
  return std::monostate{};
}
