/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinCollisionTypes.h
 * @brief:   Shared vocabulary for the collision layer: the per-channel response
 *           enum, the mobility enum, and the per-collider response container.
 *
 *           This is the deterministic, sim-side collision model — modeled on
 *           Unreal's collision-response UX but decoupled from UE's physics /
 *           trace machinery (we have neither in the sim). It is INDEPENDENT of
 *           navigation: a nav blocker need not be a collider and a collider need
 *           not block nav. Collision is purely extent-vs-extent.
 *
 *           Channels themselves are designer-defined in plugin settings
 *           (FSeinCollisionChannelDefinition). Authored data here references
 *           channels by NAME, not bit index, so reordering the registry never
 *           silently rewires an already-authored collider's responses.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinCollisionTypes.generated.h"

/**
 * How one collider responds to another on a given channel. The numeric order is
 * load-bearing: a pairwise interaction resolves to the WEAKER of the two
 * parties' responses (i.e. `min`), so:
 *   - Block  requires BOTH sides to Block  → the pair separates.
 *   - Overlap if the weaker side is Overlap → an overlap event fires, no push.
 *   - Ignore if either side Ignores         → the pair is invisible to each other.
 * Keep Ignore < Overlap < Block (0 < 1 < 2) — `ResolvePairResponse` relies on it.
 */
UENUM(BlueprintType)
enum class ESeinCollisionResponse : uint8
{
	Ignore  UMETA(DisplayName = "Ignore"),
	Overlap UMETA(DisplayName = "Overlap"),
	Block   UMETA(DisplayName = "Block")
};

/**
 * Whether — and how — a collider's transform changes at runtime. Three modes
 * mirroring Unreal's Static / Stationary / Movable, but here the meaning is
 * about COLLISION MASS + broadphase tiering, not lighting:
 *
 *   Static     — transform NEVER changes after spawn. Infinite mass (never
 *                displaced) AND cached in the broadphase's persistent tier, so
 *                static geometry costs nothing per tick and Static↔Static pairs
 *                are skipped. Walls, buildings, fixed scenery. Destructible-but-
 *                fixed counts as Static (removed from the broadphase on death,
 *                never moved).
 *   Stationary — CAN move, but is NOT pushable. For bodies whose motion is owned
 *                by script/abilities — doors, drawbridges, moving platforms,
 *                shifting cover. Infinite mass like Static (a movable that hits
 *                it is ejected; it is never displaced), but its position is
 *                rebuilt into the broadphase every tick so its motion is tracked.
 *                It never initiates a push and isn't itself constrained by Static
 *                / other Stationary colliders — its path is the driver's job.
 *   Movable    — transform changes via movement; finite mass (radius²);
 *                participates in mass-weighted separation. Units, pushable props.
 *
 * NOT inferred from a movement component — a thing can change location without
 * one (a scripted platform). Author it explicitly. Implementation note: the
 * mass/initiation checks key off `== Movable` and only the broadphase-cache
 * decision keys off `== Static`, so Stationary needs no special-casing — it
 * falls out as "infinite mass, dynamic broadphase tier."
 */
UENUM(BlueprintType)
enum class ESeinCollisionMobility : uint8
{
	Static     UMETA(DisplayName = "Static"),
	Stationary UMETA(DisplayName = "Stationary"),
	Movable    UMETA(DisplayName = "Movable")
};

/**
 * A collider's object type — the single collision channel it belongs to, by
 * name (matching a `USeinARTSCoreSettings::CollisionChannels` entry). A thin
 * FName wrapper purely so the details panel can present it as a DROPDOWN of the
 * registered channel names (`FSeinCollisionObjectTypeDetails`) rather than a
 * free-text box; the sim reads `Channel` directly. None = no object type → the
 * collider is inert even with collision enabled.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCollisionObjectType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Collision")
	FName Channel = FName(TEXT("Default"));
};

FORCEINLINE uint32 GetTypeHash(const FSeinCollisionObjectType& ObjectType)
{
	return GetTypeHash(ObjectType.Channel);
}

/**
 * One per-channel response override. Keyed by channel NAME (matches a
 * `FSeinCollisionChannelDefinition::Name` in plugin settings) so it survives
 * registry reordering. Absence of an override for a channel means "use that
 * channel's DefaultResponse from settings."
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCollisionResponseOverride
{
	GENERATED_BODY()

	/** Target channel name. Matches a channel declared in
	 *  Project Settings > Plugins > SeinARTS > Collision Channels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Collision")
	FName Channel = NAME_None;

	/** This collider's response to entities whose Object Type is `Channel`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Collision")
	ESeinCollisionResponse Response = ESeinCollisionResponse::Block;
};

FORCEINLINE uint32 GetTypeHash(const FSeinCollisionResponseOverride& Override)
{
	uint32 Hash = GetTypeHash(Override.Channel);
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Override.Response)));
	return Hash;
}

/**
 * Sparse set of a collider's per-channel response overrides. Empty = every
 * channel uses its DefaultResponse. The editor matrix writes an entry only when
 * a designer changes a cell away from the channel default (and removes it when
 * set back), mirroring Unreal's FCollisionResponse authoring model. Runtime
 * code resolves the flat per-channel response once at collider registration.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCollisionResponseContainer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Collision",
		meta = (TitleProperty = "Channel"))
	TArray<FSeinCollisionResponseOverride> Overrides;

	/** Resolve this collider's response to `Channel`: the authored override if
	 *  present, otherwise `DefaultResponse` (the channel's registry default). */
	ESeinCollisionResponse GetResponseForChannel(FName Channel, ESeinCollisionResponse DefaultResponse) const
	{
		for (const FSeinCollisionResponseOverride& Override : Overrides)
		{
			if (Override.Channel == Channel)
			{
				return Override.Response;
			}
		}
		return DefaultResponse;
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinCollisionResponseContainer& Container)
{
	uint32 Hash = GetTypeHash(Container.Overrides.Num());
	for (const FSeinCollisionResponseOverride& Override : Container.Overrides)
	{
		Hash = HashCombine(Hash, GetTypeHash(Override));
	}
	return Hash;
}

/**
 * Resolve the effective interaction between two colliders given each side's
 * response to the other's object-type channel. The weaker response wins (see
 * ESeinCollisionResponse ordering): Block only if both Block; Overlap if the
 * lesser is Overlap; Ignore if either Ignores.
 */
FORCEINLINE ESeinCollisionResponse ResolvePairResponse(ESeinCollisionResponse AToB, ESeinCollisionResponse BToA)
{
	return (static_cast<uint8>(AToB) < static_cast<uint8>(BToA)) ? AToB : BToA;
}
