#include "Classes/Unreal/SAFPlayerController.h"
#include "Classes/Unreal/SAFPlayerState.h"
#include "Classes/Unreal/SAFHUD.h"
#include "Classes/Unreal/SAFCameraPawn.h"
#include "Classes/SAFFormationManager.h"
#include "Classes/Units/SAFSquad.h"
#include "CollisionQueryParams.h"
#include "InputCoreTypes.h"
#include "Interfaces/SAFAssetInterface.h"
#include "Interfaces/SAFPlayerInterface.h"
#include "Interfaces/Units/SAFUnitInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/KismetMathLibrary.h"
#include "Utils/SAFLibrary.h"
#include "Debug/SAFDebugTool.h"
#include "DrawDebugHelpers.h"

ASAFPlayerController::ASAFPlayerController() {
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.bStartWithTickEnabled = true;
}

void ASAFPlayerController::BeginPlay() {
	Super::BeginPlay();
  SetShowMouseCursor(true);
  if (IsLocalController()) MarkClientReady();

  OnSelectStarted.AddDynamic(this, &ASAFPlayerController::OnSelectStartedHandler);
  OnSelectEnded.AddDynamic(this, &ASAFPlayerController::OnSelectEndedHandler);
  OnOrderStarted.AddDynamic(this, &ASAFPlayerController::OnOrderStartedHandler);
  OnOrderEnded.AddDynamic(this, &ASAFPlayerController::OnOrderEndedHandler);
  OnPingStarted.AddDynamic(this, &ASAFPlayerController::OnPingStartedHandler);
  OnPingEnded.AddDynamic(this, &ASAFPlayerController::OnPingEndedHandler);
}

void ASAFPlayerController::Tick(float DeltaSeconds) {
  Super::Tick(DeltaSeconds);
  if (Selection.Num() <= 0) return;
  if (!ISAFPlayerInterface::Execute_IsMySelectionFriendly(this)) return;
  if (!ISAFPlayerInterface::Execute_IsActiveActorValid(this)) return;

  ASAFSquad* Squad = Cast<ASAFSquad>(Selection[0].Get());
  if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Squad)) return;

  if(ASAFHUD* SAFHUD = Cast<ASAFHUD>(GetHUD())) {
    const FVector Point = LineTraceUnderCursor();
    TArray<FVector> Destinations =  Squad->GetCoverPositionsAtPoint(Point, false);
    SAFHUD->DrawDestinations(Destinations);
  }
}

// ===========================================================================
//                           SAFPlayerInterface
// ===========================================================================

// Initializes the player (if networked, called after all players report ready and
// during StartMatch() step).
void ASAFPlayerController::InitPlayer_Implementation() {

}

// Returns true if the Queue button (default Shift) is currently held down.
// The queue button is used to add to selection or queue orders, instead of replacing them.
bool ASAFPlayerController::IsQueueButtonDown_Implementation() const {
	return IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
}

// Returns true if the Control button (default Ctrl) is currently held down.
// The Control button is used modify clicks and hotkeys to trigger a different
// set of acitons.
bool ASAFPlayerController::IsControlButtonDown_Implementation() const {
	return IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
}

// Returns true if the Alternate button (default Ctrl) is currently held down.
// The Alternate button is used modify UI controls and hotkeys to trigger different
// views and actions.
bool ASAFPlayerController::IsAlternateButtonDown_Implementation() const {
	return IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt);
}

// Returns the trace channel this controller will use when checking raycasts into the
// world for expected SeinARTS Framework functionality. 
// (e.g. what channel to check for hits when clicking for a select action?)
ECollisionChannel ASAFPlayerController::GetCursorTraceChannel_Implementation() const {
  return static_cast<ECollisionChannel>(CursorTraceChannel.GetValue());
}

