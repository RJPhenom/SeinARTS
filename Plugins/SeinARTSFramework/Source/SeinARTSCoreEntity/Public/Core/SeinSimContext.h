/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimContext.h
 * @brief   Determinism enforcement macros for sim/render boundary.
 */

#pragma once

#include "CoreMinimal.h"

class USeinWorldSubsystem;

/** Exported accessors for the thread-local sim-context owner. thread_local
 *  variables can't carry dllexport on MSVC, so external modules call these
 *  functions instead of touching the global directly. The exact world is part
 *  of mutation authorization and effect scheduling behavior, so it remains
 *  available in every build. */
SEINARTSCOREENTITY_API bool SeinIsInSimContext();
SEINARTSCOREENTITY_API bool SeinIsInSimContext(
	const USeinWorldSubsystem* World);

/** Core-owned RAII scope that restores the exact previous world when nested.
 *  Construction is private: only the fixed-tick dispatcher can mint runtime
 *  mutation authority. */
struct SEINARTSCOREENTITY_API FSeinSimContextScope
{
	~FSeinSimContextScope();
	FSeinSimContextScope(const FSeinSimContextScope&) = delete;
	FSeinSimContextScope& operator=(const FSeinSimContextScope&) = delete;

private:
	explicit FSeinSimContextScope(const USeinWorldSubsystem& World);
	const USeinWorldSubsystem* PreviousWorld = nullptr;
	friend class USeinWorldSubsystem;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FSeinSimContextTestAccess;
#endif
};

/** Place at the top of TickSystems() and any sim-entry function. Functional
 *  context tracking remains enabled in Shipping. */
#define SEIN_SIM_SCOPE(World) FSeinSimContextScope _SeinSimScope(World);

/** Check without asserting (for conditional scheduling logic). */
#define SEIN_IS_SIM_CONTEXT() (SeinIsInSimContext())
#define SEIN_IS_SIM_CONTEXT_FOR(World) (SeinIsInSimContext(World))

#if !UE_BUILD_SHIPPING

/** Assert that we are inside a sim tick. Place on sim-only utility functions. */
#define SEIN_CHECK_SIM() checkf(SeinIsInSimContext(), TEXT("Called sim function outside sim context: %s"), ANSI_TO_TCHAR(__FUNCTION__))

/** Assert that we are NOT inside a sim tick. Place on render-only functions. */
#define SEIN_CHECK_NOT_SIM() checkf(!SeinIsInSimContext(), TEXT("Called render function inside sim context: %s"), ANSI_TO_TCHAR(__FUNCTION__))

#else

// Strip diagnostic assertions in Shipping, not functional context tracking.
#define SEIN_CHECK_SIM()
#define SEIN_CHECK_NOT_SIM()

#endif
