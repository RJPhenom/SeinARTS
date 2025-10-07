#include "Classes/Unreal/SAFHUD.h"
#include "Classes/Unreal/SAFPlayerController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Enums/SAFCoverTypes.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/SAFPlayerInterface.h"
#include "Utils/SAFLibrary.h"
#include "Utils/SAFCoverUtilities.h"
#include "Debug/SAFDebugTool.h"
#include "DrawDebugHelpers.h"

ASAFHUD::ASAFHUD() {
	// TODO: Set defaults if needed (SelectionClassFilter = APawn::StaticClass(); etc.)
}

void ASAFHUD::BeginPlay() {
	Super::BeginPlay();
	ASAFPlayerController* SAFPC = Cast<ASAFPlayerController>(GetOwningPlayerController());
  if (!SAFPC) return;
  SAFPC->OnSelectStarted.AddDynamic(this, &ASAFHUD::BeginMarquee);
  SAFPC->OnSelectEnded.AddDynamic(this, &ASAFHUD::EndMarquee);
}

// Overrides the DrawHUD event handler to add C++ hooks. 
//
// Note: this handles *drawing* the marquee, not the logic of it. It does call
// GetActorsInSelectionRectangle when drawing, but this is because we must call this 
// function during a draw call to get accurate results. The actual results logic are
// scripted in the events dispatchers, which just dispatches them out for handling by
// a SAFPlayerController or other class.
void ASAFHUD::DrawHUD() {
	Super::DrawHUD();

  APlayerController* Controller = GetOwningPlayerController();
  if (!SAFLibrary::IsPlayerControllerPtrValidSeinARTSPlayer(Controller)) { 
    SAFDEBUG_WARNING("SAFHUD::DrawHUD override aborted: invalid player controller.");
    return; 
  }

  if (bDrawMarquee && Controller) {
    // Fetch the marquee end screen pos from controller
    const FSAFVectorSet SelectVectors = ISAFPlayerInterface::Execute_GetSelectVectors(Controller);
    MarqueeEnd = SelectVectors.End2D;

    // If marquee selecting
    if (FVector2D::Distance(MarqueeStart, MarqueeEnd) > MarqueeThreshold) {
      // Normalize rect
      const float X0 = FMath::Min(MarqueeStart.X, MarqueeEnd.X);
      const float Y0 = FMath::Min(MarqueeStart.Y, MarqueeEnd.Y);
      const float X1 = FMath::Max(MarqueeStart.X, MarqueeEnd.X);
      const float Y1 = FMath::Max(MarqueeStart.Y, MarqueeEnd.Y);
      const float W  = X1 - X0;
      const float H  = Y1 - Y0;

      // Borders + fill
      DrawLine(X0, Y0, X1, Y0, MarqueeBorderColor, MarqueeBorderThickness);
      DrawLine(X1, Y0, X1, Y1, MarqueeBorderColor, MarqueeBorderThickness);
      DrawLine(X1, Y1, X0, Y1, MarqueeBorderColor, MarqueeBorderThickness);
      DrawLine(X0, Y1, X0, Y0, MarqueeBorderColor, MarqueeBorderThickness);
      DrawRect(MarqueeColor, X0, Y0, W, H);

      // Run marquee selection logic
      const TSubclassOf<AActor> Filter = MarqueeClassFilter ? MarqueeClassFilter : TSubclassOf<AActor>(AActor::StaticClass());
      MarqueeSelectionResults.Reset();
      GetActorsInSelectionRectangle(Filter, MarqueeStart, MarqueeEnd, MarqueeSelectionResults, bMarqueeIncludesNonCollidingComponents, bMarqueeRequiresActorsBeFullyInclosed);
      ISAFPlayerInterface::Execute_ReceiveMarqueeSelect(Controller, MarqueeSelectionResults);

      // Simple debug visualization for marquee results
      if (UWorld* World = GetWorld()) {
        int32 Index = 0;
        for (AActor* Actor : MarqueeSelectionResults) {
          Index++;
          if (!SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) continue;
          const FVector Loc = Actor->GetActorLocation();
          DrawDebugSphere(World, Loc, 35.f, 8, FColor::Yellow, false, 0.f, 0, 2.f);
        }
      }

    // Else we must be single-click selecting (requires hit trace under cursor)
    } else {
      FHitResult Hit;
      const TEnumAsByte<ECollisionChannel> CursorTraceChannel = ISAFPlayerInterface::Execute_GetCursorTraceChannel(Controller);
      Controller->GetHitResultUnderCursor(CursorTraceChannel, true, Hit);
      MarqueeSelectionResults.Empty();
      MarqueeSelectionResults.Add(Hit.GetActor());
      ISAFPlayerInterface::Execute_ReceiveSingleSelect(Controller, MarqueeSelectionResults[0]);
    }
  } 
  
  else if (bDrawMarquee) SAFDEBUG_ERROR("Error during marquee draw: SAFPlayerControllerRef was null. SAFHUD.cpp initializes this reference on BeginPlay(), please check class overrides.");
}

// Initializes marquee state and sets the start point.
void ASAFHUD::BeginMarquee_Implementation(const FVector2D InStart) {
  SAFDEBUG_INFO("SAFHUD: BeginMarquee called.");
	MarqueeStart = InStart;
  bDrawMarquee = true;
  OnMarqueeStarted.Broadcast(InStart);
}

// Resets marquee state and returns true if the marquee 
// was drawn (drag distance exceeded the threshold).
void ASAFHUD::EndMarquee_Implementation(const FVector2D InEnd) {
  SAFDEBUG_INFO("SAFHUD: EndMarquee called.");
  bool bWasDrawn = FVector2D::Distance(MarqueeStart, MarqueeEnd) > MarqueeThreshold;
  bDrawMarquee = false;
  MarqueeStart = FVector2D::ZeroVector;
  MarqueeEnd = FVector2D::ZeroVector;
  MarqueeWidth = 0.f;
  MarqueeHeight = 0.f;
  MarqueeSelectionResults.Empty();
  OnMarqueeEnded.Broadcast(MarqueeEnd, bWasDrawn);
}

void ASAFHUD::DrawDestinations_Implementation(const TArray<FVector>& Destinations) {
  UWorld* World = GetWorld();
  if (!World) return;

  for (const FVector& Destination : Destinations) {
    FColor Color = FColor::Magenta;
    ESAFCoverType CoverType = SAFCoverUtilities::GetCoverAtPoint(World, Destination);
    if (CoverType == ESAFCoverType::Negative) Color = FColor::Red;
    if (CoverType == ESAFCoverType::Light) Color = FColor::Yellow;
    if (CoverType == ESAFCoverType::Heavy) Color = FColor::Green;
    DrawDebugPoint(World, Destination, 15.f, Color, false, 0.f);
  }
}