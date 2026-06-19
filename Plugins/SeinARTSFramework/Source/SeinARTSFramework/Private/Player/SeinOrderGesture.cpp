/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinOrderGesture.cpp
 * @brief   Default order-gesture interpreter: click → blob, drag → line.
 */

#include "Player/SeinOrderGesture.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Settings/PluginSettings.h"

USeinOrderGesture::USeinOrderGesture()
{
	// DragFormationTag is empty by default -> a drag uses the project-wide Default Formation
	// (Project Settings -> SeinARTS -> Formation), via the broker resolver's fallback. Set
	// this per-gesture to FORCE a specific formation tag (mapped through FormationsByTag)
	// regardless of the project default.
}

FSeinOrderGestureResult USeinOrderGesture::BuildOrder_Implementation(const FSeinOrderGestureInput& Input)
{
	FSeinOrderGestureResult Result;

	// Plain click (no drag). Single-click formations ON -> leave the formation UNnominated
	// so the resolver lays out the project Default Formation at the cursor (the destination
	// preview runs this same gesture, so it shows it too). OFF -> nominate Formation.Blob to
	// force the classic single-point move.
	if (!Input.bIsDrag)
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		if (!Settings || !Settings->bEnableSingleClickFormations)
		{
			Result.FormationTag = SeinARTSTags::Formation_Blob;
		}
		return Result;
	}

	// Drag: a line from start to end (or the full sampled path for path/spline modes),
	// nominating the drag formation.
	if (bForwardFullPath && Input.PathWorld.Num() >= 2)
	{
		Result.GuidePoints = Input.PathWorld;
	}
	else
	{
		Result.GuidePoints = { Input.StartWorld, Input.EndWorld };
	}
	Result.FormationTag = DragFormationTag; // empty -> project Default Formation (resolver fallback)
	return Result;
}
