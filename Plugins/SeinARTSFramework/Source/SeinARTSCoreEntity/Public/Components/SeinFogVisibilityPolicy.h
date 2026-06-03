/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinFogVisibilityPolicy.h
 * @brief:   Standalone enum for fog-of-war post-reveal visibility behavior.
 *           Extracted from the pre-refactor archetype-definition class so it can be authored
 *           in independent component structs (FSeinExtentsComponent primarily)
 *           without pulling that header along.
 *
 *           This enum controls what happens to an entity's visibility AFTER
 *           it has been revealed — orthogonal to whether the entity blocks
 *           vision (`bBlocksFogOfWar`) or bakes into the static FoW grid
 *           (`bBakesIntoFogOfWar`). All three concepts are independent.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinFogVisibilityPolicy.generated.h"

/**
 * Policy controlling when an entity's render actor is visible to the
 * local observer in fog-of-war. Three escalating levels of visibility,
 * from "only when actively spotted" through "stays visible once scouted"
 * to "always rendered regardless of fog."
 *
 * Read by `USeinFogOfWarVisibilitySubsystem::Tick` each render frame.
 */
UENUM(BlueprintType)
enum class ESeinFogVisibilityPolicy : uint8
{
	/** Default — visible only while a vision source is actively spotting
	 *  the entity on a matching emission layer. Hidden in unexplored fog
	 *  AND in previously-explored-but-currently-unseen fog. Standard
	 *  treatment for enemy units (canonical RTS fog behavior). */
	VisionLayersOnly       UMETA(DisplayName = "Vision Layers Only"),

	/** Hidden until the player has explored the entity's location at
	 *  least once; thereafter remains visible as a "ghost" even when
	 *  current vision is lost. The Explored bit is sticky, so the reveal
	 *  persists for the rest of the match. Standard treatment for enemy
	 *  buildings — once scouted, you remember where they are. */
	VisibleOnceExplored    UMETA(DisplayName = "Visible Once Explored"),

	/** Bypasses the fog hide check entirely — always rendered. Use for
	 *  environmental entity classes the player should always see (cover
	 *  providers, persistent destructibles, self-occluding effects whose
	 *  stamp blocks vision past them but whose actor must stay rendered). */
	AlwaysVisible          UMETA(DisplayName = "Always Visible"),
};
