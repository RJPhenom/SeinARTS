/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 */

#include "SeinARTSSquadModule.h"
#include "SeinARTSSquadSettings.h"
#include "Settings/SeinConfigFingerprintRegistry.h"

IMPLEMENT_MODULE(FSeinARTSSquadModule, SeinARTSSquad)

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSSquad, Log, All);

namespace
{
	// FROZEN cross-client wire identifier — it drives the deterministic fingerprint
	// fold order. NEVER rename it: a different id sorts to a different position and
	// changes every client's fingerprint.
	const FName GSquadFingerprintId(TEXT("SquadExtension"));
}

void FSeinARTSSquadModule::StartupModule()
{
	// Register this extension's SIM-AFFECTING settings into the lockstep config-parity
	// fingerprint, so two clients differing on them (or one missing this plugin) are
	// rejected at join instead of silently desyncing. Compile-time member checks and
	// registry validation keep schema mistakes from degrading to empty values.
	if (!FSeinConfigFingerprintRegistry::RegisterContributor(
		GSquadFingerprintId,
		GetDefault<USeinARTSSquadSettings>(),
		{
			GET_MEMBER_NAME_CHECKED(USeinARTSSquadSettings, bPaceSquadsTogether),
			GET_MEMBER_NAME_CHECKED(USeinARTSSquadSettings, DefaultSquadDispatchResolverClass),
		}))
	{
		UE_LOG(LogSeinARTSSquad, Fatal,
			TEXT("Squad's lockstep config-fingerprint schema failed to register."));
	}
}

void FSeinARTSSquadModule::ShutdownModule()
{
	FSeinConfigFingerprintRegistry::UnregisterContributor(GSquadFingerprintId);
}
