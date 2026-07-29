/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementCanonicalStateProvider.h
 * @brief   Module-owned registration for persistent movement policy state.
 */

#pragma once

#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Serialization/SeinMovementStateCoverageInternal.h"

FSeinCanonicalStateRegistrationHandle
SeinRegisterMovementCanonicalStateProvider(
	const FSeinMovementStateCoverageSnapshot& Coverage,
	FString& OutError);
