/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverModule.cpp
 */

#include "SeinARTSCoverModule.h"
#include "Settings/SeinARTSCoverSettings.h"
#include "Settings/SeinConfigFingerprintRegistry.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSCover, Log, All);

namespace
{
	// Frozen cross-client identifier: renaming it changes lockstep compatibility.
	const FName GCoverFingerprintId(TEXT("CoverExtension"));
}

void FSeinARTSCoverModule::StartupModule()
{
	if (!FSeinConfigFingerprintRegistry::RegisterContributor(
		GCoverFingerprintId,
		GetDefault<USeinARTSCoverSettings>(),
		{
			GET_MEMBER_NAME_CHECKED(USeinARTSCoverSettings, CoverSystemClass),
			GET_MEMBER_NAME_CHECKED(USeinARTSCoverSettings, CoverSnapRadius),
			GET_MEMBER_NAME_CHECKED(USeinARTSCoverSettings, TerrainCoverQuality),
		}))
	{
		UE_LOG(LogSeinARTSCover, Fatal,
			TEXT("Cover's lockstep config-fingerprint schema failed to register."));
	}
	UE_LOG(LogSeinARTSCover, Log, TEXT("SeinARTSCover module started."));
}

void FSeinARTSCoverModule::ShutdownModule()
{
	FSeinConfigFingerprintRegistry::UnregisterContributor(GCoverFingerprintId);
	UE_LOG(LogSeinARTSCover, Log, TEXT("SeinARTSCover module shut down."));
}

IMPLEMENT_MODULE(FSeinARTSCoverModule, SeinARTSCover)
