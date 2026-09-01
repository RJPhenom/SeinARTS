/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinFormationPreviewComponent.cpp
 */

#include "Preview/SeinFormationPreviewComponent.h"

USeinFormationPreviewComponent::USeinFormationPreviewComponent()
{
	// Pure authoring data — the preview subsystem reads it; the component itself never runs.
	PrimaryComponentTick.bCanEverTick = false;
}
