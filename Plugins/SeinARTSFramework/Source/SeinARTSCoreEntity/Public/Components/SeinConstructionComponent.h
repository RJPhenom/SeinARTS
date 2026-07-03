/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinConstructionComponent.h
 * @brief   Construction-over-time component for placed buildings (Pattern B).
 *          The placement ability spawns a building with this
 *          component attached; a builder unit's BA_Construct ability ticks
 *          Progress each sim tick. When Progress >= TimeToCompletion, the building
 *          transitions to operational via the optional CompletionEffect.
 *
 *          Lifecycle:
 *            1. BA_PlaceX spawns building with FSeinConstructionComponent (Progress=0)
 *               and SeinARTS.State.UnderConstruction tag granted.
 *            2. Builder runs BA_Construct, calls SeinAddConstructionProgress
 *               each sim tick. When threshold crosses, BPFL auto-finishes:
 *                 a. Applies CompletionEffect (if set) to the building.
 *                 b. Removes the FSeinConstructionComponent component.
 *                 c. Ungrants SeinARTS.State.UnderConstruction tag.
 *            3. Building's normal abilities — gated via BlockedTags
 *               on SeinARTS.State.UnderConstruction — become invokable.
 *
 *          Multiple builders ticking the same target stack — each adds its
 *          own DeltaTime-scaled progress per tick. Designer's BA_Construct
 *          can apply a per-worker speed multiplier (e.g. 1.0 for first
 *          builder, 0.5 for each additional to model diminishing returns).
 *
 *          The CompletionEffect is the seam for game-specific transitions:
 *          mesh swap (foundation → finished building), grant operational
 *          ability set, fire VFX/audio, register with capture-point system,
 *          etc. Effects are designer-authored USeinEffect Blueprint subclasses.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "Templates/SubclassOf.h"
#include "Types/FixedPoint.h"
#include "SeinConstructionComponent.generated.h"

class USeinEffect;

USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinConstructionComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Total construction time in sim-seconds. Authored on the building BP's
	 *  CDO. Snapshotted on attach — modifiers that affect mid-construction
	 *  build speed should adjust the per-tick Progress increment in
	 *  BA_Construct, not mutate this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Construction",
		meta = (ClampMin = "0.0"))
	FFixedPoint TimeToCompletion = FFixedPoint::FromInt(10);

	/** Current progress (sim-seconds). Advances from 0 toward TimeToCompletion via
	 *  USeinConstructionBPFL::SeinAddConstructionProgress. When Progress >=
	 *  TimeToCompletion, BPFL auto-applies CompletionEffect + removes this component. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Construction")
	FFixedPoint Progress = FFixedPoint::Zero;

	/** USeinEffect class applied to the building entity when construction
	 *  completes. Empty = just remove the component + ungrant the
	 *  UnderConstruction tag (the building's normal abilities become invokable
	 *  on their own).
	 *
	 *  Typical authoring: a BP-authored USeinEffect that on Apply swaps the
	 *  building's render mesh from "foundation" to "complete," fires a
	 *  visual event for the construction-finish VFX, optionally grants the
	 *  building's operational ability set if it wasn't pre-granted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Construction")
	TSubclassOf<USeinEffect> CompletionEffect;
};
