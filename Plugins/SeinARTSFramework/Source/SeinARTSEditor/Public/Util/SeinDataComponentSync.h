/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDataComponentSync.h
 * @brief   AC-authoring prototype: mirrors a data-component Blueprint's
 *          variables into a paired UserDefinedStruct ("<BPName>Data") and
 *          stamps that UDS onto the CDO's `PayloadStruct` — the injection
 *          payload type the bridge bakes into ComponentData.
 *
 *          Sibling of SeinMovementTuning (SeinMovementTuningExport.h) with
 *          three deliberate differences: it qualifies EVERY deterministic
 *          Blueprint variable (a data component's variables ARE its data, so
 *          Instance-Editable is a UI choice, not a membership filter), it
 *          stamps the UDS as a top-level entity component (not sub-data), and
 *          it runs automatically after compile (deferred one tick by the
 *          module's authoring hooks — never inside the compiler, per the
 *          movement util's reentrancy rule). Field identity is rename-stable
 *          via the same source-variable GUID stamps, so renaming a Blueprint
 *          variable renames its UDS field and preserves authored values.
 *          Consolidating the two utils' shared internals is deliberate
 *          post-prototype work.
 */

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UUserDefinedStruct;

namespace SeinDataComponentSync
{
	/** True if `Blueprint` generates a USeinDataComponent subclass. */
	bool IsDataComponentBlueprint(const UBlueprint* Blueprint);

	/** Sync the Blueprint's deterministic variables into its paired payload
	 *  UDS, stamp the UDS onto the CDO (and loaded instances) `PayloadStruct`,
	 *  and return it. With zero qualifying variables the link is cleared and
	 *  no asset is created. Editor-only; must be called OUTSIDE Blueprint/UDS
	 *  compilation. */
	UUserDefinedStruct* SyncPayloadStructForBlueprint(UBlueprint* Blueprint);
}
