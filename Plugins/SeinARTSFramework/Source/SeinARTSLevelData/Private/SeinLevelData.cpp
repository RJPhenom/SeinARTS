/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelData.cpp
 */

#include "SeinLevelData.h"
#include "SeinStaticEnvironmentAdoption.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "SeinARTSLevelDataLog.h"

bool USeinLevelData::ComputeStaticEnvironmentDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	OutDigest.Invalidate();
	OutError = FString::Printf(
		TEXT("Level Data implementation '%s' did not declare an exact static-environment digest."),
		*GetClass()->GetPathName());
	return false;
}

bool USeinLevelData::ComputeStateCoverageClaim(
	FSeinLevelDataStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutClaim = {};
	OutError = FString::Printf(
		TEXT("Level Data implementation '%s' did not declare exact mutable-state coverage."),
		*GetClass()->GetPathName());
	return false;
}

FSeinStaticEnvironmentAdoptionResult::
	FSeinStaticEnvironmentAdoptionResult() = default;

FSeinStaticEnvironmentAdoptionResult::
	FSeinStaticEnvironmentAdoptionResult(
		const FSeinStaticEnvironmentAdoptionResult& Other) = default;

FSeinStaticEnvironmentAdoptionResult::
	FSeinStaticEnvironmentAdoptionResult(
		FSeinStaticEnvironmentAdoptionResult&& Other) = default;

FSeinStaticEnvironmentAdoptionResult::
	~FSeinStaticEnvironmentAdoptionResult() = default;

FSeinStaticEnvironmentAdoptionResult&
FSeinStaticEnvironmentAdoptionResult::operator=(
	const FSeinStaticEnvironmentAdoptionResult& Other) = default;

FSeinStaticEnvironmentAdoptionResult&
FSeinStaticEnvironmentAdoptionResult::operator=(
	FSeinStaticEnvironmentAdoptionResult&& Other) = default;

FSeinStaticEnvironmentAdoptionResult
FSeinStaticEnvironmentAdoptionResult::Adopted()
{
	FSeinStaticEnvironmentAdoptionResult Result;
	Result.Outcome = ESeinStaticEnvironmentAdoptionOutcome::Adopted;
	return Result;
}

FSeinStaticEnvironmentAdoptionResult
FSeinStaticEnvironmentAdoptionResult::NotApplicable(
	FString InDetail)
{
	FSeinStaticEnvironmentAdoptionResult Result;
	Result.Outcome =
		ESeinStaticEnvironmentAdoptionOutcome::NotApplicable;
	Result.Detail = MoveTemp(InDetail);
	return Result;
}

FSeinStaticEnvironmentAdoptionResult
FSeinStaticEnvironmentAdoptionResult::Rejected(FString InReason)
{
	FSeinStaticEnvironmentAdoptionResult Result;
	Result.Outcome =
		ESeinStaticEnvironmentAdoptionOutcome::Rejected;
	Result.Detail = MoveTemp(InReason);
	return Result;
}

bool FSeinStaticEnvironmentAdoptionResult::IsAdopted() const
{
	return Outcome == ESeinStaticEnvironmentAdoptionOutcome::Adopted;
}

bool FSeinStaticEnvironmentAdoptionResult::IsNotApplicable() const
{
	return Outcome ==
		ESeinStaticEnvironmentAdoptionOutcome::NotApplicable;
}

bool FSeinStaticEnvironmentAdoptionResult::IsRejected() const
{
	return Outcome == ESeinStaticEnvironmentAdoptionOutcome::Rejected;
}

void USeinLevelData::InitializeForWorld(UWorld* World)
{
	OwningWorld = World;
	OnInitialized(World);
}

void USeinLevelData::DeinitializeFromWorld()
{
	OnDeinitialized();
	OwningWorld.Reset();
}

bool USeinLevelData::CanMutateStaticEnvironment(
	const TCHAR* Operation,
	UWorld* RequestedWorld,
	FString& OutError) const
{
	OutError.Reset();
	UWorld* BoundWorld = OwningWorld.Get();
	if (!BoundWorld)
	{
		BoundWorld = GetWorld();
	}

	if (RequestedWorld && BoundWorld && RequestedWorld != BoundWorld)
	{
		OutError = FString::Printf(
			TEXT("%s targeted a world other than this level-data substrate's owning world."),
			Operation);
		return false;
	}

	UWorld* EffectiveWorld = RequestedWorld ? RequestedWorld : BoundWorld;
	const USeinWorldSubsystem* Sim = EffectiveWorld
		? EffectiveWorld->GetSubsystem<USeinWorldSubsystem>()
		: nullptr;
	if (Sim && Sim->GetCanonicalStateContractDigest().IsValid())
	{
		OutError = FString::Printf(
			TEXT("%s is not legal after the match StateContract freezes; rebake or reload before bootstrap, then restart the match/PIE session."),
			Operation);
		return false;
	}
	return true;
}

bool USeinLevelData::BeginBake(UWorld* World)
{
	FString Error;
	if (!CanMutateStaticEnvironment(TEXT("Level-data bake"), World, Error))
	{
		UE_LOG(LogSeinLevelData, Error, TEXT("%s"), *Error);
		return false;
	}
	return BeginBakeImpl(World);
}

bool USeinLevelData::LoadFromAsset(USeinLevelDataAsset* Asset)
{
	FString Error;
	if (!CanMutateStaticEnvironment(
		TEXT("Level-data asset adoption"), nullptr, Error))
	{
		UE_LOG(LogSeinLevelData, Error, TEXT("%s"), *Error);
		return false;
	}
	return LoadFromAssetImpl(Asset);
}
