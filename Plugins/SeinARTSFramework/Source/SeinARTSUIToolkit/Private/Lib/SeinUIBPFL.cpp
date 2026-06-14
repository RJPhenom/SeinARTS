/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinUIBPFL.cpp
 * @brief   UI Blueprint Function Library implementation.
 */

#include "Lib/SeinUIBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Actor/SeinActor.h"
#include "Abilities/SeinAbility.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinIdentityComponent.h"
#include "Components/SeinProductionComponent.h"
#include "Core/SeinPlayerState.h"
#include "SeinLevelData.h"
#include "SeinLevelDataSubsystem.h"
#include "Volumes/SeinLevelVolume.h"
#include "Player/SeinCameraPawn.h"
#include "Components/Image.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

// ==================== Internal Helpers ====================

namespace
{
	USeinWorldSubsystem* GetSimSubsystem(const UObject* WorldContextObject)
	{
		if (!WorldContextObject)
		{
			return nullptr;
		}
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	}

	/** Identity lookup against sim storage. FSeinIdentityComponent is
	 *  injected at spawn from the entity bridge's authored ComponentData. */
	const FSeinIdentityComponent* GetIdentityForEntity(const UObject* WorldContextObject, FSeinEntityHandle Handle)
	{
		USeinWorldSubsystem* SimSub = GetSimSubsystem(WorldContextObject);
		if (!SimSub || !Handle.IsValid()) return nullptr;
		return SimSub->GetComponent<FSeinIdentityComponent>(Handle);
	}
}

// ==================== Entity Display Helpers ====================

FText USeinUIBPFL::SeinGetEntityDisplayName(const UObject* WorldContextObject, FSeinEntityHandle Handle)
{
	const FSeinIdentityComponent* Identity = GetIdentityForEntity(WorldContextObject, Handle);
	return Identity ? Identity->DisplayName : FText::GetEmpty();
}

UTexture2D* USeinUIBPFL::SeinGetEntityIcon(const UObject* WorldContextObject, FSeinEntityHandle Handle)
{
	const FSeinIdentityComponent* Identity = GetIdentityForEntity(WorldContextObject, Handle);
	return Identity ? Identity->Icon.Get() : nullptr;
}

UTexture2D* USeinUIBPFL::SeinGetEntityPortrait(const UObject* WorldContextObject, FSeinEntityHandle Handle)
{
	const FSeinIdentityComponent* Identity = GetIdentityForEntity(WorldContextObject, Handle);
	return Identity ? Identity->Portrait.Get() : nullptr;
}

FGameplayTag USeinUIBPFL::SeinGetEntityIdentityTag(const UObject* WorldContextObject, FSeinEntityHandle Handle)
{
	const FSeinIdentityComponent* Identity = GetIdentityForEntity(WorldContextObject, Handle);
	return Identity ? Identity->IdentityTag : FGameplayTag();
}

ESeinRelation USeinUIBPFL::SeinGetEntityRelation(const UObject* WorldContextObject, FSeinEntityHandle Handle, FSeinPlayerID PlayerID)
{
	USeinWorldSubsystem* SimSub = GetSimSubsystem(WorldContextObject);
	if (!SimSub || !Handle.IsValid())
	{
		return ESeinRelation::Neutral;
	}

	FSeinPlayerID Owner = SimSub->GetEntityOwner(Handle);

	if (Owner.IsNeutral())
	{
		return ESeinRelation::Neutral;
	}

	if (Owner == PlayerID)
	{
		return ESeinRelation::Friendly;
	}

	// Check team alliance
	const FSeinPlayerState* OwnerState = SimSub->GetPlayerState(Owner);
	const FSeinPlayerState* OtherState = SimSub->GetPlayerState(PlayerID);

	if (OwnerState && OtherState && OwnerState->TeamID != 0 && OwnerState->TeamID == OtherState->TeamID)
	{
		return ESeinRelation::Friendly;
	}

	return ESeinRelation::Enemy;
}

// ==================== Conversion Helpers ====================

float USeinUIBPFL::SeinFixedToFloat(FFixedPoint Value)
{
	return Value.ToFloat();
}

FVector USeinUIBPFL::SeinFixedVectorToVector(const FFixedVector& Value)
{
	return Value.ToVector();
}

