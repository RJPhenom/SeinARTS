/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinEntityWidget.h
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Explicit entity context and game-defined progress presentation for widgets.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/SeinEntityHandle.h"
#include "SeinEntityWidget.generated.h"
/** UI-only progress projection. The game decides whether it represents work, health, resources or something else. */
USTRUCT(BlueprintType)
struct SEINARTSUITOOLKIT_API FSeinProgressDisplay
{
	GENERATED_BODY()
	/** Whether the progress display should be shown. Independent of the fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	bool bVisible = false;
	/** Normalized display fraction, clamped to zero through one by the widget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	float Percent = 0;
	/** Optional text such as Waiting for builder or a material count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	FText Label;
};
/** Reusable entity widget. No owner-chain lookup or construction data is required. */
UCLASS(Abstract, Blueprintable)
class SEINARTSUITOOLKIT_API USeinEntityWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** Assign context explicitly. Invalid clears pooled or detached widget data. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	void SetEntityContext(FSeinEntityHandle Entity);
	/** Current entity supplied by the caller. Validate it before reading game data. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "SeinARTS")
	FSeinEntityHandle BoundEntity;
	/** Read a game-defined progress display. The default is hidden; override for health, work or another measure. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "SeinARTS|UI")
	FSeinProgressDisplay GetProgressDisplay() const;
	virtual FSeinProgressDisplay GetProgressDisplay_Implementation() const { return {}; }
	/** Optional Progress Bar name in this widget's tree. None leaves progress-bar updates to Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS")
	FName ProgressBarName = TEXT("ProgressBar");
	/** Optional Text Block name in this widget's tree. None leaves label updates to Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS")
	FName ProgressLabelName = NAME_None;
	/** Context changed or cleared. Reset any game-specific display caches here. Default does nothing. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|UI")
	void OnEntityContextChanged(FSeinEntityHandle Entity);
	/** Update from current data after binding, restore or during ordinary widget ticks. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|UI")
	void RefreshEntityPresentation();
protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
};
/** Example using the work fields on Sein Construction. Games using health or materials override Get Progress Display instead. */
UCLASS(Blueprintable)
class SEINARTSUITOOLKIT_API USeinConstructionWorkProgressWidget : public USeinEntityWidget
{
	GENERATED_BODY()
public:
	virtual FSeinProgressDisplay GetProgressDisplay_Implementation() const override;
};
