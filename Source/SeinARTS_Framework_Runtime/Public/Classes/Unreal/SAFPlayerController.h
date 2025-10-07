#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/SAFPlayerInterface.h"
#include "Interfaces/SAFActorInterface.h"
#include "Structs/SAFOrder.h"
#include "SAFPlayerController.generated.h"

class ASAFCameraPawn;
class ASAFFormationManager;
class ASAFHUD;

// Event Delegates
// ==================================================================================================
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnClientReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerReady, ASAFPlayerController*, PlayerController);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectionUpdated, const TArray<AActor*>&, NewSelection);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectStarted, FVector2D, SelectStartPosition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectEnded, FVector2D, SelectEndPosition);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOrderIssued, FSAFOrder, Order, const TArray<AActor*>&, ReceivingActors);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOrderStarted, FVector2D, OrderStartPosition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOrderEnded, FVector2D, OrderEndPosition);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPingIssued, FVector, PingStartPosition, FVector, PingEndPosition, AActor*, PingedActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPingStarted, FVector2D, PingStartPosition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPingEnded, FVector2D, PingUpdatePosition);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQueueButtonStateChanged, bool, bNewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnControlButtonStateChanged, bool, bNewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAlternateButtonStateChanged, bool, bNewState);

/**
 * SAFPlayerController
 * 
 * PlayerController subclass for SeinARTS Framework.
 * Implements the SAFPlayerInterface for selection management.
 * 
 * PlayerControllers in the SeinARTS Framework communicate with the HUD and actors
 * using event dispatchers and receivers. Some of which have been proivided in the 
 * SAFPlayerInterface. You will be required to wire events for actions in Blueprints
 * if you choose to create your own. Blueprints with default wirings have been provided
 * to you in the plugin contents folder. For quick prototyping, it is recommended to 
 * subclass these blueprints for quick iteration. For support, please reach out via
 * the Phenom Studios discord server.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Player Controller"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFPlayerController : public APlayerController, public ISAFPlayerInterface {

	GENERATED_BODY()

public:

	ASAFPlayerController();

	// Event Bindings
	// ======================================================================================
	UPROPERTY(BlueprintAssignable, Category="SeinARTS|Networking")
	FOnClientReady OnClientReady;
	UPROPERTY(BlueprintAssignable, Category="SeinARTS|Networking")
	FOnServerReady OnServerReady;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Selection")
	FOnSelectionUpdated OnSelectionUpdated;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Selection")
	FOnSelectStarted OnSelectStarted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Selection")
	FOnSelectEnded OnSelectEnded;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Orders")
	FOnOrderIssued OnOrderIssued;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Orders")
	FOnOrderStarted OnOrderStarted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Orders")
	FOnOrderEnded OnOrderEnded;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Ping")
	FOnPingIssued OnPingIssued;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Ping")
	FOnPingStarted OnPingStarted;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Ping")
	FOnPingEnded OnPingEnded;
	
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Modifier Keys")
	FOnQueueButtonStateChanged OnQueueButtonStateChanged;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Modifier Keys")
	FOnControlButtonStateChanged OnControlButtonStateChanged;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="SeinARTS|Modifier Keys")
	FOnAlternateButtonStateChanged OnAlternateButtonStateChanged;

	// Player Interface Overrides
	// ================================================================================================================
	virtual void                        InitPlayer_Implementation();
	virtual bool                        IsQueueButtonDown_Implementation() const;
	virtual bool                        IsControlButtonDown_Implementation() const;
	virtual bool                        IsAlternateButtonDown_Implementation() const;
	virtual ECollisionChannel           GetCursorTraceChannel_Implementation() const;
	
	virtual ASAFFormationManager*       GetCurrentFormation_Implementation(const TArray<AActor*>& SelectionSnapshot);
	virtual ASAFFormationManager*       CreateNewFormation_Implementation(const TArray<AActor*>& InActors);

	virtual bool                        IsMySelectionFriendly_Implementation() const;
	virtual TArray<AActor*>             GetSelection_Implementation() const;
	virtual void                        SetSelection_Implementation(const TArray<AActor*>& InSelection);
	virtual void                        ClearSelection_Implementation();
	virtual void                        ClearSelectionQueue_Implementation();
	virtual void                        ProcessSelectionQueue_Implementation(bool bWasMarqueeDrawn);
	virtual bool                        Select_Implementation(AActor* Actor);
	virtual bool                        QueueSelect_Implementation(AActor* Actor);
	virtual void                        SingleSelect_Implementation();
	virtual void                        MarqueeSelect_Implementation();
	virtual void                        OnSelectStartedHandler_Implementation(FVector2D SelectStart);
	virtual void                        OnSelectUpdatedHandler_Implementation(FVector2D SelectUpdate);
	virtual void                        OnSelectEndedHandler_Implementation(FVector2D SelectEnd);
	virtual void                        ReceiveMarqueeSelect_Implementation(const TArray<AActor*>& Actors);
	virtual void                        ReceiveSingleSelect_Implementation(AActor* Actor);
	virtual int32                       GetActiveActor_Implementation() const;
	virtual void                        SetActiveActor_Implementation(int32 Index);
	virtual void                        NextActiveActor_Implementation();
	virtual bool                        IsActiveActorValid_Implementation();
	virtual FSAFVectorSet               GetSelectVectors_Implementation() const { return SelectVectors; }

	virtual bool                        SendOrder_Implementation(FSAFOrder Order, bool bQueueMode);
	virtual bool                        SendOrders_Implementation(const TArray<FSAFOrder>& Orders, bool bQueueMode);
	virtual void                        OnOrderStartedHandler_Implementation(FVector2D OrderStart);
	virtual void                        OnOrderUpdatedHandler_Implementation(FVector2D OrderUpdate);
	virtual void                        OnOrderEndedHandler_Implementation(FVector2D OrderEnd);
	virtual FSAFVectorSet               GetOrderVectors_Implementation() const { return OrderVectors; }

	virtual void                        PingLocation_Implementation(FVector Location);
	virtual void                        PingDrag_Implementation(FVector Start, FVector End);
	virtual void                        PingActor_Implementation(AActor* Actor);
	virtual void                        OnPingStartedHandler_Implementation(FVector2D PingStart);
	virtual void                        OnPingUpdatedHandler_Implementation(FVector2D PingUpdate);
	virtual void                        OnPingEndedHandler_Implementation(FVector2D PingEnd);
	virtual FSAFVectorSet               GetPingVectors_Implementation() const override { return PingVectors; }
	
	// Player Data / Properties
	// ========================================================================================================
	/** Set the desired player slot for multiplayer games. If 0, slot will be random.
	Note: player slots are downstream of teams (Team 1 has slot 1,2,3...) so if 
	DesiredTeamID is not set, this will do nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Input")
	int32 DesiredPlayerID = 0;

	/** Set the desired team slot for multiplayer games. If 0, team will be random. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Input")
	int32 DesiredTeamID = 0;

	/** Defines the distance from start and end points on a Select/Order/Ping action to 
	determine wether that action was a 'drag' action. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Input")
	float DragThreshold = 10.f;

	/** Defines far a line trace will go before ending without hitting anything. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Input")
	float TraceDistance = 1000000.f;

	/** Which collision channel to use when tracing under cursor. */
	UPROPERTY(EditDefaultsOnly, Category="SeinARTS|Input")
	TEnumAsByte<ECollisionChannel> CursorTraceChannel = ECC_Visibility;

	/** Contains the vector set corresponding to an ongoing or previous select action. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Selection")
	FSAFVectorSet SelectVectors;

	/** Contains the vector set corresponding to an ongoing or previous order action. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Order")
	FSAFVectorSet OrderVectors;

	/** Contains the vector set corresponding to an ongoing or previous ping action. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Ping")
	FSAFVectorSet PingVectors;

	// Internal Helpers
	// ==================================================================================================
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Selection")
	void LogSelection();

	/** Returns current mouse position in viewport space (0,0 = top-left). ZeroVector if unavailable. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Input")
	FVector2D GetMouseVector2D() const;

	/** Line trace from any inputted FVector2D screen position, returns hit location (or ZeroVector if no hit). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Input")
	FVector LineTraceUnderScreenPosition(
		FVector2D ScreenPosition, 
		float Distance, 
		TEnumAsByte<ECollisionChannel> TraceChannel, 
		bool bTraceComplex, 
		UPARAM(ref) AActor*& OutActor, 
		bool bPrintDebugMessages = true
	) const;

	/** Line trace from mouse cursor into the world; returns hit location (or ZeroVector if no hit). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Input")
	FVector LineTraceUnderCursor(
		UPARAM(ref) AActor*& OutActor, 
		bool bPrintDebugMessages = true
	) const { 
		return LineTraceUnderScreenPosition(GetMouseVector2D(), TraceDistance, CursorTraceChannel, false, OutActor, bPrintDebugMessages);
	}

	// C++ convenience overloads
	FVector LineTraceUnderScreenPosition(FVector2D ScreenPosition) const { AActor* Dummy=nullptr; return LineTraceUnderScreenPosition(ScreenPosition, TraceDistance, CursorTraceChannel, false, Dummy ); }
	FVector LineTraceUnderScreenPosition(FVector2D ScreenPosition, bool bPrintDebugMessages) const { AActor* Dummy=nullptr; return LineTraceUnderScreenPosition(ScreenPosition, TraceDistance, CursorTraceChannel, false, Dummy, bPrintDebugMessages ); }
	FVector LineTraceUnderScreenPosition(FVector2D ScreenPosition, AActor*& OutActor) const { return LineTraceUnderScreenPosition(ScreenPosition, TraceDistance, CursorTraceChannel, false, OutActor); }
	FVector LineTraceUnderScreenPosition(FVector2D ScreenPosition, AActor*& OutActor, bool bPrintDebugMessages) const { return LineTraceUnderScreenPosition(ScreenPosition, TraceDistance, CursorTraceChannel, false, OutActor, bPrintDebugMessages); }
	FVector LineTraceUnderCursor(bool bPrintDebugMessages) const { AActor* Dummy=nullptr; return LineTraceUnderScreenPosition(GetMouseVector2D(), TraceDistance, CursorTraceChannel, false, Dummy, bPrintDebugMessages); }
	FVector LineTraceUnderCursor() const { AActor* Dummy=nullptr; return LineTraceUnderScreenPosition(GetMouseVector2D(), TraceDistance, CursorTraceChannel, false, Dummy, false); }

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Replication
	// ==================================================================================================
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Networking")
	void MarkClientReady();

	UFUNCTION(Server, Reliable)
	void Server_ReportReady();
	void Server_ReportReady_Implementation();

	UFUNCTION(Server, Reliable)
	void Server_SendOrder(const FSAFOrder& Order, bool bQueueMode, const TArray<AActor*>& SelectionSnapshot);

	// Selection Properties
	// ==================================================================================================
	/** Current selection of actors. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="SeinARTS|Selection")
	TArray<TObjectPtr<AActor>> Selection;

	/** Temporary queue used during marquee selection and single-click selection. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="SeinARTS|Selection")
	TArray<TObjectPtr<AActor>> SelectionQueue;

	/** All actors owned by this player controller. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="SeinARTS|Selection")
	TArray<TObjectPtr<AActor>> Actors;

	/** The "active" actor index, e.g. primary selected actor in the selection. -1 indicates there 
	is no active Actor in the selection. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="SeinARTS|Selection")
	int32 ActiveActor = -1;

};
