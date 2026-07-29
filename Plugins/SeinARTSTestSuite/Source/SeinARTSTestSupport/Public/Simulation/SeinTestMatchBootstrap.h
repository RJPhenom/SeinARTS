/**
 * Non-shipping test fixture for explicit tick-zero bootstrap phases.
 * Production code never depends on this module.
 */

#pragma once

#include "CoreMinimal.h"
#include "Data/SeinMatchSettings.h"

class USeinWorldSubsystem;

namespace SeinTestMatchBootstrap
{
	/**
	 * Materialize an empty tick-zero world through Core's authority-gated
	 * delegate, leaving an exact local receipt ready for authorization.
	 */
	SEINARTSTESTSUPPORT_API bool Materialize(
		USeinWorldSubsystem& World,
		const FSeinMatchSettings& Settings = FSeinMatchSettings(),
		int64 SessionSeed = 0,
		FName FixtureID = FName(TEXT("SeinARTSTestSupport.Default")),
		FString* OutError = nullptr);

	/**
	 * Materialize designer/test-authored tick-zero state. AuthorState executes
	 * synchronously while Core's Applying capability is active and is never
	 * retained after this call.
	 */
	SEINARTSTESTSUPPORT_API bool Materialize(
		USeinWorldSubsystem& World,
		TFunctionRef<void()> AuthorState,
		const FSeinMatchSettings& Settings = FSeinMatchSettings(),
		int64 SessionSeed = 0,
		FName FixtureID = FName(TEXT("SeinARTSTestSupport.Default")),
		FString* OutError = nullptr);

	/** Authorize an already materialized fixture without starting the ticker. */
	SEINARTSTESTSUPPORT_API bool Authorize(
		USeinWorldSubsystem& World,
		FString* OutError = nullptr);

	/** Materialize if needed, authorize, then launch or resume the ticker. */
	SEINARTSTESTSUPPORT_API bool Start(
		USeinWorldSubsystem& World,
		FString* OutError = nullptr);
}
