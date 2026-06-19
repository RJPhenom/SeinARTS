/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverSettings.cpp
 * @brief   Implementation of the SeinARTS Cover Extension settings.
 */

#include "Settings/SeinARTSCoverSettings.h"

USeinARTSCoverSettings::USeinARTSCoverSettings()
	// CoverSnapRadius 500 ≈ 5m. (Formation-preview settings moved to USeinARTSCoreSettings.)
	: CoverSnapRadius(FFixedPoint::FromInt(500))
{
}

void USeinARTSCoverSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Snap-radius reconcile — a project whose serialized CDO zero-initialized
	// this field (added to an existing project, or stomped by an older INI)
	// would otherwise disable cover-snap entirely. A radius ≤ 0 makes no sense;
	// re-seed to the canonical default.
	if (CoverSnapRadius <= FFixedPoint::Zero)
	{
		CoverSnapRadius = FFixedPoint::FromInt(500);
	}
}

FName USeinARTSCoverSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText USeinARTSCoverSettings::GetSectionText() const
{
	return NSLOCTEXT("SeinARTSCover", "SeinARTSCoverSettingsSection", "SeinARTS Cover Extension");
}

FText USeinARTSCoverSettings::GetSectionDescription() const
{
	return NSLOCTEXT("SeinARTSCover", "SeinARTSCoverSettingsDescription",
		"Configure the SeinARTS Cover Extension — cover system implementation, formation preview, and cover-snap behavior.");
}
#endif
