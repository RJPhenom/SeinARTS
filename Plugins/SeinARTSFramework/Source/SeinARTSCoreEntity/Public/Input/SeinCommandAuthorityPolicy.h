/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandAuthorityPolicy.h
 * @brief   Topology-neutral, Blueprint-pluggable command authorization policy.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerState.h"
#include "Data/SeinMatchSettings.h"
#include "Input/SeinCommand.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinCommandAuthorityPolicy.generated.h"

class USeinWorldSubsystem;

/**
 * Read-only deterministic query capability supplied to authority policies.
 *
 * Blueprint object pins do not preserve C++ pointee constness, so handing a
 * policy the world subsystem would also hand it every simulation mutator. This
 * view intentionally exposes value copies and pure queries only, and refuses to
 * act as a WorldContext object. Custom policies remain fully data-driven without
 * being able to change the state they are currently authorizing.
 */
UCLASS(BlueprintType, NotBlueprintable, Transient, meta = (SeinDeterministic))
class SEINARTSCOREENTITY_API USeinCommandAuthorityView : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override { return nullptr; }

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority")
	int32 GetSimulationTick() const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority")
	ESeinMatchState GetMatchState() const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority")
	FSeinMatchSettings GetMatchSettings() const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority")
	bool GetPlayerState(FSeinPlayerID Player, FSeinPlayerState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority")
	TArray<FSeinPlayerID> GetRegisteredPlayers() const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority")
	bool IsEntityValid(FSeinEntityHandle Entity) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority")
	FSeinPlayerID GetEntityOwner(FSeinEntityHandle Entity) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority")
	FGameplayTagContainer GetEntityTags(FSeinEntityHandle Entity) const;

	/** Read an arbitrary deterministic component as a value copy. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority",
		meta = (SeinDeterministicOnly))
	bool GetEntityComponent(
		FSeinEntityHandle Entity,
		UScriptStruct* ComponentType,
		FInstancedStruct& OutComponent) const;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Command|Authority")
	bool CanPlayerControlEntity(
		FSeinPlayerID Player,
		FSeinEntityHandle Entity,
		FGameplayTag CommandType) const;

private:
	void Initialize(USeinWorldSubsystem* InWorld) { World = InWorld; }

	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> World;

	friend class USeinWorldSubsystem;
};

/**
 * Stateless command authority strategy.
 *
 * The dispatcher invokes the selected class default object. Implementations
 * must derive decisions solely from the command and deterministic world state;
 * topology, connection ownership, and coordinator election belong to ingress.
 */
UCLASS(Abstract, Blueprintable, Const)
class SEINARTSCOREENTITY_API USeinCommandAuthorityPolicy : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Positive deterministic behavior revision included in protocol compatibility.
	 * Bump when authorization or payer semantics change without changing class path.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Command|Authority",
		meta = (ClampMin = "1"))
	int32 ImplementationRevision = 1;

	/** Authorize one structurally valid command for its registered scope. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Command|Authority")
	bool AuthorizeCommand(
		const USeinCommandAuthorityView* View,
		const FSeinCommand& Command,
		ESeinCommandAuthorityScope Scope,
		FGameplayTag& OutRejectionReason) const;

	/** Entity-level primitive used by Entity and mixed-recipient EntitySet scopes. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Command|Authority")
	bool CanControlEntity(
		const USeinCommandAuthorityView* View,
		const FSeinCommand& Command,
		FSeinEntityHandle Entity) const;

	/** Resolve who pays/refunds an entity action. This is separate from control. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Command|Authority")
	FSeinPlayerID ResolveResourcePayer(
		const USeinCommandAuthorityView* View,
		const FSeinCommand& Command,
		FSeinEntityHandle Entity) const;
};

/** Owner control plus exact deterministic grants; entity owner pays by default. */
UCLASS(meta = (DisplayName = "Sein Default Command Authority Policy"))
class SEINARTSCOREENTITY_API USeinDefaultCommandAuthorityPolicy
	: public USeinCommandAuthorityPolicy
{
	GENERATED_BODY()

public:
	virtual bool AuthorizeCommand_Implementation(
		const USeinCommandAuthorityView* View,
		const FSeinCommand& Command,
		ESeinCommandAuthorityScope Scope,
		FGameplayTag& OutRejectionReason) const override;

	virtual bool CanControlEntity_Implementation(
		const USeinCommandAuthorityView* View,
		const FSeinCommand& Command,
		FSeinEntityHandle Entity) const override;

	virtual FSeinPlayerID ResolveResourcePayer_Implementation(
		const USeinCommandAuthorityView* View,
		const FSeinCommand& Command,
		FSeinEntityHandle Entity) const override;
};