// Gets the formation manager of the current selection. If the selection does
// not have one uniform formation manager, it creates one and transfers management
// of each unit it to the new manager.
ASAFFormationManager* ASAFPlayerController::GetCurrentFormation_Implementation(const TArray<AActor*>& SelectionSnapshot) {
  if (!HasAuthority()) { SAFDEBUG_WARNING("WARNING: GetCurrentFormation called on client. Formations exist only on the server, you should not be calling this on the client. Returning nullptr."); return nullptr; }
  if (SelectionSnapshot.Num() <= 0) { SAFDEBUG_WARNING("GetCurrentFormation failed: no units in current selection."); return nullptr; }

  // Collect valid unit actors that will make up the formation. 
  TArray<AActor*> SelectionActors;
  SelectionActors.Reserve(SelectionSnapshot.Num());
  for (AActor* Actor : SelectionSnapshot) if (SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) SelectionActors.Add(Actor);
  if (SelectionActors.Num() <= 0) { SAFDEBUG_WARNING("GetCurrentFormation failed: selection contains no valid units."); return nullptr; }
  else SAFDEBUG_INFO(FORMATSTR("GetCurrentFormation proceeding with check on %d units.", SelectionActors.Num()));

  // Find existing common formation (if any) and check if it matches 1:1 the selection, if so use that
  ASAFFormationManager* FoundFormation = Cast<ASAFFormationManager>(ISAFUnitInterface::Execute_GetFormation(SelectionActors[0]));
  TArray<AActor*> FoundActors = IsValid(FoundFormation) ? ISAFFormationInterface::Execute_GetActors(FoundFormation) : TArray<AActor*>();
  bool bActorCountsMatch = FoundActors.Num() == SelectionActors.Num();
  if (IsValid(FoundFormation) && bActorCountsMatch) {
    for (AActor* Actor : SelectionActors) {
      ASAFFormationManager* OldFormation = Cast<ASAFFormationManager>(ISAFUnitInterface::Execute_GetFormation(Actor));
      if (IsValid(OldFormation) && OldFormation == FoundFormation) continue;
      if (IsValid(OldFormation)) ISAFFormationInterface::Execute_RemoveActor(OldFormation, Actor);      
      ISAFFormationInterface::Execute_AddActor(FoundFormation, Actor);
    }

    return FoundFormation;
  }

  // Else, create a new formation with the selected units
  return ISAFPlayerInterface::Execute_CreateNewFormation(this, SelectionActors);
}

// Creates a new formation for the current selection.
ASAFFormationManager* ASAFPlayerController::CreateNewFormation_Implementation(const TArray<AActor*>& InActors) {
  if (!HasAuthority()) { SAFDEBUG_WARNING("WARNING: GetCurrentFormation called on client. Formations exist only on the server, you should not be calling this on the client. Returning nullptr."); return nullptr; }
  UWorld* World = GetWorld(); if (!World) { SAFDEBUG_ERROR("CreateNewFormation aborted: World was nullptr."); return nullptr; } 
  if (InActors.Num() <= 0) { SAFDEBUG_ERROR("CreateNewFormation aborted: no actors were provided."); return nullptr; } 

  // Spawn new manager at selection centroid
  FVector Center(0.f); 
  for (AActor* Actor : InActors) Center += Actor->GetActorLocation(); 
  Center /= InActors.Num();
  FActorSpawnParameters Params;
  Params.Owner = this;
  Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  ASAFFormationManager* NewFormation = World->SpawnActor<ASAFFormationManager>(ASAFFormationManager::StaticClass(), Center, FRotator::ZeroRotator, Params);
  if (!IsValid(NewFormation)) { SAFDEBUG_ERROR("CreateNewFormation failed: spawn returned nullptr."); return nullptr; }

  // Transfer membership
  for (AActor* Actor : InActors) {
    ASAFFormationManager* OldFormation = Cast<ASAFFormationManager>(ISAFUnitInterface::Execute_GetFormation(Actor));
    if (IsValid(OldFormation)) ISAFFormationInterface::Execute_RemoveActor(OldFormation, Actor);
    ISAFFormationInterface::Execute_AddActor(NewFormation, Actor);
  }

  return NewFormation;
}

// Checks if the current selection is entirely friendly (owned by this Player).
bool ASAFPlayerController::IsMySelectionFriendly_Implementation() const { 
  return SAFLibrary::IsSelectionFriendly(ISAFPlayerInterface::Execute_GetSelection(this), GetPlayerState<APlayerState>()); 
}

// Gets the current selection.
TArray<AActor*> ASAFPlayerController::GetSelection_Implementation() const {
  TArray<AActor*> OutSelection;
  OutSelection.Reserve(Selection.Num());
  for (const TObjectPtr<AActor>& Ptr : Selection) {
    AActor* Actor = Ptr.Get();
    if (SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) OutSelection.AddUnique(Actor);
  }

  return OutSelection;
}

