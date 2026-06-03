/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldWidgetPool.h
 * @brief   Generic widget pooling system for screen-space per-entity widgets
 *          (unit banners, health bars, damage numbers, etc.).
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "SeinWorldWidgetPool.generated.h"

class UUserWidget;
class UPanelWidget;

/**
 * Pooling system for per-entity screen-space widgets.
 *
 * Designers create a Widget Blueprint for their per-entity UI (e.g., unit banner),
 * then create a SeinWorldWidgetPool on their overlay widget and call
 * Acquire/Release each frame to manage visible instances.
 *
 * Usage pattern (in a ticking overlay widget):
 *   1. Initialize(CanvasPanel, MyBannerWidgetClass, 50)
 *   2. Each tick: ReleaseAll(), then for each visible entity: Acquire(Handle)
 *   3. Position the acquired widget using SetRenderTranslation
 */
UCLASS(BlueprintType)
class SEINARTSUITOOLKIT_API USeinWorldWidgetPool : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Initialize the pool.
	 * @param InParent - The panel widget that will parent pooled widgets (e.g., a CanvasPanel)
	 * @param InWidgetClass - The UUserWidget subclass to pool
	 * @param InitialPoolSize - Number of widgets to pre-create
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Pool")
	void Initialize(UPanelWidget* InParent, TSubclassOf<UUserWidget> InWidgetClass, int32 InitialPoolSize = 20);

	/**
	 * Acquire a widget for an entity. If the entity already has an active widget,
	 * returns it. Otherwise, takes one from the pool (or creates a new one).
	 * The widget is made visible.
	 * @param Entity - The entity to acquire a widget for
	 * @return The widget instance (never nullptr after Initialize)
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Pool")
	UUserWidget* Acquire(FSeinEntityHandle Entity);

	/**
	 * Release a specific entity's widget back to the pool.
	 * The widget is hidden but not destroyed.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Pool")
	void Release(FSeinEntityHandle Entity);

	/** Release all active widgets back to the pool. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI|Pool")
	void ReleaseAll();

	/** Get the active widget for an entity. Returns nullptr if not active. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Pool")
	UUserWidget* GetActiveWidget(FSeinEntityHandle Entity) const;

	/** Get the number of currently active widgets. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Pool")
	int32 GetActiveCount() const { return ActiveWidgets.Num(); }

	/** Get the total pool size (active + inactive). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|UI|Pool")
	int32 GetTotalCount() const { return Pool.Num() + ActiveWidgets.Num(); }

private:
	UUserWidget* CreatePooledWidget();

	UPROPERTY()
	TWeakObjectPtr<UPanelWidget> ParentWidget;

	UPROPERTY()
	TSubclassOf<UUserWidget> WidgetClass;

	UPROPERTY()
	TArray<TObjectPtr<UUserWidget>> Pool;

	UPROPERTY()
	TMap<FSeinEntityHandle, TObjectPtr<UUserWidget>> ActiveWidgets;
};
