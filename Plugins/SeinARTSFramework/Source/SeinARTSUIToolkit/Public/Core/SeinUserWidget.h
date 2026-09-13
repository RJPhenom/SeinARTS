/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinUserWidget.h
 * @author       RJ Macklem
 * @created      2 Jun 2026
 * @latest       12 Sep 2026
 * @brief        Base widget with cached gameplay access and fallback click consumption.
 *
 * @disclaimer   Updated with assistance from an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/SeinEntityHandle.h"
#include "SeinUserWidget.generated.h"

class USeinUISubsystem;
class USeinWorldSubsystem;
class USeinActorBridgeSubsystem;
class USeinSelectionModel;
class USeinPlayerViewModel;
class USeinEntityViewModel;
class USeinMinimapViewModel;
class ASeinPlayerController;

/**
 * Base class for SeinARTS UI widgets.
 *
 * Automatically discovers and caches the UI subsystem, player controller,
 * and world subsystem on construct. Provides convenience accessors so that
 * Widget Blueprints can access the full ViewModel and data layer with
 * minimal boilerplate.
 *
 * This is intentionally thin — no auto-binding to delegates, no Refresh()
 * pattern. Widgets subscribe to whatever they need in Blueprint.
 */
UCLASS(Abstract, Blueprintable)
class SEINARTSUITOOLKIT_API USeinUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Stops unhandled left and right clicks from reaching parent widgets or the game.
	 *  Child controls and this widget's Blueprint mouse handlers run first.
	 *  Disable to let unhandled clicks continue to parents, which may still consume them.
	 *  For a full-screen HUD, keep the widget and layout canvas Not Hit-Testable (Self Only)
	 *  and its interactive backgrounds Visible so empty screen space remains playable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Consume Clicks"))
	bool bConsumeClicks = true;

	// ========== Auto-Cached References (available after NativeConstruct) ==========

	/** The UI subsystem (ViewModel factory). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI")
	TWeakObjectPtr<USeinUISubsystem> UISubsystem;

	/** The local player's SeinARTS player controller. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI")
	TWeakObjectPtr<ASeinPlayerController> SeinPlayerController;

	/** The simulation world subsystem. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI")
	TWeakObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	// ========== Convenience Accessors ==========

	/** Get the selection model (tracks current selection, provides entity ViewModels). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinSelectionModel* GetSelectionModel() const;

	/** Get the local player's ViewModel (resources, tech, etc.). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinPlayerViewModel* GetLocalPlayerViewModel() const;

	/** Get or create an entity ViewModel for a specific entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinEntityViewModel* GetEntityViewModel(FSeinEntityHandle Handle) const;

	/** Get the minimap view-model (blips, fog overlay, background). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinMinimapViewModel* GetMinimapViewModel() const;

	/** Get the actor bridge subsystem. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinActorBridgeSubsystem* GetActorBridge() const;

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	FReply ConsumeUnhandledClick(FReply Reply, const FPointerEvent& MouseEvent) const;
};
