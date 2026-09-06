/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinSteeringDebugSelection.h
 * @author       RJ Macklem
 * @created      05 Sep 2026
 * @latest       05 Sep 2026
 * @brief        Render-side selection query without a movement-to-gameplay dependency.
 * @disclaimer   This code was generated in whole or in part with the assistance of an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "Core/SeinEntityHandle.h"
class APlayerController;
#if UE_ENABLE_DEBUG_DRAWING
DECLARE_MULTICAST_DELEGATE_TwoParams(FSeinSteeringDebugSelectionQuery, APlayerController*, TArray<FSeinEntityHandle>&);
namespace UE::SeinARTSMovement
{
	SEINARTSMOVEMENT_API FSeinSteeringDebugSelectionQuery& SteeringDebugSelectionQuery();
}
#endif
