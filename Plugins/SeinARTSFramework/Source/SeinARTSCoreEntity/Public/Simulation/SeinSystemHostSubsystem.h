/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSystemHostSubsystem.h
 * @brief   Base UWorldSubsystem that hosts one or more ISeinSystems on the sim
 *          loop with MANAGED LIFETIME — the safe way to add a custom sim system.
 *
 *          Subclass it and override CreateSystems() to hand over your systems; the
 *          base registers them with USeinWorldSubsystem during subsystem
 *          initialization, before the per-world execution topology freezes, and
 *          unregisters + destroys them on teardown. This removes the manual
 *          new / RegisterSystem / Unregister / delete dance (and its leak/dangle
 *          footgun across PIE + level reloads) that hand-rolled hosts otherwise repeat.
 *
 *          Example:
 *              UCLASS()
 *              class UMyThreatSubsystem : public USeinSystemHostSubsystem
 *              {
 *                  GENERATED_BODY()
 *              protected:
 *                  virtual void CreateSystems(USeinWorldSubsystem& Sim,
 *                      TArray<TUniquePtr<ISeinSystem>>& Out) override
 *                  {
 *                      Out.Add(MakeUnique<FMyThreatSystem>());
 *                  }
 *              };
 *
 *          A subsystem with OTHER initialization / teardown responsibilities may
 *          override Initialize / Deinitialize directly — just call Super so the hosted
 *          systems are still registered / cleaned up. See SeinSystemPriority.h for the
 *          tick-priority slots to register into.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/SeinTickPhase.h"   // ISeinSystem (complete type required by the owned TUniquePtr)
#include "SeinSystemHostSubsystem.generated.h"

class USeinWorldSubsystem;

UCLASS(Abstract)
class SEINARTSCOREENTITY_API USeinSystemHostSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Sever every hosted executable system from its live sim world before the
	 * module that defines those systems unloads.
	 *
	 * A module that owns a USeinSystemHostSubsystem subclass must call this
	 * from IModuleInterface::PreUnloadCallback for each live instance. The
	 * unregister invalidates an already-frozen match, then the TUniquePtrs are
	 * destroyed while their defining module is still executable. The method is
	 * idempotent; the later world-subsystem Deinitialize is a no-op.
	 */
	void ReleaseHostedSystemsForModuleUnload();

protected:
	/** Override to create + hand over the systems this subsystem hosts. Called once
	 *  during subsystem initialization, after the sim world subsystem exists.
	 *  Constructors must not require Actor BeginPlay. Ownership of each
	 *  TUniquePtr transfers to the base (registered now, destroyed on Deinitialize).
	 *  Default: hosts nothing. */
	virtual void CreateSystems(USeinWorldSubsystem& Sim, TArray<TUniquePtr<ISeinSystem>>& OutSystems) {}

private:
	void ReleaseHostedSystems();

	TArray<TUniquePtr<ISeinSystem>> HostedSystems;
	TWeakObjectPtr<USeinWorldSubsystem> SimRef;
};
