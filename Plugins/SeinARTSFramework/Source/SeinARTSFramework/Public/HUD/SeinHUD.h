/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinHUD.h
 * @brief   RTS HUD with marquee box selection, command feedback visualization,
 *          and debug overlays (command log panel, ping labels).
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SeinHUD.generated.h"

class ASeinActor;
class ASeinPlayerController;
class USeinCommandLogSubsystem;
class UUserWidget;

/**
 * RTS HUD that draws the marquee selection box and resolves actors
 * within the box when the mouse is released.
 *
 * Also renders the debug command log panel (toggle: Sein.Commands.ShowLog)
 * as a semi-transparent console-style overlay.
 */
UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	// ========== Marquee Visual Settings ==========

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|HUD|Marquee")
	FLinearColor MarqueeFillColor = FLinearColor(0.0f, 0.5f, 1.0f, 0.15f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|HUD|Marquee")
	FLinearColor MarqueeBorderColor = FLinearColor(0.0f, 0.7f, 1.0f, 0.8f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|HUD|Marquee")
	float MarqueeBorderThickness = 1.5f;

	// ========== Command Drag Visual Settings ==========

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|HUD|Command")
	FLinearColor CommandDragLineColor = FLinearColor(0.0f, 1.0f, 0.3f, 0.8f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|HUD|Command")
	float CommandDragLineThickness = 2.0f;

	// ========== HUD Layout Widget ==========

	/**
	 * Optional Widget Blueprint class to create as the HUD root layout.
	 * Designers set this to their custom HUD Widget Blueprint. Created and added
	 * to the viewport in BeginPlay. Any UUserWidget subclass works.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|HUD|Layout")
	TSubclassOf<UUserWidget> HUDLayoutWidgetClass;

	/** The instantiated HUD layout widget (nullptr if HUDLayoutWidgetClass is not set). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|HUD|Layout")
	TObjectPtr<UUserWidget> HUDLayoutWidget;

	// ========== Debug Command Log Settings ==========

	/** Background color for the command log panel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|HUD|Debug")
	FLinearColor LogPanelBgColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.75f);

	/** Font scale for log entries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "SeinARTS|HUD|Debug")
	float LogFontScale = 1.0f;

protected:
	void DrawMarqueeBox();
	void ResolveMarqueeSelection();
	void DrawCommandDragLine();
	void DrawCommandLogPanel();

	/**
	 * Smarter replacement for AHUD::GetActorsInSelectionRectangle. For every
	 * selectable ASeinActor, build a screen-space CONVEX POLYGON from its
	 * authored sim geometry (FSeinExtentsComponent shapes; falls back to the
	 * actor's component bounds) and test that polygon against the marquee
	 * rectangle with SAT. The engine routine instead unions an actor's full 3D
	 * bounds into a loose screen-space AABB — coarse, and it drifts from the
	 * real silhouette for tall/large/animated actors, over-selecting units the
	 * box never touched. Projection goes through AHUD::Project (canvas space) so
	 * the coordinates match the marquee rect (built from GetMousePosition).
	 * P0/P1 are the marquee's two screen-space corners (any order).
	 */
	void CollectActorsInMarquee(const FVector2D& P0, const FVector2D& P1, TArray<ASeinActor*>& OutActors);

	ASeinPlayerController* GetSeinPlayerController() const;

private:
	bool bMarqueeWasActive = false;
};
