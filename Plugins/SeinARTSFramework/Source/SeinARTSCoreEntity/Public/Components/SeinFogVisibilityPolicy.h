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
 * Policy controlling when an entity's render actor is visible to an
 * observer in fog-of-war. Four levels of visibility, from "only when
 * actively spotted," through "stays visible once the entity itself has
 * been seen," "stays visible once the entity's tile has been explored,"
 * to "always rendered regardless of fog."
 *
 * Read by `USeinFogOfWar::IsEntityVisibleToObserver` — the single gate the
 * render visibility subsystem, minimap, and cover queries all share.
 */
UENUM(BlueprintType)
enum class ESeinFogVisibilityPolicy : uint8
{
	/** Default — visible only while a vision source is actively spotting
	 *  the entity on a matching emission layer. Hidden in unexplored fog
	 *  AND in previously-explored-but-currently-unseen fog. Standard
	 *  treatment for enemy units (canonical RTS fog behavior). */
	VisionLayersOnly       UMETA(DisplayName = "Vision Layers Only"),

	/** Hidden until a vision source has actually spotted THIS ENTITY at
	 *  least once; thereafter remains visible as a "ghost" even when current
	 *  vision is lost. The reveal is latched per-entity per-observer the
	 *  first tick the entity's footprint falls under live vision — so a thing
	 *  that appears in already-explored-but-currently-unseen fog (e.g. a
	 *  building constructed in scouted territory you've since lost sight of)
	 *  stays hidden until you genuinely see it. The canonical "you remember
	 *  what you've seen" remembered-terrain reveal. Contrast
	 *  VisibleOnceExplored, which reveals on terrain scouting alone. */
	VisibleOnceSeen        UMETA(DisplayName = "Visible Once Seen"),

	/** Visible once the player has explored the CELL the entity sits on, even
	 *  if the entity itself was never under live vision — the sticky per-cell
	 *  Explored bit drives the reveal, so anything occupying an already-
	 *  scouted tile shows immediately (including units / buildings that
	 *  arrived after you lost sight of the tile). Coarser, cheaper "intel
	 *  from terrain" reveal; prefer VisibleOnceSeen when a thing must be
	 *  actually seen before it becomes known. */
	VisibleOnceExplored    UMETA(DisplayName = "Visible Once Explored"),

	/** Bypasses the fog hide check entirely — always rendered. Use for
	 *  environmental entity classes the player should always see (cover
	 *  providers, persistent destructibles, self-occluding effects whose
	 *  stamp blocks vision past them but whose actor must stay rendered). */
	AlwaysVisible          UMETA(DisplayName = "Always Visible"),
};