FText USeinUIBPFL::SeinFormatResourceCost(const TMap<FGameplayTag, float>& Cost)
{
	if (Cost.IsEmpty())
	{
		return FText::FromString(TEXT("Free"));
	}

	FString Result;
	bool bFirst = true;

	for (const auto& Pair : Cost)
	{
		if (!bFirst)
		{
			Result += TEXT(", ");
		}
		// Use the tag's leaf name for display (e.g. "SeinARTS.Resource.Manpower" → "Manpower")
		FString TagString = Pair.Key.ToString();
		int32 LastDot = INDEX_NONE;
		TagString.FindLastChar(TEXT('.'), LastDot);
		const FString Label = (LastDot != INDEX_NONE) ? TagString.RightChop(LastDot + 1) : TagString;

		Result += FString::Printf(TEXT("%d %s"), FMath::RoundToInt(Pair.Value), *Label);
		bFirst = false;
	}

	return FText::FromString(Result);
}

// ==================== Screen Projection ====================

bool USeinUIBPFL::SeinWorldToScreen(const UObject* WorldContextObject, APlayerController* PlayerController, FVector WorldPos, FVector2D& OutScreenPos)
{
	if (!PlayerController)
	{
		OutScreenPos = FVector2D::ZeroVector;
		return false;
	}

	return PlayerController->ProjectWorldLocationToScreen(WorldPos, OutScreenPos, true);
}

bool USeinUIBPFL::SeinScreenToWorld(const UObject* WorldContextObject, APlayerController* PlayerController, FVector2D ScreenPos, float GroundZ, FVector& OutWorldPos)
{
	if (!PlayerController)
	{
		OutWorldPos = FVector::ZeroVector;
		return false;
	}

	FVector WorldOrigin, WorldDirection;
	if (!PlayerController->DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, WorldOrigin, WorldDirection))
	{
		OutWorldPos = FVector::ZeroVector;
		return false;
	}

	// Intersect ray with horizontal plane at GroundZ
	if (FMath::IsNearlyZero(WorldDirection.Z))
	{
		OutWorldPos = FVector::ZeroVector;
		return false;
	}

	const float T = (GroundZ - WorldOrigin.Z) / WorldDirection.Z;
	if (T < 0.0f)
	{
		OutWorldPos = FVector::ZeroVector;
		return false;
	}

	OutWorldPos = WorldOrigin + WorldDirection * T;
	return true;
}

// ==================== Minimap Math ====================

FVector2D USeinUIBPFL::SeinWorldToMinimap(FVector WorldPos, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax)
{
	const FVector2D Range = WorldBoundsMax - WorldBoundsMin;
	if (Range.X <= 0.0f || Range.Y <= 0.0f)
	{
		return FVector2D(0.5f, 0.5f);
	}

	return FVector2D(
		(WorldPos.X - WorldBoundsMin.X) / Range.X,
		(WorldPos.Y - WorldBoundsMin.Y) / Range.Y
	);
}

FVector USeinUIBPFL::SeinMinimapToWorld(FVector2D MinimapUV, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax, float GroundZ)
{
	const FVector2D Range = WorldBoundsMax - WorldBoundsMin;
	return FVector(
		WorldBoundsMin.X + MinimapUV.X * Range.X,
		WorldBoundsMin.Y + MinimapUV.Y * Range.Y,
		GroundZ
	);
}

TArray<FVector2D> USeinUIBPFL::SeinGetCameraFrustumCorners(APlayerController* PlayerController, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax, float GroundZ)
{
	TArray<FVector2D> Corners;

	if (!PlayerController)
	{
		return Corners;
	}

	// Get viewport size
	int32 ViewportX, ViewportY;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return Corners;
	}

	// Screen corners: top-left, top-right, bottom-right, bottom-left
	const FVector2D ScreenCorners[4] = {
		FVector2D(0.0f, 0.0f),
		FVector2D(static_cast<float>(ViewportX), 0.0f),
		FVector2D(static_cast<float>(ViewportX), static_cast<float>(ViewportY)),
		FVector2D(0.0f, static_cast<float>(ViewportY))
	};

	Corners.Reserve(4);
	for (int32 i = 0; i < 4; ++i)
	{
		FVector WorldPos;
		if (SeinScreenToWorld(PlayerController, PlayerController, ScreenCorners[i], GroundZ, WorldPos))
		{
			Corners.Add(SeinWorldToMinimap(WorldPos, WorldBoundsMin, WorldBoundsMax));
		}
	}

	return Corners;
}