// Sets the current selection to a new provided array of actors.
void ASAFPlayerController::SetSelection_Implementation(const TArray<AActor*>& InActors) {
	ISAFPlayerInterface::Execute_ClearSelection(this);

  for (AActor* Actor : InActors) {
    if (SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) {
      AActor* OutSelected = nullptr;
      bool bResult = false;
      ISAFAssetInterface::Execute_Select(Actor, OutSelected);

      // Check bResult to confirm if selection was successful
      if (!bResult) SAFDEBUG_WARNING(FORMATSTR("SetSelection failed on actor '%s': Select_Implementation returned false.", Actor ? *Actor->GetName() : TEXT("nullptr")));
    } else SAFDEBUG_WARNING(FORMATSTR("SetSelection skipped actor '%s': invalid actor.", Actor ? *Actor->GetName() : TEXT("nullptr")));
  }

  SAFDEBUG_SUCCESS("Selection set successfully!");
  OnSelectionUpdated.Broadcast(ISAFPlayerInterface::Execute_GetSelection(this));
}

// Processes the queue into the active selection.
void ASAFPlayerController::ProcessSelectionQueue_Implementation(bool bWasMarqueeDrawn) {
  // Check to clear first (we need to always clear selection on empty/non-queue select actions)
	if (!ISAFPlayerInterface::Execute_IsQueueButtonDown(this) || !ISAFPlayerInterface::Execute_IsMySelectionFriendly(this)) ISAFPlayerInterface::Execute_ClearSelection(this);
  if (SelectionQueue.Num() == 0) { SAFDEBUG_INFO("ProcessSelectionQueue called with empty SelectionQueue. Process discarded."); return; }

  // Handle selection
  if (bWasMarqueeDrawn) ISAFPlayerInterface::Execute_MarqueeSelect(this);
  else ISAFPlayerInterface::Execute_SingleSelect(this);

  // Cleanup and logging
  ISAFPlayerInterface::Execute_ClearSelectionQueue(this);
  if (Selection.Num() > 0 && ActiveActor < 0) ISAFPlayerInterface::Execute_SetActiveActor(this, 0);
  LogSelection();
}

// Clears the current selection.
void ASAFPlayerController::ClearSelection_Implementation() {
	for (AActor* Actor : Selection) {
    if (SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) ISAFAssetInterface::Execute_Deselect(Actor);
    else SAFDEBUG_WARNING(FORMATSTR("ClearSelection skipped actor '%s': invalid actor.", Actor ? *Actor->GetName() : TEXT("nullptr")));
  }

  SAFDEBUG_SUCCESS("Selection cleared successfully!");
  Selection.Empty();
  ISAFPlayerInterface::Execute_SetActiveActor(this, -1);
  OnSelectionUpdated.Broadcast(ISAFPlayerInterface::Execute_GetSelection(this));
}

// Clears the queued selection (marquee, etc.).
void ASAFPlayerController::ClearSelectionQueue_Implementation() {
  for (AActor* Actor : SelectionQueue) if (SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) ISAFAssetInterface::Execute_DequeueSelect(Actor);
  SelectionQueue.Empty();
}

// Attempts to select a single actor, returns true if successful.
bool ASAFPlayerController::Select_Implementation(AActor* Actor) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) { SAFDEBUG_WARNING(FORMATSTR("Select failed on actor '%s': OutSelected is invalid.", Actor ? *Actor->GetName() : TEXT("nullptr"))); return false; }

	AActor* OutSelected = nullptr;
	const bool bActorSelected = ISAFAssetInterface::Execute_Select(Actor, OutSelected);
  if (!bActorSelected) { SAFDEBUG_WARNING(FORMATSTR("Select failed on actor '%s': Select_Implementation returned false.", Actor ? *Actor->GetName() : TEXT("nullptr"))); return false; }

  SAFDEBUG_SUCCESS("Select succeeded!");
  Selection.AddUnique(OutSelected);
  return true;
}

// Attempts to queue-select a single actor, returns true if successful.
// No logging since this can be called on tick.
bool ASAFPlayerController::QueueSelect_Implementation(AActor* Actor) {
  if (!SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) return false;

	AActor* OutQueueSelected = nullptr;
	const bool bActorQueueSelected = ISAFAssetInterface::Execute_QueueSelect(Actor, OutQueueSelected);
  if (!bActorQueueSelected || !IsValid(OutQueueSelected)) return false;
  SelectionQueue.AddUnique(OutQueueSelected);
  return true;
}

