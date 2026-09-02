/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentEligibility.h
 * @brief   Single source of truth for "is this struct a valid top-level entity
 *          ComponentData entry?" Shared by the editor bridge-picker filter
 *          (USeinSimComponentFactory, an Editor module) and the K2 Get/Set
 *          Component node menu (SeinARTSGraphNodes, UncookedOnly) so the two
 *          eligibility rules can't drift. Both depend on SeinARTSCoreEntity.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinPayload.h"
#if WITH_EDITORONLY_DATA
#include "StructUtils/UserDefinedStruct.h"
#endif

namespace SeinComponentEligibility
{
	/** True iff `Struct` is eligible as a top-level entity ComponentData entry:
	 *  carries the `SeinDeterministic` meta, is NOT `SeinSubData`, and is either a
	 *  native USTRUCT inheriting FSeinPayload OR a UserDefinedStruct carrying the
	 *  `SeinEntityComponent` meta. (UDS IsChildOf is unreliable — UE's UDS compiler
	 *  clears SuperStruct — so the meta tag stamped by USeinSimComponentFactory is
	 *  the substitute.) Metadata is editor-only, so this returns false in cooked /
	 *  no-editor builds; the only callers are Editor + UncookedOnly modules. */
	inline bool IsEntityComponentStruct(const UStruct* Struct)
	{
#if WITH_EDITORONLY_DATA
		if (!Struct) return false;
		static const FName DeterministicMeta(TEXT("SeinDeterministic"));
		static const FName EntityComponentMeta(TEXT("SeinEntityComponent"));
		static const FName SubDataMeta(TEXT("SeinSubData"));
		if (Struct->HasMetaData(SubDataMeta)) return false;        // sub-data veto (nested, not top-level)
		if (!Struct->HasMetaData(DeterministicMeta)) return false; // must be deterministic
		if (Struct->IsA<UUserDefinedStruct>())
		{
			return Struct->HasMetaData(EntityComponentMeta);
		}
		if (const UScriptStruct* SS = Cast<UScriptStruct>(Struct))
		{
			return SS->IsChildOf(FSeinPayload::StaticStruct());
		}
		return false;
#else
		(void)Struct;
		return false;
#endif
	}
}