UTexture2D* USeinUIBPFL::SeinGetMinimapTextureForLevel(const UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return nullptr;
	}

	// 1. Per-level designer override on a level volume wins (first one set).
	for (TActorIterator<ASeinLevelVolume> It(World); It; ++It)
	{
		ASeinLevelVolume* Vol = *It;
		if (Vol && !Vol->MinimapOverrideTexture.IsNull())
		{
			if (UTexture2D* Override = Vol->MinimapOverrideTexture.LoadSynchronous())
			{
				return Override;
			}
		}
	}

	// 2. Baked top-down texture from the level-data substrate.
	if (USeinLevelData* LevelData = USeinLevelDataSubsystem::GetLevelDataForWorld(World))
	{
		return LevelData->GetMinimapTexture();
	}

	return nullptr;
}

float USeinUIBPFL::SeinGetMinimapRotationDegrees(APlayerController* PlayerController, bool bRotateWithCamera, float RotationOffsetDeg)
{
	if (!bRotateWithCamera || !PlayerController)
	{
		return RotationOffsetDeg;
	}
	if (ASeinCameraPawn* Cam = Cast<ASeinCameraPawn>(PlayerController->GetPawn()))
	{
		// Counter-rotate the map so the camera's forward points "up".
		return -Cam->GetCameraYaw() + RotationOffsetDeg;
	}
	return RotationOffsetDeg;
}

bool USeinUIBPFL::SeinMinimapLocalToWorld(FVector2D LocalPos, FVector2D WidgetSize, FVector2D WorldBoundsMin,
	FVector2D WorldBoundsMax, float GroundZ, float MapRotationDeg, bool bCircleClip, FVector& OutWorld)
{
	OutWorld = FVector::ZeroVector;

	const float S = FMath::Min(WidgetSize.X, WidgetSize.Y);
	if (S <= 0.0f)
	{
		return false;
	}

	const FVector2D Center = WidgetSize * 0.5f;
	const FVector2D Pr = LocalPos - Center;
	if (bCircleClip && Pr.Size() > S * 0.5f)
	{
		return false;
	}

	// Inverse of the display rotation → recover the north-up offset.
	const float R = FMath::DegreesToRadians(-MapRotationDeg);
	const float C = FMath::Cos(R);
	const float Sn = FMath::Sin(R);
	const FVector2D P(Pr.X * C - Pr.Y * Sn, Pr.X * Sn + Pr.Y * C);

	const FVector2D UV(P.X / S + 0.5f, P.Y / S + 0.5f);
	if (UV.X < 0.0f || UV.X > 1.0f || UV.Y < 0.0f || UV.Y > 1.0f)
	{
		return false;
	}

	OutWorld = SeinMinimapToWorld(UV, WorldBoundsMin, WorldBoundsMax, GroundZ);
	return true;
}

