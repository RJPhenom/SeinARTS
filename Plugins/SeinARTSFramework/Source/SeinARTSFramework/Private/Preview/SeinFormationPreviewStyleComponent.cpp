/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinFormationPreviewStyleComponent.cpp
 * @author       RJ Macklem
 * @created      29 Aug 2026
 * @latest       29 Aug 2026
 * @brief        Data-only per-unit preview style component (see header). No tick, no
 *               logic beyond packing the authored fields into the element style.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Preview/SeinFormationPreviewStyleComponent.h"

USeinFormationPreviewStyleComponent::USeinFormationPreviewStyleComponent()
{
	// Pure data — the preview subsystem pulls from it; nothing to tick.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

FSeinFormationPreviewElementStyle USeinFormationPreviewStyleComponent::BuildElementStyle(
	FSeinEntityHandle MemberHandle) const
{
	FSeinFormationPreviewElementStyle Style;
	Style.MemberHandle = MemberHandle;
	Style.MarkerMesh = MarkerMesh;
	Style.MarkerMaterial = MarkerMaterial;
	Style.StyleTint = StyleTint;
	Style.MarkerSizeUU = MarkerSizeUU;
	Style.StyleTag = StyleTag;
	return Style;
}
