/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInputConfig.h
 * @brief   Data asset holding Enhanced Input action and mapping context references.
 *          Designers create an instance and assign it to the player controller.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SeinInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Data asset containing all input action and mapping context references
 * used by the RTS player controller and camera pawn.
 *
 * Create an instance of this asset in your content folder and assign it
 * to ASeinPlayerController::InputConfig (or the GameMode's default).
 */
UCLASS(BlueprintType, Const)
class SEINARTSFRAMEWORK_API USeinInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// ========== Mapping Context ==========

	/** Default input mapping context applied when the controller is set up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	/** Priority for the default mapping context (higher = takes precedence). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input")
	int32 DefaultMappingPriority = 0;

	// ========== Keyboard Camera Actions ==========

	/** Keyboard pan (WASD / arrow keys). Value: Axis2D. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Keyboard")
	TObjectPtr<UInputAction> IA_KeyPan;

	/** Keyboard rotate (Q/E). Value: Axis1D (yaw delta). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Keyboard")
	TObjectPtr<UInputAction> IA_KeyRotate;

	/** Keyboard zoom (Z/X). Value: Axis1D. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Keyboard")
	TObjectPtr<UInputAction> IA_KeyZoom;

	/** Keyboard pan speed multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Keyboard", meta = (ClampMin = "0.0"))
	float KeyPanSpeed = 1.0f;

	/** Keyboard rotate speed multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Keyboard", meta = (ClampMin = "0.0"))
	float KeyRotateSpeed = 1.0f;

	/** Keyboard zoom speed multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Keyboard", meta = (ClampMin = "0.0"))
	float KeyZoomSpeed = 1.0f;

	// ========== Mouse Camera Actions ==========

	/** Mouse pan (middle-mouse drag). Value: Axis2D (mouse delta). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Mouse")
	TObjectPtr<UInputAction> IA_MousePan;

	/** Mouse rotate (Alt+MMB drag). Value: Axis1D (mouse X delta). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Mouse")
	TObjectPtr<UInputAction> IA_MouseRotate;

	/** Mouse zoom (scroll wheel). Value: Axis1D (zoom delta). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Mouse")
	TObjectPtr<UInputAction> IA_MouseZoom;

	/** Mouse pan speed multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Mouse", meta = (ClampMin = "0.0"))
	float MousePanSpeed = 1.0f;

	/** Mouse rotate speed multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Mouse", meta = (ClampMin = "0.0"))
	float MouseRotateSpeed = 1.0f;

	/** Mouse zoom speed multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera|Mouse", meta = (ClampMin = "0.0"))
	float MouseZoomSpeed = 1.0f;

	// ========== Camera Utility Actions ==========

	/** Follow focused entity (F). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera")
	TObjectPtr<UInputAction> IA_FollowCamera;

	/** Reset camera rotation (Backspace). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Camera")
	TObjectPtr<UInputAction> IA_ResetCamera;

	// ========== Selection & Command Actions ==========

	/** Primary select (left-click). Value: bool (pressed/released). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Selection")
	TObjectPtr<UInputAction> IA_Select;

	/** Issue command (right-click). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Command")
	TObjectPtr<UInputAction> IA_Command;

	/** Cycle active focus through selection (Tab). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Selection")
	TObjectPtr<UInputAction> IA_CycleFocus;

	/** Double-click select (select all of same type on screen). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Selection")
	TObjectPtr<UInputAction> IA_SelectAllOfType;

	/** Ping a location (Ctrl+MMB). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Command")
	TObjectPtr<UInputAction> IA_Ping;

	// ========== Modifier Keys ==========

	/** Shift modifier (add to selection / queue command). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Modifiers")
	TObjectPtr<UInputAction> IA_ModifierShift;

	/** Ctrl modifier (toggle selection). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Modifiers")
	TObjectPtr<UInputAction> IA_ModifierCtrl;

	/** Alt modifier (reserved for designer use). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Modifiers")
	TObjectPtr<UInputAction> IA_ModifierAlt;

	// ========== Action Hotkeys ==========

	/**
	 * Action slot keys (12 slots for ability/action panel buttons).
	 * Default (WASD mode): 1-6 + Shift+1-6, or designer-assigned.
	 * Grid mode (squad-tactical layout): Q,W,E,R / A,S,D,F / Z,X,C,V.
	 * Value: digital (trigger on press).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Actions")
	TArray<TObjectPtr<UInputAction>> IA_ActionSlots;

	// ========== Menu ==========

	/** Menu / cancel (Escape). Value: bool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|Menu")
	TObjectPtr<UInputAction> IA_Menu;

	// ========== Control Groups ==========

	/** Control group keys (0-9). Value: digital (trigger on press). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Input|ControlGroups")
	TArray<TObjectPtr<UInputAction>> IA_ControlGroups;
};