// Handles finalization of single-click selection.
void ASAFPlayerController::SingleSelect_Implementation() {
	if (SelectionQueue.Num() == 0) { 
    SAFDEBUG_WARNING("SingleSelect called with empty SelectionQueue. You should be blocking this execution during validation at the ProcessSelectionQueue step of the control flow."); 
    return; 
  }

	AActor* Actor = SelectionQueue[0];
	if (SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) ISAFPlayerInterface::Execute_Select(this, Actor);
  SAFDEBUG_SUCCESS("SingleSelect succeeded!");
  OnSelectionUpdated.Broadcast(ISAFPlayerInterface::Execute_GetSelection(this));
}

// Handles finalization of marquee selection.
void ASAFPlayerController::MarqueeSelect_Implementation() {
  if (SelectionQueue.Num() == 0) { 
    SAFDEBUG_WARNING("MarqueeSelect called with empty SelectionQueue. You should be blocking this execution during validation at the ProcessSelectionQueue step of the control flow."); 
    return; 
  }

	// Filter queue to only units owned by this player (you cannot marquee-select enemy units)
	const TArray<AActor*> Filtered = SAFLibrary::FilterSelectionByPlayer(SelectionQueue, GetPlayerState<APlayerState>(), false);
	for (AActor* Actor : Filtered) {
    AActor* OutSelected = nullptr;
		if (ISAFAssetInterface::Execute_Select(Actor, OutSelected)) {
			Selection.AddUnique(Actor);
		} else {
      SAFDEBUG_WARNING(FORMATSTR("MarqueeSelect failed on actor '%s': Select returned false.", *Actor->GetName()));
    }
	}

  SAFDEBUG_SUCCESS("MarqueeSelect succeeded!");
  OnSelectionUpdated.Broadcast(ISAFPlayerInterface::Execute_GetSelection(this));
}

// Handles starting a select action by assigning start vectors.
void ASAFPlayerController::OnSelectStartedHandler_Implementation(FVector2D SelectStart) {
  SelectVectors.Start2D = SelectStart;
  SelectVectors.Start = LineTraceUnderScreenPosition(SelectStart);
  SAFDEBUG_INFO("Select started.");
}

// Handles updating select end vectors during select drag/draw.
void ASAFPlayerController::OnSelectUpdatedHandler_Implementation(FVector2D SelectUpdate) {
  SelectVectors.End2D = SelectUpdate;
  SelectVectors.End = LineTraceUnderScreenPosition(SelectUpdate, false);
}

// Handles selecting at end of select input action.
void ASAFPlayerController::OnSelectEndedHandler_Implementation(FVector2D SelectEnd) {
  SelectVectors.End2D = SelectEnd;
  SelectVectors.End = LineTraceUnderScreenPosition(SelectEnd, false);
  SAFDEBUG_INFO("Select ended.");
  const bool bWasMarqueeDrawn = FVector2D::Distance(SelectVectors.Start2D, SelectVectors.End2D) > DragThreshold;
  ISAFPlayerInterface::Execute_ProcessSelectionQueue(this, bWasMarqueeDrawn);
}

// Receives a marquee (box) selection, usually from a SAFHUD, and tries to queue it for finalization.
// No logging since this can be called on tick.
void ASAFPlayerController::ReceiveMarqueeSelect_Implementation(const TArray<AActor*>& InActors) {
	ISAFPlayerInterface::Execute_ClearSelectionQueue(this);
  TArray<AActor*> FilteredActors = SAFLibrary::FilterSelectionByPlayer(InActors, GetPlayerState<APlayerState>(), false);
  for (AActor* Actor : FilteredActors) {
    if (ISAFAssetInterface::Execute_GetMultiSelectable(Actor))
      ISAFPlayerInterface::Execute_QueueSelect(this, Actor);
  }
}

// Receives a single-click selection, usually from a SAFHUD, and tries to queue it for finalization.
// No logging since this can be called on tick.
void ASAFPlayerController::ReceiveSingleSelect_Implementation(AActor* Actor) {
  if (!SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) { SAFDEBUG_WARNING("ReceiveSingleSelect called on an invalid actor. Discarding."); return; }
  if (ISAFAssetInterface::Execute_GetMultiSelectable(Actor) || SelectionQueue.Num() <= 0) {
    ISAFPlayerInterface::Execute_ClearSelectionQueue(this);
    ISAFPlayerInterface::Execute_QueueSelect(this, Actor);
  }
}

