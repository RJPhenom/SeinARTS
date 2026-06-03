/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinFogVisibilityComponent.h
 * @brief:   Sim-side storage struct for fog-of-war visibility (policy +
 *           emission mask). The authoring surface is `USeinEntityComponent`'s
 *           top-level `FogVisibilityPolicy` + `FogVisibilityLayerMask`
 *           fields — those are universal attrs every entity has, not an
 *           opt-in. The bridge's `InjectAuthoredComponents` auto-populates
 *           this struct in sim component storage at spawn.
 *
 *           Marked `SeinSubData` so it does NOT appear in the entity
 *           bridge's ComponentData picker. Designers never add this struct
 *           manually; the bridge fields carry the authoring weight.
 *
 *           Why a separate struct at all? Sim/render separation rule —
 *           the simulation never touches `AActor*`, so the FoW system
 *           reads visibility off a sim-side component, not off the
 *           render-side `USeinEntityComponent`. This struct is that
 *           sim-side mirror.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "Components/SeinFogVisibilityPolicy.h"
#include "SeinFogVisibilityComponent.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinFogVisibilityComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Persistence policy for what happens AFTER this entity is revealed:
	 *    VisionLayersOnly (default) — visible only while currently spotted.
	 *      Standard for enemy units.
	 *    VisibleOnceExplored        — visible after the player has scouted
	 *      the entity's location at least once; stays as a "ghost" after.
	 *      Standard for enemy buildings.
	 *    AlwaysVisible              — bypasses the fog hide check entirely.
	 *      Cover providers, persistent destructibles, self-occluding effects
	 *      (their stamp blocks vision but the actor still renders). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|FogOfWar")
	ESeinFogVisibilityPolicy FogVisibilityPolicy = ESeinFogVisibilityPolicy::VisionLayersOnly;

	/** Which observer fog-of-war layer bits actually see this entity. An
	 *  observer's vision query on a given layer bit only "spots" this entity
	 *  if its mask is non-zero in that bit. Independent of FogVisibilityPolicy
	 *  (policy = persistence after reveal; this = who can reveal in the
	 *  first place).
	 *
	 *  Default 0xFE (all bits set except Explored / E, which is sticky-
	 *  reveal and shouldn't grant initial spotting). Designers narrow this
	 *  for stealth / camo units (e.g. set to N0 only; only observers
	 *  stamping the N0 layer can see it). The runtime mutation BPFLs
	 *  (`SeinSetEntityEmissionMask` / `SeinAddEntityEmissionLayers` /
	 *  `SeinRemoveEntityEmissionLayers`) flip these bits at ability time
	 *  for cloak / detect mechanics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|FogOfWar",
		meta = (Bitmask, BitmaskEnum = "/Script/SeinARTSFogOfWar.ESeinFogOfWarLayerBit"))
	uint8 FogVisibilityLayerMask = 0xFE;
};

FORCEINLINE uint32 GetTypeHash(const FSeinFogVisibilityComponent& Component)
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(Component.FogVisibilityPolicy));
	Hash = HashCombine(Hash, GetTypeHash(Component.FogVisibilityLayerMask));
	return Hash;
}
