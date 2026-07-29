/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementCanonicalStateTestAccess.h
 * @brief   Non-shipping hostile-payload access for the disabled test suite.
 */

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

struct FSeinCanonicalStateContributorRecord;

/**
 * Re-encode a valid Movement contributor record with a hostile exact class
 * path and a matching leaf digest, so restore tests reach provider staging.
 */
SEINARTSMOVEMENT_API bool
SeinReplaceFirstMovementClassPathForTest(
	FSeinCanonicalStateContributorRecord& Record,
	const FString& ReplacementClassPath,
	FString& OutError);

#endif