// Return the “active” unit index (e.g., primary selected actor).
int32 ASAFPlayerController::GetActiveActor_Implementation() const {
  return ActiveActor;
}

// Increments the “active” unit index (e.g., primary selected actor).
void ASAFPlayerController::SetActiveActor_Implementation(int32 Index) {
  ActiveActor = Index;
}

// Returns true or false based on if the ActiveActor is a valid index in the selection.
bool ASAFPlayerController::IsActiveActorValid_Implementation() {
  return ActiveActor >= 0 && ActiveActor < Selection.Num();
}

// Return the “active” unit (e.g., primary selected actor).
void ASAFPlayerController::NextActiveActor_Implementation() {
  if (Selection.Num() <= 0) { SAFDEBUG_ERROR("NextActiveActor aborted: tried to advance active unit, but selection is empty."); return; }
  if (++ActiveActor >= Selection.Num()) ActiveActor = -1;
}

// Sends an order to all currently selected units (if selection is friendly).
bool ASAFPlayerController::SendOrder_Implementation(FSAFOrder Order, bool bQueueMode) {
	if (!ISAFPlayerInterface::Execute_IsMySelectionFriendly(this)) { SAFDEBUG_INFO("SendOrders fizzled: current selection is not entirely friendly."); return false; }

  TArray<AActor*> SelectionSnapshot = ISAFPlayerInterface::Execute_GetSelection(this);
  Server_SendOrder(Order, bQueueMode, SelectionSnapshot);
  OnOrderIssued.Broadcast(Order, SelectionSnapshot);
  return true;
}

// Sends a list of orders to the current selection.
bool ASAFPlayerController::SendOrders_Implementation(const TArray<FSAFOrder>& Orders, bool bQueueMode) {
  int32 Sent = 0;
  for (const FSAFOrder& Order : Orders) Sent += ISAFPlayerInterface::Execute_SendOrder(this, Order, bQueueMode) ? 1 : 0;
  if (Sent > 0) SAFDEBUG_SUCCESS(FORMATSTR("SendOrders succeeded: sent %d out of %d orders.", Sent, Orders.Num()));
  else SAFDEBUG_WARNING(FORMATSTR("SendOrders sent 0 orders out of %d. There may have been an issue with the Orders param", Orders.Num()));
  return Sent > 0;
}

// SERVER RPC: Handles the execution of actual orders, which only happens on the server
void ASAFPlayerController::Server_SendOrder_Implementation(const FSAFOrder& Order, bool bQueueMode, const TArray<AActor*>& SelectionSnapshot) {
  if (SelectionSnapshot.Num() <= 0) { SAFDEBUG_WARNING("Server_SendOrder called on empty selection. Discarding."); return; }

  // Validation/cleaning
  TArray<AActor*> CleanedSnapshot;
  CleanedSnapshot.Reserve(SelectionSnapshot.Num());
  for (AActor* Actor : SelectionSnapshot) {
    if (!SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) continue;

    APlayerState* ActorOwner = ISAFAssetInterface::Execute_GetOwningPlayer(Actor);
    APlayerState* CallerOwner = GetPlayerState<APlayerState>();
    if (ActorOwner != CallerOwner) continue;
    CleanedSnapshot.AddUnique(Actor);
  }

  // Formation management
  ASAFFormationManager* Formation = Cast<ASAFFormationManager>(ISAFPlayerInterface::Execute_GetCurrentFormation(this, CleanedSnapshot));
  if (CleanedSnapshot.Num() <= 0) { SAFDEBUG_WARNING("Server_SendOrder had no units to order after sanitizing selection. Discarding."); return; }
  if (!Formation) { SAFDEBUG_WARNING("Server_SendOrder was unable to find or generate a formation to issue orders. Aborting."); return; }

  // Execution
  const bool bReceived = ISAFFormationInterface::Execute_ReceiveOrder(Formation, bQueueMode, Order);
  if (bReceived) SAFDEBUG_SUCCESS("Server_SendOrder: order sent successfully.");
  else SAFDEBUG_WARNING("Server_SendOrder: order rejected by formation.");
}

// Handles starting a order action by assigning start vectors
void ASAFPlayerController::OnOrderStartedHandler_Implementation(FVector2D OrderStart) {
  OrderVectors.Start2D = OrderStart;
  OrderVectors.Start = LineTraceUnderScreenPosition(OrderStart, false);
  SAFDEBUG_INFO("Order started.");
}

