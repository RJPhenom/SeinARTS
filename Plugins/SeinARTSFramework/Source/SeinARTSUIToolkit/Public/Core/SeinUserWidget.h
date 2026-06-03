/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinUserWidget.h
 * @brief   Thin base class for all SeinARTS UI widgets. Provides auto-wired
 *          access to the UI subsystem, player controller, and world subsystem.
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

	/** Get the actor bridge subsystem. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	USeinActorBridgeSubsystem* GetActorBridge() const;

protected:
	virtual void NativeConstruct() override;
};
