


#include "Game/SAFHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "SAFISelection.h"
#include "GameFramework/HUD.h"
#include "SAFSelectionComponent.h"
#include "Components/WidgetComponent.h"

void ASAFHUD::DrawHUD()
{
    Super::DrawHUD();

    if (DrawSelector) {
        SelectorResults.Empty();
        GetOwningPlayerController()->GetMousePosition(SelectorW, SelectorH);
        
        // Only draw rect if it has substantial bounds
        DrawSelectorRect = FMath::Abs(SelectorX - SelectorW) > 5.0f && FMath::Abs(SelectorY - SelectorH) > 5.0f;

        if (DrawSelectorRect) {
            // Borders
            DrawLine(SelectorX, SelectorY, SelectorW, SelectorY, BorderColour); // top
            DrawLine(SelectorX, SelectorY, SelectorX, SelectorH, BorderColour); // left
            DrawLine(SelectorW, SelectorY, SelectorW, SelectorH, BorderColour); // right
            DrawLine(SelectorX, SelectorH, SelectorW, SelectorH, BorderColour); // bottom

            // Fill
            bool InvertW = SelectorX < SelectorW;
            bool InvertH = SelectorY < SelectorH;

            float RectX = InvertW ? SelectorW : SelectorX;
            float RectY = InvertH ? SelectorH : SelectorY;

            float RectW = InvertW ? (SelectorX - SelectorW) : (SelectorW - SelectorX);
            float RectH = InvertH ? (SelectorY - SelectorH) : (SelectorH - SelectorY);

            SelectorRect = FVector4(RectX, RectY, RectW, RectH);
            DrawRect(FillColour, SelectorRect.X, SelectorRect.Y, SelectorRect.Z, SelectorRect.W);
            SelectorResults = GetSelectablesInSelectorRect();
        }

        else {
            SelectorResults.Add(GetSelectableUnderCursor());
        }

        for (AActor* Actor : SelectorResults) {
            if (IsValid(Actor)) ISAFISelection::Execute_Highlight(Actor);
        }
    }

    else {
        if (AActor* Actor = GetSelectableUnderCursor()) ISAFISelection::Execute_Highlight(Actor);
    }
}

void ASAFHUD::ReceiveSelectorStarted() {
    APlayerController* Controller = GetOwningPlayerController();
    if (IsValid(Controller)) {
        Controller->SetShowMouseCursor(!HideCursorWhileSelecting);
        Controller->GetMousePosition(SelectorX, SelectorY);
    }

    DrawSelector = true;

    // Debug
    if (GEngine && EnableSelectorDebugMessages) GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Emerald, 
            FString::Printf(TEXT("Selector started at position %f, %f"), SelectorX, SelectorY));
}

TArray<AActor*> ASAFHUD::ReceiveSelectorEnded() {
    APlayerController* Controller = GetOwningPlayerController();
    if (IsValid(Controller)) {
        Controller->SetShowMouseCursor(true);
    }

    TArray<AActor*> Results = SelectorResults;
    DrawSelector = false;

    // Debug
    if (GEngine && EnableSelectorDebugMessages) GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Emerald, 
            FString::Printf(TEXT("Selector ended at %f, %f."), SelectorW, SelectorH));

    return Results;
}


AActor* ASAFHUD::GetSelectableUnderCursor() const {
    FVector WorldPos, WorldDir;
    GetOwningPlayerController()->DeprojectMousePositionToWorld(WorldPos, WorldDir);

    const FVector TraceEnd = WorldPos + (WorldDir * TraceLength);
    FCollisionQueryParams QParams;

    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(Hit, WorldPos, TraceEnd, ECC_Visibility, QParams);

    AActor* HitActor = Hit.GetActor();
    if (IsValid(HitActor)) {
        USAFSelectionComponent* SelectionComponent = HitActor->FindComponentByClass<USAFSelectionComponent>();
        if (SelectionComponent && SelectionComponent->Selectable) return HitActor;
    }

    return nullptr;
}

