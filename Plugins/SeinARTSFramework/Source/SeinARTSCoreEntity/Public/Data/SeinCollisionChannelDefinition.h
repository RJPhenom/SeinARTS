/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionChannelDefinition.h
 * @brief   Plugin-settings row defining one designer-configurable collision
 *          channel (object type). The collision-channel registry is the
 *          extensible, project-wide set of object types a collider can BE
 *          (its Object Type) and can respond to (its response matrix).
 *
 *          Analogous to Unreal's object channels (WorldStatic, Pawn, Vehicle,
 *          …), but fully data-driven: designers add/rename channels in
 *          Project Settings > Plugins > SeinARTS > Collision Channels and every
 *          collider's Object Type picker + response matrix updates to match.
 *
 *          Authored collider data references channels by NAME (stable across
 *          reordering). The runtime resolves names → a compact per-session
 *          index/bit layout when colliders register; because every peer reads
 *          the same settings, that layout is identical across the lockstep
 *          session. Renaming a channel is safe; reordering is cosmetic for
 *          authored data but changes the runtime index order — keep it stable
 *          across a shipped build to avoid churn in replays/saves.
 */

#pragma once

#include "CoreMinimal.h"
#include "Collision/SeinCollisionTypes.h"
#include "SeinCollisionChannelDefinition.generated.h"

/**
 * One designer-configurable collision channel (object type).
 *
 *  - Name            the stable identifier authored colliders reference (their
 *                    Object Type, and their per-channel response overrides).
 *                    Use project nouns: "StaticEntity", "DynamicEntity", etc.
 *  - DefaultResponse the response every collider gives this channel UNLESS it
 *                    authors an override. UE convention: most channels default
 *                    to Block; channels like projectiles often default to
 *                    Overlap or Ignore for things they should pass through.
 *  - DebugColor      tint used by the entity-bridge visualizer to color a
 *                    collider's wireframe by its Object Type.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinCollisionChannelDefinition
{
	GENERATED_BODY()

	/** Stable channel identifier. Referenced by colliders' Object Type and
	 *  response overrides — rename-safe, but keep unique within the registry. */
	UPROPERTY(Config, EditAnywhere, Category = "SeinARTS|Collision|Channel")
	FName Name = NAME_None;

	/** Response a collider gives entities of this channel when it has authored
	 *  no explicit override for it. */
	UPROPERTY(Config, EditAnywhere, Category = "SeinARTS|Collision|Channel")
	ESeinCollisionResponse DefaultResponse = ESeinCollisionResponse::Block;

	/** Wireframe tint for colliders whose Object Type is this channel, in the
	 *  entity-bridge visualizer. */
	UPROPERTY(Config, EditAnywhere, Category = "SeinARTS|Collision|Channel")
	FLinearColor DebugColor = FLinearColor::White;
};
