#pragma once

#include "natives/registry.hpp"

class ResourceWorld;
class Render2D;
struct ResolvedNode;
// Bounded draw-time material cache; callers must establish the stock load request.
NativeResult cacheDrawMaterial(ResourceWorld&, Render2D&, const ResolvedNode&);

// Handler at 0047cf80, java.util.resource.ResourceRef.set(I)V. The receiver's native link record is its
// ReceiverBinding in VmGameState (created on demand, since ResourceRef.newNative already allocated the record in
// stock). A receiver whose `ptr` is 0 is a silent no-op. Otherwise it runs resourceRefSet against
// VmGameState::resources and copies the link's id word into the receiver's VmResourceState.words[2], whether or
// not the id was found. A crash or a refused pack mount in the host model becomes a VM error with its detail.
NativeResult resourceRefSetInt(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult resourceRefId(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);

NativeResult resourceRefCache(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult resourceRefPrecache(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>* classes);

// Bounded type-14 RenderRef.changeResource at 00480220.
NativeResult renderRefChangeResource(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                     const std::vector<const TufaClass*>* classes);

// Bounded CPU mesh scaling for ResourceRef.scaleMesh at 00480390.
NativeResult resourceRefScaleMesh(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                  const std::vector<const TufaClass*>* classes);

// Bounded type-14 RenderRef.create and identity-rotation setMatrix handlers.
NativeResult renderRefCreate(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>* classes);
NativeResult renderRefGetTypeId(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult renderRefSetMatrix(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes);
NativeResult renderRefSetLight(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);
NativeResult renderRefSetFlare(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes);

NativeResult resourceRefFirstChild(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult resourceRefNextChild(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);

struct LinkOwner;
NativeResult wrapResourceReference(LinkOwner* target, VmArena* arena, const std::vector<const TufaClass*>* classes);

NativeResult resourceRefDeleteNative(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult resourceRefDestroy(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult resourceRefGetParent(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult resourceRefGetParentId(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult resourceRefGetWTRoot(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult resourceRefLoad(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult resourceRefSetRef(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult resourceRefType(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult resourceRefUnload(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
