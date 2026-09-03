/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinFormationPreviewComponent.cpp
 * @author       RJ Macklem
 * @created      02 Sep 2026
 * @latest       02 Sep 2026
 * @brief        Implementation of the destination-preview opt-in component.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Preview/SeinFormationPreviewComponent.h"

USeinFormationPreviewComponent::USeinFormationPreviewComponent()
{
	// Pure authoring data — the preview subsystem reads it; the component itself never runs.
	PrimaryComponentTick.bCanEverTick = false;
}