void USeinUIBPFL::SeinDrawCameraViewportToRenderTarget(const UObject* WorldContextObject, APlayerController* PlayerController,
	UTextureRenderTarget2D* RenderTarget, FVector2D WorldBoundsMin, FVector2D WorldBoundsMax, float GroundZ,
	FLinearColor LineColor, float LineThickness)
{
	if (!RenderTarget || !PlayerController)
	{
		return;
	}

	// The Kismet rendering helpers take a non-const world context.
	UObject* WCO = const_cast<UObject*>(WorldContextObject);

	UKismetRenderingLibrary::ClearRenderTarget2D(WCO, RenderTarget, FLinearColor::Transparent);

	// True camera-ground footprint (north-up UV) — deprojected screen corners, so it
	// widens/shrinks faithfully with tilt. <4 corners means the camera is at the horizon.
	const TArray<FVector2D> CornerUVs = SeinGetCameraFrustumCorners(PlayerController, WorldBoundsMin, WorldBoundsMax, GroundZ);
	if (CornerUVs.Num() != 4)
	{
		return;
	}

	const FVector2D RTSize(static_cast<float>(RenderTarget->SizeX), static_cast<float>(RenderTarget->SizeY));

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(WCO, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector2D A = CornerUVs[i] * RTSize;
			const FVector2D B = CornerUVs[(i + 1) % 4] * RTSize;
			Canvas->K2_DrawLine(A, B, LineThickness, LineColor);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(WCO, Context);
}

void USeinUIBPFL::SeinSetImageBrushResource(UImage* Image, UObject* ResourceObject)
{
	if (!Image)
	{
		return;
	}
	FSlateBrush Brush = Image->GetBrush();
	Brush.SetResourceObject(ResourceObject);
	Image->SetBrush(Brush);
}

// ==================== Action Slot Builders ====================

FSeinActionSlotData USeinUIBPFL::SeinBuildAbilitySlotData(const UObject* WorldContextObject, FSeinEntityHandle Entity, FGameplayTag AbilityTag, int32 SlotIndex)
{
	FSeinActionSlotData Data;
	Data.SlotIndex = SlotIndex;
	Data.ActionTag = AbilityTag;
	Data.State = ESeinActionSlotState::Empty;

	USeinWorldSubsystem* SimSub = GetSimSubsystem(WorldContextObject);
	if (!SimSub || !Entity.IsValid())
	{
		return Data;
	}

	const FSeinAbilityComponent* AbilityComp = SimSub->GetComponent<FSeinAbilityComponent>(Entity);
	if (!AbilityComp)
	{
		return Data;
	}

	USeinAbility* Ability = AbilityComp->FindAbilityByTag(*SimSub, AbilityTag);
	if (!Ability)
	{
		return Data;
	}

	Data.Name = Ability->AbilityName;
	Data.ActionTag = Ability->AbilityTag;
	Data.Icon = Ability->Icon;

	// Convert tag-keyed resource cost to float for display
	for (const auto& Pair : Ability->ResourceCost.Amounts)
	{
		Data.ResourceCost.Add(Pair.Key, Pair.Value.ToFloat());
	}

	// Determine state
	if (Ability->bIsActive)
	{
		Data.State = ESeinActionSlotState::Active;
	}
	else if (Ability->IsOnCooldown())
	{
		Data.State = ESeinActionSlotState::OnCooldown;
		const float TotalCD = Ability->Cooldown.ToFloat();
		Data.CooldownPercent = TotalCD > 0.0f ? Ability->CooldownRemaining.ToFloat() / TotalCD : 0.0f;
	}
	else
	{
		Data.State = ESeinActionSlotState::Available;
	}

	return Data;
}

TArray<FSeinActionSlotData> USeinUIBPFL::SeinBuildAllAbilitySlotData(const UObject* WorldContextObject, FSeinEntityHandle Entity)
{
	TArray<FSeinActionSlotData> Result;

	USeinWorldSubsystem* SimSub = GetSimSubsystem(WorldContextObject);
	if (!SimSub || !Entity.IsValid())
	{
		return Result;
	}

	const FSeinAbilityComponent* AbilityComp = SimSub->GetComponent<FSeinAbilityComponent>(Entity);
	if (!AbilityComp)
	{
		return Result;
	}

	int32 SlotIndex = 0;
	for (USeinAbility* Ability : AbilityComp->GetAbilityInstances(*SimSub))
	{
		if (Ability && !Ability->bIsPassive)
		{
			Result.Add(SeinBuildAbilitySlotData(WorldContextObject, Entity, Ability->AbilityTag, SlotIndex));
			++SlotIndex;
		}
	}

	return Result;
}

// SeinBuildProductionSlotData removed (refactored 2026-05-05): production is
// unified into the ability surface. Designers render production buttons via
// SeinBuildAllAbilitySlotData (same code path as ability buttons).

// ==================== World-Space Widget Helpers ====================

bool USeinUIBPFL::SeinProjectEntityToScreen(const UObject* WorldContextObject, APlayerController* PlayerController, FSeinEntityHandle Handle, float VerticalWorldOffset, FVector2D& OutScreenPos)
{
	OutScreenPos = FVector2D::ZeroVector;

	if (!PlayerController || !Handle.IsValid())
	{
		return false;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return false;
	}

	USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>();
	if (!Bridge)
	{
		return false;
	}

	ASeinActor* Actor = Bridge->GetActorForEntity(Handle);
	if (!Actor)
	{
		return false;
	}

	FVector WorldPos = Actor->GetActorLocation() + FVector(0.0f, 0.0f, VerticalWorldOffset);
	return PlayerController->ProjectWorldLocationToScreen(WorldPos, OutScreenPos, true);
}

bool USeinUIBPFL::SeinIsEntityOnScreen(const UObject* WorldContextObject, APlayerController* PlayerController, FSeinEntityHandle Handle, float ScreenMargin)
{
	FVector2D ScreenPos;
	if (!SeinProjectEntityToScreen(WorldContextObject, PlayerController, Handle, 0.0f, ScreenPos))
	{
		return false;
	}

	if (!PlayerController)
	{
		return false;
	}

	int32 ViewportX, ViewportY;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	return ScreenPos.X >= -ScreenMargin
		&& ScreenPos.Y >= -ScreenMargin
		&& ScreenPos.X <= ViewportX + ScreenMargin
		&& ScreenPos.Y <= ViewportY + ScreenMargin;
}
