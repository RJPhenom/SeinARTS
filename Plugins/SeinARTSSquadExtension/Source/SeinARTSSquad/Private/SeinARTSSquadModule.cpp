/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 */

#include "SeinARTSSquadModule.h"
#include "SeinARTSSquadSettings.h"
#include "Settings/SeinConfigFingerprintRegistry.h"

IMPLEMENT_MODULE(FSeinARTSSquadModule, SeinARTSSquad)

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
	// rejected at join instead of silently desyncing. Field names must match the
	// UPROPERTY names on USeinARTSSquadSettings exactly (a typo yields an empty value
	// and silently re-opens the gap).
	FSeinConfigFingerprintRegistry::RegisterContributor(
		GSquadFingerprintId,
		GetDefault<USeinARTSSquadSettings>(),
		{ TEXT("bPaceSquadsTogether"), TEXT("DefaultSquadDispatchResolverClass") });
}

void FSeinARTSSquadModule::ShutdownModule()
{
	FSeinConfigFingerprintRegistry::UnregisterContributor(GSquadFingerprintId);
}
