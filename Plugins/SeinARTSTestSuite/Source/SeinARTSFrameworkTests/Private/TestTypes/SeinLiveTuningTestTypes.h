#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "SeinLiveTuningTestTypes.generated.h"

/** One authored slot that also carries runtime-only (non-Edit) state, the
 *  shape a designer's weapon/ability slot array takes. Live tuning must patch
 *  the authored field without clobbering the runtime fields beside it. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinLiveTuningTestSlot
{
	GENERATED_BODY()

	/** Authored. */
	UPROPERTY(EditAnywhere, Category = "Test")
	FFixedPoint Range = FFixedPoint::Zero;

	/** Runtime-only (no CPF_Edit). */
	UPROPERTY()
	FFixedPoint CooldownRemaining = FFixedPoint::Zero;

	UPROPERTY()
	FFixedPoint ReloadRemaining = FFixedPoint::Zero;

	UPROPERTY()
	int32 MagazineRemaining = 0;
};

/** Designer-style component: an authored array of slots plus a runtime seed
 *  latch at the top level. */
USTRUCT(meta = (SeinDeterministic))
struct FSeinLiveTuningTestComponent : public FSeinComponent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Test")
	TArray<FSeinLiveTuningTestSlot> Slots;

	/** Runtime-only. */
	UPROPERTY()
	bool bRuntimeSeeded = false;
};
