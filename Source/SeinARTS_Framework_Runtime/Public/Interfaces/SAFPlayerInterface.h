#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Structs/SAFOrder.h"
#include "Structs/SAFVectorSet.h"
#include "SAFPlayerInterface.generated.h"

class AActor;
class ASAFFormationManager;

UINTERFACE(Blueprintable)
class USAFPlayerInterface : public UInterface { GENERATED_BODY() };

/**
 * SAFPlayerInterface
 * 
 * The SAFPlayerInterface is the primary interface for communicating with players in the SeinARTS Framework.
 * It provides methods for selection management, order flow, pings, and other utilities.
 */
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFPlayerInterface {

    GENERATED_BODY()

public:

    // Initialization
    // =====================================================================================
    /** Initializes the player interface. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Networking")
    void InitPlayer();

    // Input
    // =====================================================================================
    /** Returns true if the Queue button (default Shift) is currently held down. The queue 
     * button is used to add to selection or queue orders, instead of replacing them. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Input")
    bool IsQueueButtonDown() const;

    /** Returns true if the Control button (default Ctrl) is currently held down. The Control 
     * button is used modify clicks and hotkeys to trigger a different set of actions. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Input")
    bool IsControlButtonDown() const;

    /** Returns true if the Alternate button (default Alt) is currently held down. The 
     * Alternate button is used modify UI controls and hotkeys to trigger different views 
     * and actions. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Input")
    bool IsAlternateButtonDown() const;

    /** Returns the trace channel this controller will use when checking raycasts into the 
     * world for expected SeinARTS Framework functionality. (e.g. what channel to check for 
     * hits when clicking for a select action?) */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Input")
    ECollisionChannel GetCursorTraceChannel() const;

    // Formations
    // =====================================================================================
    /** SERVER ONLY: Gets the formation manager of the current selection. If the selection 
     * does not have one uniform formation manager, it creates one and transfers management 
     * of each actor to the new manager. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
    ASAFFormationManager* GetCurrentFormation(const TArray<AActor*>& SelectionSnapshot);

    /** SERVER ONLY: Creates a new formation for the current selection. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Formation")
    ASAFFormationManager* CreateNewFormation(const TArray<AActor*>& InActors);

    // Selection
    // =====================================================================================
    /** Checks if the current selection is entirely friendly (owned by this Player). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    bool IsMySelectionFriendly() const;

    /** Gets the current selection array. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    TArray<AActor*> GetSelection() const;

    /** Sets the current selection to a new provided array of actors. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void SetSelection(const TArray<AActor*>& InSelection);

    /** Clears the current selection. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void ClearSelection();

    /** Clears the current selection queue. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void ClearSelectionQueue();

    /** Processes the selection queue into the active selection once finalized. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void ProcessSelectionQueue(bool bWasMarqueeDrawn);

    /** Attempts to select a single actor, returns true if successful. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    bool Select(AActor* Actor);

    /** Attempts to queue-select a single actor, returns true if successful. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    bool QueueSelect(AActor* Actor);

    /** Handles finalization of single-click selection. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void SingleSelect();

    /** Handles finalization of marquee selection. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void MarqueeSelect();

    /** Handles the select start. On a SAFPlayerController, this runs to mark the start 2D 
     * and 3D mouse positions so when select ends (select button is lifted) the controller 
     * knows if it was a drag-select or single-select. */
    UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Selection")
    void OnSelectStartedHandler(FVector2D SelectStart);

    /** Handles updates to the select action. On a SAFPlayerController, this runs on tick 
     * while the select button is held to track the end/travel of the select. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Input")
    void OnSelectUpdatedHandler(FVector2D SelectUpdate);

    /** Handles the select end. On a SAFPlayerController, this runs to mark the end 2D 
     * and 3D mouse positions of the select action so that the controller can then run 
     * a drag-select or single-select. */
    UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Selection")
    void OnSelectEndedHandler(FVector2D SelectEnd);

    /** Called when a marquee (box) selection is finalized. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void ReceiveMarqueeSelect(const TArray<AActor*>& Actors);

    /** Called when a single-click selection is finalized. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void ReceiveSingleSelect(AActor* Actor);

    /** Return the “active” actor (e.g., primary selected actor, if any). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    int32 GetActiveActor() const;

    /** Sets the “active” actor (e.g., primary selected actor, if any). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void SetActiveActor(int32 Index);

    /** Change ActiveActor to the next actor in the selection if possible. If index is 
     * out of range reset and clear the ActiveActor. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    void NextActiveActor();

    /** Returns true or false based on if the ActiveActor is a valid index in the 
     * selection. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    bool IsActiveActorValid();

    /** Returns a vector set for the select vector containers on the PlayerController, 
     * if any. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Selection")
    FSAFVectorSet GetSelectVectors() const;

    // Orders
    // =====================================================================================
    /** Sends an order to the current selection. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
    bool SendOrder(FSAFOrder Order, bool bQueueMode);

    /** Sends a list of orders to the current selection. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
    bool SendOrders(const TArray<FSAFOrder>& Orders, bool bQueueMode);

    /** Handles the order start. On a SAFPlayerController, this runs to mark the start 
     * 2D and 3D mouse positions so when order ends (order button is lifted) the controller 
     * knows if it was a drag-order, actor-targeted order, or click-order. */
    UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Orders")
    void OnOrderStartedHandler(FVector2D OrderStart);

    /** Handles updates to the order action. On a SAFPlayerController, this runs on tick 
     * while the order button is held to track the end/travel of the order. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Input")
    void OnOrderUpdatedHandler(FVector2D OrderUpdate);

    /** Handles the order end. On a SAFPlayerController, this runs to mark the end 
     * 2D and 3D mouse positions so that the controller can then execute a drag-order, 
     * actor-targeted order, or click-order. */
    UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Orders")
    void OnOrderEndedHandler(FVector2D OrderEnd);

    /** Returns a vector set for the order vector containers on the PlayerController, 
     * if any. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Orders")
    FSAFVectorSet GetOrderVectors() const;

    // Pings
    // =====================================================================================
    /** Pings a given location. Defaults to a drawn debug point. This function should be 
     * overridden in Blueprint to provide custom ping effects. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Pings")
    void PingLocation(FVector Location);

    /** Pings an arrow from Start to End. This can be overridden for additional or custom 
     * drag-pinging effect for your game. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Pings")
    void PingDrag(FVector Start, FVector End);

    /** Ping a specific actor. Defaults to a drawn debug point and string with actor name. 
     * This function should be overridden in a Blueprint to provide custom ping effects. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Pings")
    void PingActor(AActor* Actor);

    /** Handles the ping start. On a SAFPlayerController, this runs to mark the start 
     * 2D and 3D mouse positions so that when ping ends (ping button is lifted) the controller 
     * knows if it was a drag-ping, actor ping, or click-ping. */
    UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Orders") 
    void OnPingStartedHandler(FVector2D PingStart);

    /** Handles updates to the ping action. On a SAFPlayerController, this runs on tick 
     * while the ping button is held to track the end/travel of the ping. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Input")
    void OnPingUpdatedHandler(FVector2D PingUpdate);

    /** Handles the ping end. On a SAFPlayerController, this runs to mark the end 
     * 2D and 3D mouse positions so that the controller can execute a drag-ping, actor ping, 
     * or click-ping. */
    UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Orders") 
    void OnPingEndedHandler(FVector2D PingEnd);

    /** Returns a vector set for the ping vector containers on the PlayerController, 
     * if any. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Pings")
    FSAFVectorSet GetPingVectors() const;

};