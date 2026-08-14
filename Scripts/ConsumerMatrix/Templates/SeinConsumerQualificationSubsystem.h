#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SeinConsumerQualificationSubsystem.generated.h"

class USeinReplayReader;
class USeinWorldSubsystem;
class UNetDriver;
struct FSeinCommand;
namespace ENetworkFailure
{
	enum Type : int;
}

/**
 * Generated-consumer-only packaged runtime driver.
 *
 * The production plugins do not know this class exists. It drives their public
 * lobby, lockstep, resync, reconnect, replay, and canonical-state surfaces from
 * a disposable downstream project when explicitly enabled by command line.
 */
UCLASS()
class SEINCONSUMER_API USeinConsumerQualificationSubsystem
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaSeconds);
	void TickServer(UWorld& World);
	void TickClient(UWorld& World);
	void TickReplay(UWorld& World);
	void ObserveReplayCommands(
		int32 Tick,
		const TArray<FSeinCommand>& Commands);
	void HandleNetworkFailure(
		UWorld* World,
		UNetDriver* NetDriver,
		ENetworkFailure::Type FailureType,
		const FString& ErrorString);

	bool IsMap(const UWorld& World, const TCHAR* PackageName) const;
	bool WriteMarker(const TCHAR* FileName, const FString& Body) const;
	void Fail(const FString& Reason);

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle NetworkFailureHandle;
	FString Role;
	FString MarkerDirectory;
	FString ServerAddress;
	FString ReplayPath;
	FString ExpectedReplayRoot;
	FString ExpectedMovementState;
	double StartedAtSeconds = 0.0;
	double DisconnectIssuedAtSeconds = 0.0;
	double LastServerLockstepProgressAtSeconds = 0.0;
	double LastClientResyncProgressAtSeconds = 0.0;
	double LastReconnectMovementDiagnosticAtSeconds = 0.0;
	int32 InitialResyncRequestTick = INDEX_NONE;
	int32 ReconnectResyncRequestTick = INDEX_NONE;
	int32 LastReconnectResyncPhase = INDEX_NONE;
	int32 ServerReconnectTick = INDEX_NONE;
	int32 ExpectedReplayEndTick = INDEX_NONE;
	bool bFailed = false;
	bool bWorldObservedWritten = false;
	bool bListenTravelIssued = false;
	bool bServerHostClaimed = false;
	bool bServerReadyWritten = false;
	bool bMatchStartRequested = false;
	bool bServerMatchStarted = false;
	bool bServerRootGossipCompleted = false;
	bool bServerPairGrantSubmitted = false;
	bool bServerPairGrantObserved = false;
	bool bServerMovementObserved = false;
	bool bServerSawDrop = false;
	bool bServerSawReconnecting = false;
	bool bServerSawReconnect = false;
	bool bServerPairRevokeSubmitted = false;
	bool bServerPairRevokeObserved = false;
	bool bServerReplayPublished = false;
	bool bPingSubmitted = false;
	bool bInitialResyncRequested = false;
	bool bInitialResyncObserved = false;
	bool bInitialResyncCompleted = false;
	bool bClientPairGrantObserved = false;
	bool bClientMovementCommandSubmitted = false;
	bool bClientMovementObserved = false;
	bool bInitialConnectTravelIssued = false;
	bool bDisconnectIssued = false;
	bool bReconnectTravelIssued = false;
	bool bReconnectNetworked = false;
	bool bReconnectResyncRequested = false;
	bool bReconnectResyncObserved = false;
	bool bReconnectCompleted = false;
	bool bReconnectPairCapabilityPreserved = false;
	bool bReconnectMovementPreserved = false;
	bool bClientPairRevokeObserved = false;
	bool bReplayTravelIssued = false;
	bool bReplayStarted = false;
	bool bReplayObservedPlaying = false;
	bool bReplayObservedPairGrant = false;
	bool bReplayObservedPairRevoke = false;
	bool bReplayObservedMovementCommand = false;
	bool bReplayObservedMovementActive = false;
	bool bReplayObservedMovementAdvanced = false;
	FDelegateHandle ReplayCommandObserverHandle;
	TWeakObjectPtr<USeinReplayReader> ActiveReplayReader;
	TWeakObjectPtr<USeinWorldSubsystem> ReplayObserverWorld;
};
