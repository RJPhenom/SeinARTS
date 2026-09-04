/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinFormationPreviewComponent.cpp
 * @author       RJ Macklem
 * @created      02 Sep 2026
 * @latest       03 Sep 2026
 * @brief        Implementation of the destination-preview renderer component.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Preview/SeinFormationPreviewComponent.h"

USeinFormationPreviewComponent::USeinFormationPreviewComponent()
{
	// Pure authoring data — the preview subsystem reads it; the component itself never runs.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

FSeinFormationPreviewElementStyle USeinFormationPreviewComponent::BuildElementStyle(
	FSeinEntityHandle MemberHandle,
	const FSeinFormationPreviewElementStyle* InheritedStyle) const
{
	FSeinFormationPreviewElementStyle Style = InheritedStyle
		? *InheritedStyle
		: FSeinFormationPreviewElementStyle();
	Style.MemberHandle = MemberHandle;

	if (MarkerMesh)
	{
		Style.MarkerMesh = MarkerMesh;
	}
	if (MarkerMaterial)
	{
		Style.MarkerMaterial = MarkerMaterial;
	}
	if (StyleTint != FLinearColor::White)
	{
		Style.StyleTint = StyleTint;
	}
	if (MarkerSizeUU > 0.f)
	{
		Style.MarkerSizeUU = MarkerSizeUU;
	}
	if (StyleTag.IsValid())
	{
		Style.StyleTag = StyleTag;
	}

	return Style;
}
