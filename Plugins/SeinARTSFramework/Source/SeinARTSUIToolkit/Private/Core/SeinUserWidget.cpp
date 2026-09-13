/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinUserWidget.cpp
 * @author       RJ Macklem
 * @created      2 Jun 2026
 * @latest       12 Sep 2026
 * @brief        Cached gameplay access and click consumption after Blueprint handlers.
 *
 * @disclaimer   Updated with assistance from an AI language model.
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
#include "InputCoreTypes.h"

FReply USeinUserWidget::ConsumeUnhandledClick(FReply Reply, const FPointerEvent& MouseEvent) const
{
	const FKey Button = MouseEvent.GetEffectingButton();
	if (!Reply.IsEventHandled() && bConsumeClicks
		&& (Button == EKeys::LeftMouseButton || Button == EKeys::RightMouseButton))
	{
		return FReply::Handled();
	}
	return Reply;
}

FReply USeinUserWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Super dispatches the Blueprint override, including minimap camera and order input.
	// Preserve handled replies intact so capture, focus and drag requests survive.
	return ConsumeUnhandledClick(Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent), InMouseEvent);
}

FReply USeinUserWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return ConsumeUnhandledClick(Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent), InMouseEvent);
}

FReply USeinUserWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return ConsumeUnhandledClick(Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent), InMouseEvent);
}

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
