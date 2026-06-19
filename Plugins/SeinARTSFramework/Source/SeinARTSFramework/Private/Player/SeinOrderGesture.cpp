/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinOrderGesture.cpp
 * @brief   Default order-gesture interpreter: click → blob, drag → line.
 */

#include "Player/SeinOrderGesture.h"
#include "Tags/SeinARTSGameplayTags.h"

USeinOrderGesture::USeinOrderGesture()
{
	// Default drag formation = the framework's Box formation (a Total-War rank box
	// sized by the drag). Designers re-point this (and the resolver's FormationsByTag)
	// to bind a drag to any formation (e.g. Formation.Line for a true single rank).
	DragFormationTag = SeinARTSTags::Formation_Box;
}

FSeinOrderGestureResult USeinOrderGesture::BuildOrder_Implementation(const FSeinOrderGestureInput& Input)
{
	FSeinOrderGestureResult Result;

	// Simple click: no guide, no nominated formation → the resolver falls back to its
	// default formation (a blob — the historic single-destination move).
	if (!Input.bIsDrag)
	{
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
	Result.FormationTag = DragFormationTag;
	return Result;
}