// Handles updating order end vectors during order drag/draw.
void ASAFPlayerController::OnOrderUpdatedHandler_Implementation(FVector2D OrderUpdate) {
  OrderVectors.End2D = OrderUpdate;
  OrderVectors.End = LineTraceUnderScreenPosition(OrderUpdate, false);
}

// Handles ordering at end of order input action
void ASAFPlayerController::OnOrderEndedHandler_Implementation(FVector2D OrderEnd) {
  AActor* TargetActor = nullptr;
  OrderVectors.End2D = OrderEnd;
  OrderVectors.End = LineTraceUnderScreenPosition(OrderEnd, TargetActor, false);
  SAFDEBUG_INFO("Order ended.");

  // Note: Tag is null at creation on purpose. It is up to the unit or the formation to determine 
  // what kind of order it is. Think from the player's PoV: an 'order' is just a RMB click. The 
  // unit should 'know' what to do about it.
  bool bValidTarget = SAFLibrary::IsActorPtrValidSeinARTSActor(TargetActor);
  FSAFOrder Order(
    bValidTarget ? TargetActor : nullptr, 
    bValidTarget ? ISAFAssetInterface::Execute_GetAsset(TargetActor) : nullptr,
    OrderVectors.Start, 
    OrderVectors.End);
  bool bQueueMode = ISAFPlayerInterface::Execute_IsQueueButtonDown(this);
  ISAFPlayerInterface::Execute_SendOrder(this, Order, bQueueMode);
}

// Pings a specific location
void ASAFPlayerController::PingLocation_Implementation(FVector Location) {
  if (UWorld* World = GetWorld()) {
    DrawDebugPoint(World, Location, 25.0f, FColor::Magenta, false, 2.0f);
    DrawDebugString(World, Location, TEXT("Ping!"), nullptr, FColor::Magenta, 2.0f, true, 1.5f);
    OnPingIssued.Broadcast(PingVectors.Start, Location, nullptr);
  } else SAFDEBUG_ERROR("PingLocation() failed: World is nullptr.");
}

// Draws a directional ping arrow from Start to End
void ASAFPlayerController::PingDrag_Implementation(FVector Start, FVector End) {
  if (UWorld* World = GetWorld()) {
    DrawDebugDirectionalArrow(GetWorld(), Start, End, 100.f, FColor::Magenta, false, 5.f, 0, 2.f);
    DrawDebugString(World, End, TEXT("Ping!"), nullptr, FColor::Magenta, 2.0f, true, 1.5f);
    OnPingIssued.Broadcast(Start, End, nullptr);
  } else SAFDEBUG_ERROR("PingDrag() failed: World is nullptr.");
}

// Pings a targetable actor
void ASAFPlayerController::PingActor_Implementation(AActor* Actor) {
  if (!SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) { SAFDEBUG_WARNING("PingActor tried to ping an invalid actor."); return; }
  FVector Location = Actor->GetActorLocation();

  if (UWorld* World = GetWorld()) {
  DrawDebugPoint(World, Location, 25.0f, FColor::Magenta, false, 2.0f);
  DrawDebugString(World, Location, FORMATSTR("Pinged actor '%s'!", *Actor->GetName()), nullptr, FColor::Magenta, 2.0f, true, 1.5f);
  OnPingIssued.Broadcast(PingVectors.Start, Location, Actor);
  } else SAFDEBUG_ERROR("PingActor() failed: World is nullptr.");
}

// Handles starting a ping action by assigning start vector
void ASAFPlayerController::OnPingStartedHandler_Implementation(FVector2D PingStart) {
  PingVectors.Start2D = PingStart;
  PingVectors.Start = LineTraceUnderScreenPosition(PingStart, false);
  SAFDEBUG_INFO("Ping started.");
}

// Handles updating ping end vectors during ping drag/draw.
void ASAFPlayerController::OnPingUpdatedHandler_Implementation(FVector2D PingUpdate) {
  PingVectors.End2D = PingUpdate;
  PingVectors.End = LineTraceUnderScreenPosition(PingUpdate, false);
}

