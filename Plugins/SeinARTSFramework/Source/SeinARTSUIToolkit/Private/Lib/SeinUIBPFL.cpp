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
#include "Engine/World.h"
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
