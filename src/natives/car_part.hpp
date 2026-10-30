#ifndef SL_NATIVES_CAR_PART_HPP
#define SL_NATIVES_CAR_PART_HPP

#include "natives/registry.hpp"

// java.game.parts.Part
NativeResult partGetSlots(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                          const std::vector<const TufaClass*>* classes);
NativeResult partGetSlotId(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);
NativeResult partGetSlotIndex(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult partPartOnSlot(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult partSlotIdOnSlot(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult partGetSlotDamage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult partSetSlotDamage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult partSetSlotPos(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult partGetCar(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                        const std::vector<const TufaClass*>* classes);
NativeResult partGetCarRef(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);
NativeResult partGetWheelId(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult partGetMass(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>* classes);
NativeResult partGetWear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>* classes);
NativeResult partSetWear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>* classes);
NativeResult partSetMaxWear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult partGetTear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>* classes);
NativeResult partSetTear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>* classes);
NativeResult partDisableSlot(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>* classes);
NativeResult partIsSlotDisabled(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult partGetTexture(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult partSetTexture(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult partGetMesh(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>* classes);
NativeResult partSetMesh(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>* classes);
NativeResult partGetRenderType(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult partSetRenderType(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult partGetLogo(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>* classes);
NativeResult partSetSfxLoopParams(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                  const std::vector<const TufaClass*>* classes);
NativeResult partFlap(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                      const std::vector<const TufaClass*>* classes);
NativeResult partInstallOk(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);

// java.game.parts.DynoData
NativeResult dynoDataNewNative(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult dynoDataDeleteNative(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                  const std::vector<const TufaClass*>* classes);
NativeResult dynoDataCalcDyno(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult dynoDataGetTorque(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult dynoDataGetHp(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);

// java.game.parts.bodypart.Chassis
NativeResult chassisGetTorque(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult chassisSetTorque(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult chassisGetMass(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult chassisGetCm(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                          const std::vector<const TufaClass*>* classes);
NativeResult chassisGetMin(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);
NativeResult chassisGetMax(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);
NativeResult chassisGetWheels(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult chassisGetWheelPos(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult chassisGetWheelDamage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>* classes);
NativeResult chassisSetWheelDamage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>* classes);
NativeResult chassisGetWheel(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>* classes);
NativeResult chassisGetSfxTable(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult chassisSetCooling(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult chassisSetHornSfx(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult chassisSetNitroSfx(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult chassisGetMileage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult chassisSetMileage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult chassisSetBuck(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult chassisForceUpdate(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult chassisSetSfxExhaustMinVol(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                        const std::vector<const TufaClass*>* classes);
NativeResult chassisSetSteerWheel(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                  const std::vector<const TufaClass*>* classes);
NativeResult chassisSetSteerWheelRadius(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                        const std::vector<const TufaClass*>* classes);
NativeResult chassisSetAckermann(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>* classes);

// java.game.parts.WheelRef
NativeResult wheelRefGetDrive(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetDrive(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult wheelRefGetSteer(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetSteer(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult wheelRefGetRadius(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetRadius(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetFriction(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetFrictnX(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetSliction(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetStiffness(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                  const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetRollRes(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetBearing(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetMaxLoad(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetLoadSmooth(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetPacejka(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetForce(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetDamping1(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetDamping2(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetRestLen(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetMinLen(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetMaxLen(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult wheelRefGetBrake(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetBrake(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult wheelRefGetHBrake(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetHBrake(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetWidth(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetCPatch(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetArm(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetHub(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetOppWheel(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetInstantCenter(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                      const std::vector<const TufaClass*>* classes);
NativeResult wheelRefGetPos(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetPos(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult wheelRefGetYpr(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult wheelRefSetYpr(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);

// java.game.parts.SfxTable
NativeResult sfxTableGetItems(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult sfxTableClear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);
NativeResult sfxTableAddItem(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>* classes);

// java.game.Vehicle
NativeResult vehicleGetHorn(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult vehicleGetSpeedSquare(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>* classes);
NativeResult vehicleHasCrime(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>* classes);

#endif
