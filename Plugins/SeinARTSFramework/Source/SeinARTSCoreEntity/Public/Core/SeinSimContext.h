/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimContext.h
 * @brief   Determinism enforcement macros for sim/render boundary.
 */

#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

/** Exported accessors for the thread-local sim-context flag. thread_local
 *  variables can't carry dllexport on MSVC, so external modules call these
 *  functions instead of touching the global directly. */
SEINARTSCOREENTITY_API bool SeinIsInSimContext();
SEINARTSCOREENTITY_API void SeinSetSimContext(bool bInSim);

/** RAII scope guard that sets sim context flag on construction, clears on destruction. */
struct SEINARTSCOREENTITY_API FSeinSimContextScope
{
	FSeinSimContextScope() { SeinSetSimContext(true); }
	~FSeinSimContextScope() { SeinSetSimContext(false); }
};

/** Place at the top of TickSystems() and any sim-entry function. */
#define SEIN_SIM_SCOPE FSeinSimContextScope _SeinSimScope;

/** Assert that we are inside a sim tick. Place on sim-only utility functions. */
#define SEIN_CHECK_SIM() checkf(SeinIsInSimContext(), TEXT("Called sim function outside sim context: %s"), ANSI_TO_TCHAR(__FUNCTION__))

/** Assert that we are NOT inside a sim tick. Place on render-only functions. */
#define SEIN_CHECK_NOT_SIM() checkf(!SeinIsInSimContext(), TEXT("Called render function inside sim context: %s"), ANSI_TO_TCHAR(__FUNCTION__))

/** Check without asserting (for conditional logic). */
#define SEIN_IS_SIM_CONTEXT() (SeinIsInSimContext())

#else

// Strip all checks in shipping builds
#define SEIN_SIM_SCOPE
#define SEIN_CHECK_SIM()
#define SEIN_CHECK_NOT_SIM()
#define SEIN_IS_SIM_CONTEXT() (true)

#endif
