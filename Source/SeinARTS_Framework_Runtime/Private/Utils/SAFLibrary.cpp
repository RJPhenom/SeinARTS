#include "Utils/SAFLibrary.h"
#include "Classes/SAFActor.h"
#include "Classes/SAFUnit.h"
#include "Classes/Units/SAFSquadMember.h"
#include "Classes/Units/SAFVehiclePawn.h"
#include "Classes/SAFPlayerController.h"
#include "Classes/SAFPlayerState.h"
#include "Classes/SAFPlayerController.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Interfaces/SAFActorInterface.h"
#include "Interfaces/SAFPlayerInterface.h"
#include "Interfaces/SAFUnitInterface.h"
#include "Interfaces/Units/SAFSquadMemberInterface.h"
#include "Interfaces/Units/SAFVehiclePawnInterface.h"
#include "Components/SAFCoverCollider.h"
#include "Structs/SAFOrder.h"
#include "Debug/SAFDebugTool.h"

namespace SAFLibrary {

	/** Soft-compare two soft refs (prefers loaded pointers, otherwise compares object paths). */
	bool SoftEqual(const TSoftObjectPtr<USAFAsset>& A, const TSoftObjectPtr<USAFAsset>& B) {
		if (A.Get() && B.Get()) return A.Get() == B.Get();
		return A.ToSoftObjectPath() == B.ToSoftObjectPath();
	}

	/** Checks if a player controller is valid, implements the player interface 
	 * and not marked for destruction. */
	bool IsPlayerControllerPtrValidSeinARTSPlayer(APlayerController* InPtr) {
		return IsValid(InPtr) 
		&& InPtr->GetClass()->ImplementsInterface(USAFPlayerInterface::StaticClass()) 
		&& !InPtr->IsActorBeingDestroyed();
	}

	/** Checks if an actor is valid, implements the SeinARTS asset interface 
	 * and not marked for destruction. */
	bool IsActorPtrValidSeinARTSActor(AActor* InPtr) {
		return IsValid(InPtr) 
		&& InPtr->GetClass()->ImplementsInterface(USAFActorInterface::StaticClass()) 
		&& !InPtr->IsActorBeingDestroyed();
	}

	/** Checks if an actor is valid, is a child or instance of class ASAFUnit, 
	 * implements the SeinARTS asset interface and not marked for destruction. */
	bool IsActorPtrValidSeinARTSUnit(AActor* InPtr) {
		return IsValid(InPtr) 
		&& InPtr->GetClass()->ImplementsInterface(USAFUnitInterface::StaticClass())
		&& !InPtr->IsActorBeingDestroyed();
	}

	/** Checks if an actor is valid, implements the cover interface and not 
	 * marked for destruction. */
	bool IsActorPtrValidSeinARTSCover(AActor* InPtr) {
		return IsValid(InPtr) 
		&& InPtr->FindComponentByClass<USAFCoverCollider>() != nullptr 
		&& !InPtr->IsActorBeingDestroyed();
	}
	
	/** Checks if a pawn is valid, implements the unit and squad member interfaces, 
	 * and not marked for destruction. */
	bool IsPawnPtrValidSeinARTSSquadMember(APawn* InPtr) {
		return IsValid(InPtr) 
		&& InPtr->GetClass()->ImplementsInterface(USAFActorInterface::StaticClass()) 
		&& InPtr->GetClass()->ImplementsInterface(USAFSquadMemberInterface::StaticClass()) 
		&& !InPtr->IsActorBeingDestroyed();
	}
	
	/** Checks if a pawn is valid, implements the unit and vehicle pawn interfaces, 
	 * and not marked for destruction. */
	bool IsPawnPtrValidSeinARTSVehiclePawn(APawn* InPtr) {
		return IsValid(InPtr) 
		&& InPtr->GetClass()->ImplementsInterface(USAFActorInterface::StaticClass()) 
		&& InPtr->GetClass()->ImplementsInterface(USAFVehiclePawnInterface::StaticClass()) 
		&& !InPtr->IsActorBeingDestroyed();
	}
	
	/** Decide if a candidate actor is friendly to a requesting player. */
	bool IsSelectionFriendly(const TArray<AActor*>& Selection, const APlayerState* RequestingPlayer) {
		for (AActor* Actor : Selection) 
		if (IsActorPtrValidSeinARTSActor(Actor) && ISAFActorInterface::Execute_GetOwningPlayer(Actor) != RequestingPlayer) return false;
		return true;
	}

	/** Filter input actors by player friendliness into OutActors. */
	TArray<AActor*> FilterSelectionByPlayer(const TArray<AActor*>& InActors, const APlayerState* RequestingPlayer, bool bShowDebugMessages) {
		TArray<AActor*> Result;
		if (!RequestingPlayer) { 
			if (bShowDebugMessages) SAFDEBUG_ERROR("FilterSelectionByPlayer Failed: invalid RequestingPlayer."); 
			return Result; 
		}

		for (AActor* Actor : InActors) {
			if (!IsActorPtrValidSeinARTSActor(Actor)) { 
				if (bShowDebugMessages && IsValid(Actor)) SAFDEBUG_INFO(FORMATSTR("FilterSelectionByPlayer skipped actor '%s' because it does not implement the unit interface.", *Actor->GetName()));
				else if(bShowDebugMessages) SAFDEBUG_WARNING("FilterSelectionByPlayer: invalid actor, skipping."); 
				continue; 
			}

			ASAFPlayerState* Owner = ISAFActorInterface::Execute_GetOwningPlayer(Actor);
			if (IsValid(Owner) && Owner == RequestingPlayer) { 
				if(bShowDebugMessages) SAFDEBUG_INFO(FORMATSTR("FilterSelectionByPlayer added matching actor '%s'.", *Actor->GetName())); 
				Result.Add(Actor); 
			} 
			
			else if(!IsValid(Owner) && bShowDebugMessages) SAFDEBUG_WARNING(FORMATSTR("FilterSelectionByPlayer called on actor '%s', but they returned nullptr for their owning player. Actor skipped.", *Actor->GetName()));
			else if (bShowDebugMessages) SAFDEBUG_INFO(FORMATSTR("FilterSelectionByPlayer skipped actor '%s' because it does not match the requesting player.", *Actor->GetName()));
		}

		return Result;
	}

	/** Construct a FGameplayAbilityTargetDataHandle out of FSAFOrder props, used for the ability GAS implementation. */
	FGameplayAbilityTargetDataHandle MakeTargetDataFromOrder(const FSAFOrder& Order, AActor* Source) {
		FGameplayAbilityTargetDataHandle Out;

		// Build a single LocationInfo element that can hold Start and/or End
		FGameplayAbilityTargetData_LocationInfo* LocData = new FGameplayAbilityTargetData_LocationInfo();

		FGameplayAbilityTargetingLocationInfo Src;
		Src.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform;
		Src.SourceActor = Source;
		Src.LiteralTransform = FTransform(Order.Vectors.Start);
		LocData->SourceLocation = Src;

		FGameplayAbilityTargetingLocationInfo Dst;
		Dst.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform;
		Dst.SourceActor  = Source;
		Dst.LiteralTransform = FTransform(Order.Vectors.End);
		LocData->TargetLocation = Dst;

		Out.Add(LocData);

		// Add actor target if present
		if (IsActorPtrValidSeinARTSActor(Order.Target.Get())) {
			FGameplayAbilityTargetDataHandle ActorHandle = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromActor(Order.Target.Get());
			UAbilitySystemBlueprintLibrary::AppendTargetDataHandle(Out, ActorHandle);
		}

		return Out;
	}

}
