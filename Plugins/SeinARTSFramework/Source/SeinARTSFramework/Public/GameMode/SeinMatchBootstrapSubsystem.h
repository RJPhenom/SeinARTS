/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchBootstrapSubsystem.h
 * @brief   Topology-neutral Framework facade for tick-zero materialization.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"
#include "SeinMatchBootstrapSubsystem.generated.h"

class FSeinMatchBootstrapTransaction;
class USeinWorldSubsystem;
struct FSeinMatchBootstrapReceipt;
struct FSeinMatchSettings;

/**
 * Binds the Framework's default actor-based materializer to CoreEntity's
 * bootstrap barrier. The subsystem never ticks. Its transaction scratch lives
 * only from the first materialization request until Core authorizes or fails
 * the sealed receipt.
 */
UCLASS()
class SEINARTSFRAMEWORK_API USeinMatchBootstrapSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual ~USeinMatchBootstrapSubsystem() override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/**
	 * Release callbacks and transaction scratch before this module's code can
	 * be withdrawn. Idempotent with ordinary world teardown.
	 */
	void ReleaseModuleOwnedStateForModuleUnload();

private:
	/** Resolve and seal the standalone tick-zero contract when it has not
	 * already been prepared. Identical retries are accepted.
	 * bAllowLobbyContract: whether a lobby-published match snapshot may supply
	 * the contract. Explicit launches (lobby in-place start, lobby travel with
	 * the StandaloneLaunch URL intent) allow it; an AMBIENT BeginPlay auto-start
	 * in any other world must not, or a stale snapshot left over from an
	 * aborted/kicked session drives a bootstrap in the menu map. */
	bool EnsureStandaloneBootstrapAuthorized(bool bAllowLobbyContract = true);
	bool MaterializeMatchBootstrap(
		const FSeinMatchSettings& Settings,
		const FGuid& AuthorizationContextDigest,
		FSeinMatchBootstrapReceipt& OutReceipt,
		FString& OutError);
	bool LaunchStandaloneSimulation();

	void HandleMatchBootstrapClosed(bool bAuthorized);
	void ReleaseTransaction();

	TUniquePtr<FSeinMatchBootstrapTransaction> Transaction;
	TWeakObjectPtr<USeinWorldSubsystem> BoundWorldSubsystem;
	FDelegateHandle BootstrapClosedHandle;
	FSeinMatchBootstrapAuthorityHandle BootstrapAuthority;
	bool bMaterializerExecuting = false;
	bool bReleaseTransactionWhenMaterializerReturns = false;
};