TArray<AActor*> ASAFHUD::GetSelectablesInSelectorRect() const {
    TArray<AActor*> Selectables;

    for (TActorIterator<AActor> Actor(GetWorld(), FilterByActor); Actor; ++Actor) {
        USAFSelectionComponent* SelectionComponent = Actor->FindComponentByClass<USAFSelectionComponent>();
        
        if (SelectionComponent && SelectionComponent->Selectable) {
            TSet<UActorComponent*> Components = Actor->GetComponents();
        
            for (UActorComponent* Component : Components) {
                if (IncludeBanners && SelectorIntersectsBannerBounds(Component)) Selectables.Add(*Actor);
                else if (SelectorIntersectsActorBounds(Component)) Selectables.Add(*Actor);
            }
        }
    }

    return Selectables;
}

bool ASAFHUD::SelectorIntersectsBannerBounds(const UActorComponent* Component) const {
    if (!IsValid(Component)) return false;
    if (const UWidgetComponent* WidgetComponent = Cast<UWidgetComponent>(Component)) {
        //TODO: if it is a banner type
        UUserWidget* Widget = WidgetComponent->GetWidget();

        const FGeometry& WidgetGeometry = Widget->GetCachedGeometry();
        const FVector2D WidgetPosition = WidgetGeometry.GetAbsolutePosition();
        const FVector2D WidgetSize = WidgetGeometry.GetLocalSize();
        const FVector4 WidgetRect(WidgetPosition.X, WidgetPosition.Y, WidgetPosition.X + WidgetSize.X, WidgetPosition.Y + WidgetSize.Y);

        return RectsIntersect(WidgetRect, SelectorRect);
    }

    return false;
}

bool ASAFHUD::SelectorIntersectsActorBounds(const UActorComponent* Component) const {
    // NOTE: This function is copied from Unreal's base HUD.cpp, then modified. In our 
    // version we build bounding boxes at component level. This is because sometimes the
    // actor bounds does not resize down properly if you have moving components using 
    // the default AHUD::GetActorsInSelectionRectangle().
    FBox2D SelectionRect(ForceInit);
    SelectionRect += FVector2D(SelectorRect.X, SelectorRect.Y);
    SelectionRect += FVector2D(SelectorRect.Z, SelectorRect.W);

    const FVector BoundsPointMapping[8] = {
        FVector(1.f, 1.f, 1.f),
        FVector(1.f, 1.f, -1.f),
        FVector(1.f, -1.f, 1.f),
        FVector(1.f, -1.f, -1.f),
        FVector(-1.f, 1.f, 1.f),
        FVector(-1.f, 1.f, -1.f),
        FVector(-1.f, -1.f, 1.f),
        FVector(-1.f, -1.f, -1.f)
    };

    if (Component->IsA(FilterByComponent)) {
        const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
        const bool ToQuery = (IsValid(Primitive) && Primitive->IsCollisionEnabled()) || Component && IncludingNonCollidingComponents;

        if (ToQuery) {
            const FBox Bounds = Component->GetStreamingBounds();
            const FVector Extents = Bounds.GetExtent();
            const FVector Center = Bounds.GetCenter();

            FBox2D Box(ForceInit);
            for (uint8 i = 0; i < 8; i++) {
                const FVector ProjectedVert = Project(Center + (BoundsPointMapping[i] * Extents), true);
                if (ProjectedVert.Z > 0.f) Box += FVector2D(ProjectedVert.X, ProjectedVert.Y);
            }

            if (Box.bIsValid) {
                if (UnitsMustBeFullyEnclosed) return SelectionRect.IsInside(Box);
                else return SelectionRect.Intersect(Box);
            }
        }
    }

    return false;
}

bool ASAFHUD::RectsIntersect(const FVector4& Rect1, const FVector4& Rect2) const {
    return Rect1.X < Rect2.Z && Rect1.Z > Rect2.X && Rect1.Y < Rect2.W && Rect1.W > Rect2.Y;
}
