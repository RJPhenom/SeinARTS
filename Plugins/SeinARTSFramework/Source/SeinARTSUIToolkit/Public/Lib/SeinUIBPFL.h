/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinUIBPFL.h
 * @brief   Blueprint Function Library for UI utility functions — entity display
 *          helpers, conversion utilities, screen projection, minimap math,
 *          and action slot data builders.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Data/SeinUITypes.h"
#include "Data/SeinActionSlotData.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "SeinUIBPFL.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;
class APlayerController;
class UImage;

UCLASS(meta = (DisplayName = "SeinARTS UI Library"))
class SEINARTSUITOOLKIT_API USeinUIBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ==================== Entity Display Helpers ====================

	/** Get an entity's display name from its FSeinIdentityComponent. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Display Name"))
	static FText SeinGetEntityDisplayName(const UObject* WorldContextObject, FSeinEntityHandle Handle);

	/** Get an entity's icon texture from its FSeinIdentityComponent. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Icon"))
	static UTexture2D* SeinGetEntityIcon(const UObject* WorldContextObject, FSeinEntityHandle Handle);

	/** Get an entity's portrait texture from its FSeinIdentityComponent. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Portrait"))
	static UTexture2D* SeinGetEntityPortrait(const UObject* WorldContextObject, FSeinEntityHandle Handle);

	/** Get an entity's identity tag (from its FSeinIdentityComponent). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Identity Tag"))
	static FGameplayTag SeinGetEntityIdentityTag(const UObject* WorldContextObject, FSeinEntityHandle Handle);

	/** Get the relationship between an entity and a player (Friendly/Enemy/Neutral). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Relation"))
	static ESeinRelation SeinGetEntityRelation(const UObject* WorldContextObject, FSeinEntityHandle Handle, FSeinPlayerID PlayerID);

	// ==================== Conversion Helpers ====================

	/** Convert a FFixedPoint value to float for display. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Math", meta = (DisplayName = "Fixed To Float"))
	static float SeinFixedToFloat(FFixedPoint Value);

	/** Convert a FFixedVector to FVector for display. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Math", meta = (DisplayName = "Fixed Vector To Vector"))
	static FVector SeinFixedVectorToVector(const FFixedVector& Value);

	/** Format a tag-keyed resource cost map into a human-readable string
	 *  (e.g., "100 Manpower, 50 Fuel"). Resource tag leaf name is used as the label. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Format", meta = (DisplayName = "Format Resource Cost", Categories = "SeinARTS.Resource"))
	static FText SeinFormatResourceCost(const TMap<FGameplayTag, float>& Cost);

	// ==================== Screen Projection ====================

	/**
	 * Project a world position to screen coordinates.
	 * @param WorldPos - World position to project
	 * @param OutScreenPos - Screen coordinates (output)
	 * @return True if the position is in front of the camera
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Projection", meta = (WorldContext = "WorldContextObject", DisplayName = "World To Screen"))
	static bool SeinWorldToScreen(const UObject* WorldContextObject, APlayerController* PlayerController, FVector WorldPos, FVector2D& OutScreenPos);

	/**
	 * Project a screen position to world space (ray-plane intersection at GroundZ).
	 * @param ScreenPos - Screen coordinates
	 * @param GroundZ - Z height of the ground plane
	 * @param OutWorldPos - World position (output)
	 * @return True if intersection was found
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Projection", meta = (WorldContext = "WorldContextObject", DisplayName = "Screen To World"))
	static bool SeinScreenToWorld(const UObject* WorldContextObject, APlayerController* PlayerController, FVector2D ScreenPos, float GroundZ, FVector& OutWorldPos);

	// ==================== Minimap Math ====================

	/** Map a world XY position to minimap UV coordinates (0-1). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Minimap", meta = (DisplayName = "World To Minimap"))
	static FVector2D SeinWorldToMinimap(FVector WorldPos, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax);

	/** Map minimap UV coordinates (0-1) back to world XY position. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Minimap", meta = (DisplayName = "Minimap To World"))
	static FVector SeinMinimapToWorld(FVector2D MinimapUV, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax, float GroundZ = 0.0f);

	/**
	 * Compute the camera frustum's 4 ground-plane intersection corners in minimap UV space.
	 * Used to draw the camera view trapezoid on the minimap.
	 * @param PlayerController - The player controller whose camera to use
	 * @param WorldBoundsMin - XY world bounds min
	 * @param WorldBoundsMax - XY world bounds max
	 * @param GroundZ - Z height of the ground plane
	 * @return Array of 4 FVector2D in minimap UV space (top-left, top-right, bottom-right, bottom-left)
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Minimap", meta = (DisplayName = "Get Camera Frustum Corners"))
	static TArray<FVector2D> SeinGetCameraFrustumCorners(APlayerController* PlayerController, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax, float GroundZ = 0.0f);

	/**
	 * Resolve the minimap background texture for the current level. A per-level designer
	 * override authored on an ASeinLevelVolume (MinimapOverrideTexture) wins; otherwise
	 * the top-down texture baked into the level-data substrate. Null if neither exists
	 * (no override + no bake). The minimap view-model uses this; designers can also call
	 * it directly to set an Image brush.
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Minimap", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Minimap Texture For Level"))
	static UTexture2D* SeinGetMinimapTextureForLevel(const UObject* WorldContextObject);

	/**
	 * The minimap's display rotation in degrees. When bRotateWithCamera is true this is
	 * -cameraYaw + RotationOffsetDeg (so the camera's forward points "up", Company-of-Heroes
	 * style); otherwise just RotationOffsetDeg. Apply this as the render-transform angle on
	 * the panel that holds the minimap visuals, AND pass it to Minimap Local To World so
	 * clicks un-rotate consistently. One source of truth for the rotation.
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Minimap", meta = (DisplayName = "Get Minimap Rotation Degrees"))
	static float SeinGetMinimapRotationDegrees(APlayerController* PlayerController, bool bRotateWithCamera, float RotationOffsetDeg = 0.0f);

	/**
	 * Convert a minimap click (widget-local pixel position on the UN-rotated minimap area)
	 * to a world point. Inverts MapRotationDeg, maps to north-up UV, clips to the shape,
	 * and projects onto the ground plane at GroundZ.
	 * @param LocalPos       - Click position in the minimap widget's local space.
	 * @param WidgetSize     - The minimap widget's local size (Geometry.GetLocalSize()).
	 * @param MapRotationDeg - The same value Get Minimap Rotation Degrees returned.
	 * @param bCircleClip    - True for a circular minimap (rejects clicks outside the inscribed circle).
	 * @param OutWorld       - The resolved world point.
	 * @return True if the click was inside the map (OutWorld valid); false otherwise.
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Minimap", meta = (DisplayName = "Minimap Local To World"))
	static bool SeinMinimapLocalToWorld(FVector2D LocalPos, FVector2D WidgetSize, FVector2D WorldBoundsMin,
		FVector2D WorldBoundsMax, float GroundZ, float MapRotationDeg, bool bCircleClip, FVector& OutWorld);

	/**
	 * Draw the camera's true ground-plane view footprint (a trapezoid — it widens/shrinks
	 * faithfully with camera tilt, since it's the deprojection of the four screen corners
	 * onto the ground) into a render target, in NORTH-UP minimap space. Show the render
	 * target on an Image inside the SAME rotated panel as the rest of the minimap visuals,
	 * so it ends up upright and aligned with the blips. Call this each frame from the
	 * minimap widget's tick — only a widget that actually shows a minimap pays for it.
	 * Clears the target each call. No-op if fewer than 4 corners hit the ground (camera at
	 * the horizon).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Minimap", meta = (WorldContext = "WorldContextObject", DisplayName = "Draw Camera Viewport To Render Target"))
	static void SeinDrawCameraViewportToRenderTarget(const UObject* WorldContextObject, APlayerController* PlayerController,
		UTextureRenderTarget2D* RenderTarget, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax, float GroundZ,
		FLinearColor LineColor, float LineThickness = 2.0f);

	/**
	 * Set an Image widget's brush resource to any texture-like UObject. Works for a
	 * UTexture2D (background / fog) AND a UTextureRenderTarget2D (the viewport-box RT) —
	 * the stock "Set Brush from Texture" node only accepts Texture2D and rejects render
	 * targets. Preserves the Image's other brush settings (tint, draw-as, size).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Minimap", meta = (DisplayName = "Set Image Brush Resource"))
	static void SeinSetImageBrushResource(UImage* Image, UObject* ResourceObject);

	// ==================== Action Slot Builders ====================

	/** Build action slot data for a single ability on an entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Action", meta = (WorldContext = "WorldContextObject", DisplayName = "Build Ability Slot Data"))
	static FSeinActionSlotData SeinBuildAbilitySlotData(const UObject* WorldContextObject, FSeinEntityHandle Entity, FGameplayTag AbilityTag, int32 SlotIndex);

	/** Build action slot data for all abilities on an entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Action", meta = (WorldContext = "WorldContextObject", DisplayName = "Build All Ability Slot Data"))
	static TArray<FSeinActionSlotData> SeinBuildAllAbilitySlotData(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	// SeinBuildProductionSlotData removed (refactored 2026-05-05): production is
	// now unified into the ability surface. Use SeinBuildAllAbilitySlotData —
	// production buttons sit in the same slot row as targetable abilities, with
	// the same click semantics. The triggering ability (e.g. SA_TrainRifleman)
	// calls Self.EnqueueProduction(<class>) in its OnActivate; the producer's
	// the producible's CDO supplies BuildTime + RefundPolicy + research metadata.

	// ==================== World-Space Widget Helpers ====================

	/**
	 * Project an entity's position to screen space with a vertical offset.
	 * @param Handle - Entity to project
	 * @param VerticalWorldOffset - World-space offset above entity (in cm)
	 * @param OutScreenPos - Screen position (output)
	 * @return True if the entity is in front of the camera
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Projection", meta = (WorldContext = "WorldContextObject", DisplayName = "Project Entity To Screen"))
	static bool SeinProjectEntityToScreen(const UObject* WorldContextObject, APlayerController* PlayerController, FSeinEntityHandle Handle, float VerticalWorldOffset, FVector2D& OutScreenPos);

	/** Check if an entity is currently visible on screen (with margin). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Projection", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Entity On Screen"))
	static bool SeinIsEntityOnScreen(const UObject* WorldContextObject, APlayerController* PlayerController, FSeinEntityHandle Handle, float ScreenMargin = 0.0f);
};
