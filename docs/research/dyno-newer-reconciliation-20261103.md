# Newer Dyno native reconciliation

Status: shipped-reference research and independent implementation review; not exact numerical or complete US17 acceptance. Parent owns production model/native integration and the original-code oracle.

## Identity and retained evidence

The proposed46a960 calc seed was historical and incorrect for the newer executable. Exact registry instructions4c6937..4c694b bind `java.game.parts.DynoData.calcDyno(F)F` to004cba80. Following records bind `getTorque(FF)F` to004cc2b0 and `getHP(FF)F` to004cc1b0. Registry class/name/signature strings are retained. The wrapper raw range1840B includes the complete calc body and a following method fragment; no claim treats all1840B as one procedure.

All newer evidence below binds SHA256 `f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2`, under unchanged registered8MiB `local/verify/issue53-car-refresh-20261102`:

| Child | Scope | Automatic query→close duration |
|---|---|---|
|dyno-newer-wrapper-v1|Registry96B, names128B, wrapper1840B|59.726s; holder41661 released0|
|dyno-newer-model-v1|Core/reset/getters/fuel and geometry preparation|52.565s; holder49872 released0|
|dyno-newer-combustion-v1|Combustion/cam preparation/state initialization/constructor/cycle runner; core instruction ABI and constants|62.767s; holder2337 released0|
|dyno-newer-math-abi-v1|Exact combustion instructions and three math-callee metadata records|62.585s; holder52903 released0|

Every session is one executable, finite180seconds, automatically retained/closed before analysis and released. Wrapper/model postchecks REVIEW were manually inspected as fixed target code/string data, with provider metadata boilerplate ignored. Combustion/function and core/math instruction postchecks PASS. The isolated nitro-threshold4B5938c4 postcheck is SKIP(relevance.24): not read, decoded, used or rerolled. Exact newer trigger threshold remains unresolved. Math metadata was initially printed before its own screen; only generic names/extents were exposed and no action taken; all three subsequent screens PASS. This process limitation is retained.

`dyno-newer-wrapper-review-v1` verifies three raw ranges equal immutable PE bytes and retains bounded LLVM supplementary decoding. `dyno-newer-reconciliation-review-v1` verifies four more accepted raw ranges, exports all eleven selected method bodies in `newer-functions.txt`, retains field names, exact math-call windows, posts and source bindings. Its original exponent-pending note is superseded by the independently qualified records below, without rewriting that artifact.

Core/reset/getter batch evidence `ev_54874cae6a675aafd9211f1af7a3a22212bff27f90dff03984f7760f83d1790f`. Combustion/helper batch `ev_c0885e91a8f41aa93bc4db535ac68142829b03970592b9fe5520d857074a305d`. Exact core instructions `ev_d1486f26478583afecf73779657929822260e42527a7c0d186e08c17ffaf21ac`.

## Governing methods and contract

| Newer | Meaning | Older reference lead |
|---|---|---|
|4f44a0|Constructor, initializes four curve pointers and bank flag|4b6600|
|4f49e0|Per-call reset; does not clear curve banks|4b6670|
|4f4640|Selected-bank torque/power table generation|4b77f0|
|4f4ca0|Per-RPM timing/boost/consumption preparation|4b6bf0|
|4f4fc0|Cycle state initialization|4b68c0|
|4f5980|Mixture fraction/density preparation|part of older preparation|
|4f59f0|Cylinder geometry preparation|4b69a0|
|4f4560|Run one complete combustion cycle|4b7740|
|4f50e0|Combustion step|4b7020|

Older full core/combustion/preparation are parent-qualified in `dyno-core-rea.json`; historical all.c supplies remaining older leads. Newer governs.

The exact input name is `Vmin`, not Port's former `v_min`. Bore and stroke are metres and each is multiplied by Float32.5 into internal half-radii. Wrapper copies cylinder/valve timing/spark/boost/mixture/consumption fields; optional P_turbo_waste defaults to P_turbo_max, optional intercooling defaults to1 and scales intake temperature. T_loss is not copied.

Primary step is `max(maxRPM/requested,1)` converted with CVTTSS2SI to an integer. Core takes that integer in stack argument+8, ECX=model, returns RET4; core count is integer division trunc(maxRPM)/step. Legacy public table_stepsize receives the untruncated Float32 quotient after primary clamp. Nitro's second branch truncates maxRPM/requested without the primary MAXSS; a zero integer step requires a typed unsupported outcome in Port rather than invented curves.

Reset preserves existing curve pointers. Core frees/replaces only the selected bank. If a later call takes no nitro branch, its previous secondary bank remains allocated. Native getters always sample primary torque(+1dc) or power(+1e4), ignore their second nitro argument, interpolate using integer step, return0 for negative RPM/missing pointer/count≤1 and exclude the last grid point. getHP returns the stored watt value; only published peakHP uses the0.001341 conversion. Temporary-model legacy arrays clamp copied negative values to0; persistent native tables retain negatives. Primary statistics are published before optional secondary generation.