// Handles pinging at end of ping input action
void ASAFPlayerController::OnPingEndedHandler_Implementation(FVector2D PingEnd) {
  AActor* PingedActor = nullptr;
  PingVectors.End2D = PingEnd;
  PingVectors.End = LineTraceUnderScreenPosition(PingEnd, PingedActor, false);
  SAFDEBUG_INFO("Ping ended.");

  if (SAFLibrary::IsActorPtrValidSeinARTSActor(PingedActor)) {
    SAFDEBUG_INFO(FORMATSTR("Pinged actor '%s'!", PingedActor ? *PingedActor->GetName() : TEXT("nullptr")));
    ISAFPlayerInterface::Execute_PingActor(this, PingedActor);
  } else if (FVector2D::Distance(PingVectors.Start2D, PingVectors.End2D) < DragThreshold) {
    SAFDEBUG_INFO("Ping!");
    ISAFPlayerInterface::Execute_PingLocation(this, PingVectors.End);
  } else {
    SAFDEBUG_INFO(FORMATSTR("Drag-ping drawn from start X:'%0.f' Y:'%0.f' to end X:'%0.f' Y:'%0.f'!", 
      PingVectors.Start2D.X, 
      PingVectors.Start2D.Y, 
      PingVectors.End2D.X, 
      PingVectors.End2D.Y));
    ISAFPlayerInterface::Execute_PingDrag(this, PingVectors.Start, PingVectors.End);
  }
}

// ===========================================================================
//                                    Input
// ===========================================================================

// Returns current mouse position in viewport space (0,0 = top-left). ZeroVector if unavailable.
FVector2D ASAFPlayerController::GetMouseVector2D() const {
	float X = 0.f, Y = 0.f;
	GetMousePosition(X, Y);
  return FVector2D(X, Y);
}

// Line trace at some position in screen space, getting whats under that point.
FVector ASAFPlayerController::LineTraceUnderScreenPosition(FVector2D ScreenPosition, float Distance, TEnumAsByte<ECollisionChannel> TraceChannel, bool bTraceComplex, AActor*& OutActor, bool bPrintDebugMessages) const {
  FVector Origin, Dir;
  if (!DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, Origin, Dir)) { 
    if (bPrintDebugMessages) SAFDEBUG_WARNING("LineTraceUnderScreenPosition failed: deprojection failed. Returning ZeroVector"); 
    return FVector::ZeroVector; 
  }

  FHitResult Hit;
  const FVector Start = Origin;
  const FVector End = Origin + Dir * Distance;
  FCollisionQueryParams Params(SCENE_QUERY_STAT(LineTraceUnderCursor), bTraceComplex);
  Params.AddIgnoredActor(this);
  if (UWorld* World = GetWorld()) {
    World->LineTraceSingleByChannel(Hit, Start, End, TraceChannel, Params);
    if (!Hit.bBlockingHit && bPrintDebugMessages) SAFDEBUG_WARNING("LineTraceUnderScreenPosition failed: line trace missed. Returning ZeroVector");
    else if (bPrintDebugMessages) SAFDEBUG_INFO(FORMATSTR("LineTraceUnderScreenPosition fired at X: %.0f, Y: %.0f.", ScreenPosition.X, ScreenPosition.Y));
    OutActor = Hit.GetActor();
    return Hit.bBlockingHit ? Hit.Location : FVector::ZeroVector;
  } else { SAFDEBUG_ERROR("LineTraceUnderScreenPosition failed: World was nullptr. Returning ZeroVector"); return FVector::ZeroVector; }
}

// ===========================================================================
//                                Selection
// ===========================================================================

// Logs current selection
void ASAFPlayerController::LogSelection() {
	 SAFDEBUG_INFO(FORMATSTR("Selection logged. New selection of '%d' units.", Selection.Num()));
}

// ===========================================================================
//                                Networking
// ===========================================================================

// Called on the client to mark readiness to start the match.
void ASAFPlayerController::MarkClientReady() {
  OnClientReady.Broadcast();
  SAFDEBUG_INFO(FORMATSTR("Player '%s' client is ready.", *GetName()));
  if (IsLocalController()) Server_ReportReady();
}

void ASAFPlayerController::Server_ReportReady_Implementation() {
  ASAFPlayerState* SAFPlayerState = GetPlayerState<ASAFPlayerState>();
  if (!IsValid(SAFPlayerState)) { SAFDEBUG_WARNING("Server_ReportReady aborted: invalid PlayerState."); return; }
  if (!SAFPlayerState->bIsReady) {
    SAFPlayerState->SetReady();
    SAFDEBUG_INFO(FORMATSTR("Player '%s' server is ready.", *GetName()));
    OnServerReady.Broadcast(this);
  } return;
}