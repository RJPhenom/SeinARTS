/**
 * Non-shipping test fixture for explicitly trusted snapshot adoption.
 * Production code never depends on this module.
 */

#pragma once

#include "CoreMinimal.h"

class USeinWorldSubsystem;
struct FSeinSnapshotRestoreOptions;
struct FSeinWorldSnapshot;

namespace SeinTestSnapshotRestore
{
	/**
	 * Claim the test suite's world-scoped restore authority and adopt the
	 * captured local-state policy. Claim failures are optionally returned to
	 * the caller; snapshot validation failures remain owned by CoreEntity.
	 */
	SEINARTSTESTSUPPORT_API bool RestoreTrusted(
		USeinWorldSubsystem& World,
		const FSeinWorldSnapshot& Snapshot,
		FString* OutClaimError = nullptr);

	/** Explicit-policy variant for multiplayer catch-up, dormant adoption,
	 *  and local-presentation tests. */
	SEINARTSTESTSUPPORT_API bool RestoreTrusted(
		USeinWorldSubsystem& World,
		const FSeinWorldSnapshot& Snapshot,
		const FSeinSnapshotRestoreOptions& Options,
		FString* OutClaimError = nullptr);
}
