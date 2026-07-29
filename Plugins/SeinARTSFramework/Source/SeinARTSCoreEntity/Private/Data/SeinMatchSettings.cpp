/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchSettings.cpp
 */

#include "Data/SeinMatchSettings.h"

#include "Data/SeinMatchBootstrapRules.h"

#include "Serialization/SeinDeterministicValueDigest.h"

bool SeinCanonicalizeAndDigestMatchSettings(
	FSeinMatchSettings& Settings,
	FGuid& OutDigest,
	FSeinDeterministicValueDigestError* OutError)
{
	OutDigest.Invalidate();
	FSeinMatchSettings Canonical = Settings;
	for (FInstancedStruct& Extension : Canonical.Extensions)
	{
		if (!Extension.IsValid() || !Extension.GetScriptStruct()
			|| !Extension.GetMemory())
		{
			if (OutError)
			{
				OutError->Result =
					ESeinDeterministicValueDigestResult::InvalidInstancedStruct;
				OutError->FieldPath = TEXT("$.Extensions");
				OutError->Message = TEXT("Match extension has no valid concrete value.");
			}
			return false;
		}

		// Extension arrays retain authored order by default. This framework-owned
		// rule is logically a tag-keyed set, so make its representation independent
		// of lobby/UI insertion order before the enclosing settings are digested.
		if (Extension.GetScriptStruct() == FSeinMatchBootstrapRules::StaticStruct())
		{
			FSeinMatchBootstrapRules* Rules =
				Extension.GetMutablePtr<FSeinMatchBootstrapRules>();
			check(Rules);
			Rules->StartingResources.Sort(
				[](const FSeinStartingResourceOverride& A,
					const FSeinStartingResourceOverride& B)
				{
					return A.ResourceTag.ToString() < B.ResourceTag.ToString();
				});
		}
	}

	Canonical.Slots.Sort([](const FSeinMatchSlot& A, const FSeinMatchSlot& B)
	{
		return A.SlotIndex < B.SlotIndex;
	});
	Canonical.Extensions.Sort(
		[](const FInstancedStruct& A, const FInstancedStruct& B)
		{
			return A.GetScriptStruct()->GetPathName()
				< B.GetScriptStruct()->GetPathName();
		});

	FSeinDeterministicValueDigestOptions Options;
#if !WITH_METADATA
	// Cooked metadata omits SeinDeterministic. The exact concrete type paths
	// remain part of this digest and are compared before simulation begins.
	Options.bTrustCookedTypesWithoutMetadata = true;
#endif
	if (FSeinDeterministicValueDigest::Compute(
			FSeinMatchSettings::StaticStruct(), &Canonical, OutDigest, OutError,
			Options) != ESeinDeterministicValueDigestResult::Success)
	{
		return false;
	}

	Settings = MoveTemp(Canonical);
	return true;
}
