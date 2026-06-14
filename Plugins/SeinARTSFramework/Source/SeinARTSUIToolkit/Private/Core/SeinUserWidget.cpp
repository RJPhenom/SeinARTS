/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinUserWidget.cpp
 * @brief   Base widget implementation.
 */

#include "Core/SeinUserWidget.h"
#include "Core/SeinUISubsystem.h"
#include "ViewModel/SeinSelectionModel.h"
#include "ViewModel/SeinPlayerViewModel.h"
#include "ViewModel/SeinEntityViewModel.h"
#include "ViewModel/SeinMinimapViewModel.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Player/SeinPlayerController.h"
#include "Engine/World.h"

void USeinUserWidget::NativeConstruct()
{
	// Cache subsystems BEFORE Super::NativeConstruct() fires the BP "Event Construct".
	// Otherwise BP code that calls GetSelectionModel() / etc. from Event Construct
	// sees null subsystems and returns nullptr.
	if (UWorld* World = GetWorld())
	{
		UISubsystem = World->GetSubsystem<USeinUISubsystem>();
		WorldSubsystem = World->GetSubsystem<USeinWorldSubsystem>();
		SeinPlayerController = Cast<ASeinPlayerController>(GetOwningPlayer());
	}

	Super::NativeConstruct();
}

USeinSelectionModel* USeinUserWidget::GetSelectionModel() const
{
	return UISubsystem.IsValid() ? UISubsystem->GetSelectionModel() : nullptr;
}

USeinPlayerViewModel* USeinUserWidget::GetLocalPlayerViewModel() const
{
	return UISubsystem.IsValid() ? UISubsystem->GetLocalPlayerViewModel() : nullptr;
}

USeinEntityViewModel* USeinUserWidget::GetEntityViewModel(FSeinEntityHandle Handle) const
{
	return UISubsystem.IsValid() ? UISubsystem->GetEntityViewModel(Handle) : nullptr;
}

USeinMinimapViewModel* USeinUserWidget::GetMinimapViewModel() const
{
	return UISubsystem.IsValid() ? UISubsystem->GetMinimapViewModel() : nullptr;
}

USeinActorBridgeSubsystem* USeinUserWidget::GetActorBridge() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<USeinActorBridgeSubsystem>() : nullptr;
}
