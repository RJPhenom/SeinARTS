/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSCoreEntityModule.h
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Module declaration for the SeinARTSCoreEntity module.
 *				Defines the FSeinARTSCoreEntity module class with startup
 *				and shutdown lifecycle hooks for the entity/ECS layer
 *				of the deterministic lockstep simulation framework.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "Serialization/SeinLatentActionCodecRegistry.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Simulation/SeinCanonicalStateRecipeRegistry.h"

class SEINARTSCOREENTITY_API FSeinARTSCoreEntity
	: public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** True only while every framework-owned built-in schema is registered. */
	bool AreBuiltInCommandSchemasReady() const { return bBuiltInCommandSchemasReady; }

	/**
	 * Empty configuration is ready; invalid, partial, or settings-stale
	 * registration is not.
	 */
	bool AreConfiguredCanonicalStateRecipesReady() const;

	/**
	 * Prove the live Project Settings recipe list exactly matches the claims
	 * installed at module startup. Editor changes require an intentional module
	 * reload/restart before a world may consume the new composition.
	 */
	bool ValidateConfiguredCanonicalStateRecipes(FString& OutError) const;

	/** False keeps bootstrap fail-closed after a content-registration error. */
	bool IsSimulationContentContributorReady() const
	{
		return SimulationContentRegistrationHandle.IsValid();
	}

	bool IsWaitActionCodecReady() const
	{
		return WaitActionCodecHandle.IsValid();
	}

	bool ArePoolObjectCodecsReady() const
	{
		return PoolObjectCodecHandles.Num() == 3
			&& PoolObjectCodecHandles.ContainsByPredicate(
				[](const FSeinPoolObjectCodecRegistrationHandle& Handle)
				{
					return !Handle.IsValid();
				}) == false;
	}

private:
	TArray<FSeinCommandSchemaRegistrationHandle> BuiltInCommandSchemaHandles;
	FSeinSimulationContentRegistrationHandle SimulationContentRegistrationHandle;
	FSeinLatentActionCodecRegistrationHandle WaitActionCodecHandle;
	TArray<FSeinPoolObjectCodecRegistrationHandle>
		PoolObjectCodecHandles;
	TArray<FSeinCanonicalStateRecipeRegistrationHandle>
		ConfiguredCanonicalStateRecipeHandles;
	TArray<FString> ConfiguredCanonicalStateRecipePaths;
	bool bBuiltInCommandSchemasReady = false;
	bool bConfiguredCanonicalStateRecipesReady = false;
};
