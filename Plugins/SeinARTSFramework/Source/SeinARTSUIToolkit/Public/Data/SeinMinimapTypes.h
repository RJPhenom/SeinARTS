/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMinimapTypes.h
 * @brief   Value types for the minimap: display shape, per-blip descriptor, and the
 *          designer-facing style. The view-model produces relation/size-tagged blips
 *          (style-free); the widget maps relation/size → color/radius via its FSeinMinimapStyle.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Data/SeinUITypes.h"     // ESeinRelation
#include "SeinMinimapTypes.generated.h"

class UTexture2D;

/** Overall minimap display shape (clip mask). Circle is recommended when the map
 *  rotates with the camera (a rotating square leaves empty corner wedges). */
UENUM(BlueprintType)
enum class ESeinMinimapShape : uint8
{
	Square,
	Circle
};

/** Coarse unit-size class → drives blip radius. The view-model tags each blip; the
 *  widget resolves the actual pixel radius from FSeinMinimapStyle. */
UENUM(BlueprintType)
enum class ESeinMinimapBlipSize : uint8
{
	Small,
	Medium,
	Large
};

/**
 * One unit marker, produced by USeinMinimapViewModel each refresh and consumed by the
 * widget's NativePaint. Position is in NORTH-UP normalized map space [0,1]^2 (the widget
 * applies the camera-yaw rotation at paint time, so the data stays orientation-agnostic).
 */
USTRUCT(BlueprintType)
struct FSeinMinimapBlip
{
	GENERATED_BODY()

	/** North-up normalized position in [0,1]^2 (U grows with world +X, V with world +Y). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	FVector2D NormalizedPos = FVector2D(0.5, 0.5);

	/** Relation to the local player — the widget colors the blip from this. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	ESeinRelation Relation = ESeinRelation::Neutral;

	/** Coarse size class — the widget resolves a pixel radius from this. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	ESeinMinimapBlipSize SizeClass = ESeinMinimapBlipSize::Medium;

	/** True if this entity is in the local player's current selection. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	bool bSelected = false;

	/** The entity this blip represents (for future click-to-select on the minimap). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	FSeinEntityHandle Entity;

	/** Per-type minimap sprite, copied from the entity's FSeinIdentityPayload::MinimapIcon.
	 *  Null → the widget should draw its default dot. Typically tinted by Relation. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	TObjectPtr<UTexture2D> Icon = nullptr;
};

/**
 * Designer-facing minimap styling. Authored on USeinMinimapWidget (or a Blueprint
 * subclass default) so a minimap can be fully re-skinned without C++.
 */
USTRUCT(BlueprintType)
struct FSeinMinimapStyle
{
	GENERATED_BODY()

	// --- Blip colors by relation ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap|Blips")
	FLinearColor FriendlyColor = FLinearColor(0.25f, 0.85f, 0.30f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap|Blips")
	FLinearColor EnemyColor = FLinearColor(0.90f, 0.20f, 0.18f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap|Blips")
	FLinearColor NeutralColor = FLinearColor(0.85f, 0.80f, 0.30f, 1.0f);

	/** Outline drawn around selected blips. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap|Blips")
	FLinearColor SelectedOutlineColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	// --- Blip radii (pixels, before widget DPI scale) by size class ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap|Blips", meta = (ClampMin = "0.5"))
	float BlipRadiusSmall = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap|Blips", meta = (ClampMin = "0.5"))
	float BlipRadiusMedium = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap|Blips", meta = (ClampMin = "0.5"))
	float BlipRadiusLarge = 4.0f;

	// --- Camera viewport box ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap|Viewport")
	FLinearColor ViewportBoxColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap|Viewport", meta = (ClampMin = "0.5"))
	float ViewportBoxThickness = 1.5f;
};
