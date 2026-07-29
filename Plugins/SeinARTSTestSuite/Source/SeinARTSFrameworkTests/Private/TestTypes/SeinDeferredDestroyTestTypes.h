/**
 * Test-only canonical state used by stateless deferred-destroy producers.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinDeferredDestroyTestTypes.generated.h"

USTRUCT(meta = (SeinDeterministic))
struct FSeinDeferredDestroyTestComponent
{
	GENERATED_BODY()

	UPROPERTY()
	bool bArmed = false;
};
