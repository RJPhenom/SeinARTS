/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverEntityDraw.cpp
 *
 * Cover-area + slot draw layer for the entity-bridge visualizer. Registered
 * via FSeinARTSEditorModule's draw callback registry from SeinARTSCoverEditor's
 * StartupModule.
 *
 * Slot ring sizing: diameter = nav cell size from
 * `USeinARTSCoreSettings::CellSize`. Reading the framework's cell size at
 * draw time keeps the viz in sync if a designer rebakes with a different
 * cell size — no manual tuning of a hardcoded ring radius. Falls back to
 * 50cm when settings can't be reached (defensive only — the CDO is loaded
 * for the editor process lifetime).
 *
 * Slot color: derived from `FSeinCoverComponent::QualityTag`:
 *   - SeinARTS.Cover.Heavy    → green
 *   - SeinARTS.Cover.Light    → yellow
 *   - SeinARTS.Cover.Negative → red
 *   - empty / other tag       → white
 *
 * Area volume color is independent of quality (light-green wire box / sphere)
 * so the area volume reads as "where cover applies" and the slot rings carry
 * the quality signal — follows the common UX of color = protection level.
 */

#include "Visualizers/SeinCoverEntityDraw.h"

#include "Components/SeinCoverComponent.h"
#include "Tags/SeinCoverGameplayTags.h"
#include "Types/SeinCoverTypes.h"

#include "Settings/PluginSettings.h"      // USeinARTSCoreSettings::CellSize

#include "SceneManagement.h"              // DrawCircle, DrawOrientedWireBox, DrawWireSphere
#include "StructUtils/InstancedStruct.h"

namespace SeinCoverEntityDrawLocal
{
	/** Resolve the slot ring diameter from the active nav settings. The
	 *  cover viz reads cell size at draw time so the rings auto-track any
	 *  rebake / settings change — no manual sync needed. */
	static float GetSlotDiameterUU()
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (!Settings) return 100.0f; // defensive default — settings CDO should always exist in editor
		const float CellSizeFloat = Settings->CellSize.ToFloat();
		// Guard against unconfigured / zero cell size. 50cm fallback matches
		// the framework's "infantry-scale RTS" default in the plugin settings
		// docstring; keeps slot rings visible regardless.
		return CellSizeFloat > 1.0f ? CellSizeFloat : 100.0f;
	}

	/** Slot color derived from the cover provider's QualityTag. Follows the
	 *  common cover UX so designers can read protection level at a glance:
	 *  green=heavy, yellow=light, red=negative, white=unset/other. */
	static FColor GetSlotColorForQuality(const FGameplayTag& QualityTag)
	{
		if (!QualityTag.IsValid())
		{
			return FColor::White;
		}
		if (QualityTag == SeinCoverTags::Cover_Heavy)
		{
			return FColor(0, 220, 80);   // green
		}
		if (QualityTag == SeinCoverTags::Cover_Light)
		{
			return FColor(255, 220, 0);  // yellow
		}
		if (QualityTag == SeinCoverTags::Cover_Negative)
		{
			return FColor(220, 30, 30);  // red
		}
		// Designer-defined extension tag (project-specific). White keeps the
		// slots visible without claiming a specific protection signal —
		// matches the "no tag" fallback.
		return FColor::White;
	}

	/** Planar wire circle (XY) — DrawCircle's stock signature in 5.7 takes
	 *  (PDI, Base, X, Y, Color, Radius, NumSides, DPG, Thickness). */
	static void DrawWireCircleXY(FPrimitiveDrawInterface* PDI,
		const FVector& Center, float Radius, int32 NumSides,
		FColor Color, uint8 DepthPriority, float Thickness)
	{
		DrawCircle(PDI, Center, FVector::ForwardVector, FVector::RightVector,
			Color, Radius, NumSides, DepthPriority, Thickness);
	}
}

void SeinCoverEntityDraw::DrawCoverEntries(
	const TArray<FInstancedStruct>& ComponentData,
	const FQuat& ActorQuat, const FVector& ActorPos,
	FPrimitiveDrawInterface* PDI)
{
	// Greenish area to distinguish from extents (yellow box) + vision
	// (radial green circle from the FoW viz layer). Area color is uniform
	// per-provider; quality signal lives on the slot rings only.
	const FColor AreaColor    = FColor( 80, 220, 120);
	const float  Thickness    = 1.5f;
	const int32  CircleSides  = 32;

	const float  SlotDiameter = SeinCoverEntityDrawLocal::GetSlotDiameterUU();
	const float  SlotRadius   = SlotDiameter * 0.5f;

	for (const FInstancedStruct& Entry : ComponentData)
	{
		if (!Entry.IsValid()) continue;
		if (Entry.GetScriptStruct() != FSeinCoverComponent::StaticStruct()) continue;

		const FSeinCoverComponent& Data = Entry.Get<FSeinCoverComponent>();

		const FVector AxisX = ActorQuat.GetForwardVector();
		const FVector AxisY = ActorQuat.GetRightVector();
		const FVector AxisZ = ActorQuat.GetUpVector();

		// Area volume — drawn at actor pose. LocalExtents semantics:
		// Box → half-extents on each axis; Sphere → X is radius (Y/Z
		// ignored). Matches FSeinCoverArea's storage convention.
		switch (Data.Area.Shape)
		{
		case ESeinCoverAreaShape::Box:
		{
			const FVector HalfExtents(
				FMath::Max(0.0f, Data.Area.LocalExtents.X.ToFloat()),
				FMath::Max(0.0f, Data.Area.LocalExtents.Y.ToFloat()),
				FMath::Max(0.0f, Data.Area.LocalExtents.Z.ToFloat()));
			DrawOrientedWireBox(
				PDI,
				ActorPos,
				AxisX, AxisY, AxisZ,
				HalfExtents,
				AreaColor,
				SDPG_World,
				Thickness);
			break;
		}
		case ESeinCoverAreaShape::Sphere:
		{
			const float Radius = FMath::Max(0.0f, Data.Area.LocalExtents.X.ToFloat());
			if (Radius <= 0.0f) break;
			// DrawWireSphere renders three orthogonal great circles — reads
			// as a sphere at any view angle.
			DrawWireSphere(PDI, ActorPos, AreaColor,
				Radius, /*NumSides*/ 24, SDPG_World, Thickness);
			break;
		}
		case ESeinCoverAreaShape::None:
		default:
			break;
		}

		// Slot markers — horizontal ring per slot, diameter matching the
		// nav cell size. Color follows the provider's QualityTag (see
		// GetSlotColorForQuality). Designer reads protection level at
		// a glance and sizing scales with the underlying grid resolution.
		const FColor SlotColor = SeinCoverEntityDrawLocal::GetSlotColorForQuality(Data.QualityTag);

		for (const FFixedVector& LocalSlot : Data.Slots)
		{
			const FVector LocalOffset(
				LocalSlot.X.ToFloat(),
				LocalSlot.Y.ToFloat(),
				LocalSlot.Z.ToFloat());
			const FVector WorldSlotPos = ActorPos + ActorQuat.RotateVector(LocalOffset);
			SeinCoverEntityDrawLocal::DrawWireCircleXY(PDI, WorldSlotPos,
				SlotRadius, CircleSides, SlotColor, SDPG_World, Thickness);
		}
	}
}
