#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SAFSquadMemberInterface.generated.h"

class ASAFSquad;
class USAFSquadMemberAsset;

UINTERFACE(Blueprintable)
class USAFSquadMemberInterface : public UInterface { GENERATED_BODY() };

/**
 * SAFSquadMemberInterface
 * 
 * The SAFSquadMemberInterface is the interface that designates and enables a pawn class to act as
 * a squad member for an ASAFSquad.
 */
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFSquadMemberInterface {

	GENERATED_BODY()

public:

	/** Initializes the squad member actor. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|SquadMember")
	void InitSquadMember(USAFSquadMemberAsset* InSquadMemberAsset, ASAFSquad* InSquad);

	/** Assigns the managing squad (server-authoritative). */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|SquadMember")
	void SetSquad(ASAFSquad* InSquad);

	/** Gets the SquadMember's managing squad, if any. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|SquadMember")
	ASAFSquad* GetSquad() const;

	/** Returns true if the SquadMember is currently assigned to a squad. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|SquadMember")
	bool HasSquad() const;
	
	/** Get the data asset that seeded this squad member. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|SquadMember")
	USAFSquadMemberAsset* GetSquadMemberAsset() const;

};
