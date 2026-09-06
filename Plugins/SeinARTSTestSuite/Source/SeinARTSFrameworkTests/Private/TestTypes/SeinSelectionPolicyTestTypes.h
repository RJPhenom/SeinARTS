// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
#pragma once
#include "Actor/SeinActor.h"
#include "Player/SeinPlayerController.h"
#include "SeinSelectionPolicyTestTypes.generated.h"

UCLASS()
class ASeinSelectionOtherTestActor : public ASeinActor
{
	GENERATED_BODY()
};

UCLASS()
class ASeinSelectionTestController : public ASeinPlayerController
{
	GENERATED_BODY()
public:
	using ASeinPlayerController::HandleSelectAll;
	using ASeinPlayerController::HandleSelectAllOfType;
	using ASeinPlayerController::PurgeStaleSelection;
	void SetShift(bool bHeld) { bShiftHeld = bHeld; }
};