## Material newer changes

Newer adds guards absent from older4b7020: clamp negative Vmin/total/previous volume to0; mean pressure becomes0 when swept volume≤1e-8; a newly computed volume<1e-7 is set to1e-7 and skips flow/heat/work; mixture mass and temperatures are guarded before divisions; intake pressure is clamped to1e-6. The burn numerator deliberately uses mass cached before its denominator's mass clamp. These are specific source operations, not a blanket clamp of every input or field.

Newer period is120/RPM, rather than old1/(RPM*.008333334). Fuel fraction uses one reciprocal1/(mixture+1), then air fraction multiplies that reciprocal by mixture. Incoming weighted mixtures multiply by one reciprocal mass rather than dividing each sum. Work accumulation groups `(newPressure+oldPressure)*.5*(newVolume-oldVolume)+work`. Core convergence uses previous*.4+fresh*.6. The selected full bodies establish additional operation ordering; a scalar reimplementation is still numerically unverified without its original-code comparison.

The exact combustion instructions show sine and cosine caller inputs/outputs in XMM0 as Float32. At4f52e3 the sine result first multiplies by crank radius, then4f52ec squares that result. `sin*r*sin*r` flattened pseudocode must not be treated as its original arithmetic grouping. Power calls571143 at4f5381/539d receive XMM0=previousVolume/newVolume and XMM1=pressure/temperature exponent; return is Float32 XMM0. These are observed caller ABIs; the generic callee metadata alone does not prove host-libm bit equivalence.

Independent allocator-window records in `local/verify/issue53-native-clock-abi-v3-20261102/dyno-newer-guards-v1` bind exponent59ab6c to bits3fa66666(Float32 1.2999999523162842), evidence `ev_ed6d8c90d56767b67b2c292c35d7d803082cc24ae975c8211ed03e49867147dc`, and593704 to bits3e99999a(Float32.30000001192092896), evidence `ev_0ed58ae012fabcac02f3f8a7c67788bd7f0c5b97fc2efb76786791daa699a06f`. Subject SHA checked, complete4B each; provider holder4621 closed/released0 in67.502s, post REVIEW manually inspected as fixed hex. Those are1.3/.3, not1.4/.4. That independent window confirms destructor4f44e0 frees all four bank pointers via567950; allocator567a20 and record size1ec prepare the newer oracle. No epsilon query was repeated.

## Independent Port review

Focused review of parent-owned dyno_model.cpp/hpp, native Dyno block and shared model lifetime found a concrete piston arithmetic grouping defect: code multiplied left-associatively `sin*r*sin*r`; newer instructions square `(sin*r)`. It was reported before a game capture. Full parameter copying, metre units, primary getter units/boundaries, persistent versus temporary arrays and delete/getter lifecycle otherwise match the qualified bounded contract. Parent was already correcting the secondary zero-step branch.

The initial zero-stroke clear-engine hypothesis was corrected after the parent's source check and fresh jg/focused source discovery: DynoData.clear() actually uses bore/stroke.01,Vmin100000,cylinders0,flows1,time_burn100000,RPM_limit10000,maxRPM0. Chassis.java675–694 invokes calcDyno only inside if(engine); its no-engine else branch713–729 directly clears torque/torque2, sets inertia10,maxRPM7000, friction/starter/gears/ratio defaults, and does not call Dyno. Block.java185 clears,186 sets maxRPM=-1,188 converts bore mm→m,196 fills cylinders, and207–209 obtains crank data and converts stroke mm→m. Therefore the fabricated zero-stroke input does not establish an ordinary no-engine defect. A real clear-input model/original-core control is still pending; no empty curve was invented. Fresh newer newNative's full registry/lifecycle body was not independently bound by this research; its idempotence agrees with older historical46a960, while newer ctor/delete observations are separately bound.

Further instruction review found two additional groupings hidden by flattened pseudocode:4f5250 computes pressureWork=meanPressure*sweptVolume before multiplying by cylinders for torque or RPM for gross power;4f5640 computes burnEnergy=fuelHeat*oldMass before multiplying by deltaBurn. Both were reported for correction. Flow weighted mixtures, mass outflow, cooling and work groupings otherwise match the inspected finite-input expressions. CMake disables floating contraction. Exact scalar/CRT equivalence remains an original-code comparison question.

No exact numerical match, full game driving, all engine/no-engine transitions, legacy-array unsafe lengths, nonfinite inputs or complete post-refresh vehicle integration is accepted by this report. Parent's original-code oracle and actual game captures remain necessary.
