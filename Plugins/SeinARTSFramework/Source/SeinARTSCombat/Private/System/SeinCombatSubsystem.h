/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatSubsystem.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Declares the world subsystem hosting Combat systems and the
 *               derived target-acquisition index.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatTargetIndex.h"
#include "Simulation/SeinSystemHostSubsystem.h"
#include "SeinCombatSubsystem.generated.h"

class ISeinSystem;
class USeinWorldSubsystem;

UCLASS()
class SEINARTSCOMBAT_API USeinCombatSubsystem : public USeinSystemHostSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Collect a canonical-order spatial prefilter for one target query.
	 *  False asks the caller to retain the exact full-sweep fallback. */
	bool CollectTargetCandidates(
		const USeinWorldSubsystem& World,
		const FFixedVector& Origin,
		FFixedPoint Radius,
		FSeinEntityHandle Exclude,
		TArray<FSeinEntityHandle>& OutHandles) const;

	/** Remove delegates, cache state, and hosted systems before module unload. */
	void ReleaseModuleOwnedStateForModuleUnload();

protected:
	virtual void CreateSystems(
		USeinWorldSubsystem& Sim,
		TArray<TUniquePtr<ISeinSystem>>& OutSystems) override;

private:
	void InvalidateTargetIndex();
	void ReleaseModuleOwnedState();

	TWeakObjectPtr<USeinWorldSubsystem> SimRef;
	mutable FSeinCombatTargetIndex TargetIndex;
};
