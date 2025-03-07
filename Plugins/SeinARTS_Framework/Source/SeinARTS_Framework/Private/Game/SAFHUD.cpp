


#include "Game/SAFHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"

void ASAFHUD::DrawHUD()
{
    Super::DrawHUD();

    if (DrawSelector) {
        GetOwningPlayerController()->GetMousePosition(SelectorW, SelectorH);
        
        // Only draw rect if it has substantial bounds
        DrawSelectorRect = FMath::Abs(SelectorX - SelectorW) > 5.0f && FMath::Abs(SelectorY - SelectorH) > 5.0f;

        if (DrawSelectorRect) {
            // Borders
            DrawLine(SelectorX, SelectorY, SelectorW, SelectorY, SelectorBorderColour); // top
            DrawLine(SelectorX, SelectorY, SelectorX, SelectorH, SelectorBorderColour); // left
            DrawLine(SelectorW, SelectorY, SelectorW, SelectorH, SelectorBorderColour); // right
            DrawLine(SelectorX, SelectorH, SelectorW, SelectorH, SelectorBorderColour); // bottom

            // Fill
            DrawRect(SelectorFillColour, SelectorX, SelectorY, SelectorW, SelectorH);
        }
        

    }

}

void ASAFHUD::ReceiveSelectorStarted() {
    DrawSelector = true;
    GetOwningPlayerController()->GetMousePosition(SelectorX, SelectorY);


}

TArray<UObject*> ASAFHUD::ReceiveSelectorEnded() {
    TArray<UObject*> SelectorItems;

    if (DrawSelectorRect) {
        SelectorItems = GetSAFUnitsInSelectorRect();
    }

    else {
        FVector WorldPos, WorldDir;
        GetOwningPlayerController()->DeprojectMousePositionToWorld(WorldPos, WorldDir);

        FVector TraceEnd = WorldPos + (WorldDir * TraceLength);
        FCollisionQueryParams QParams;

        FHitResult Hit;
        GetWorld()->LineTraceSingleByChannel(Hit, WorldPos, TraceEnd, ECC_Visibility, QParams);

        UPrimitiveComponent* HitComponent = Hit.GetComponent();
        if (HitComponent && (HitComponent->GetOwner()->IsA(ASAFObject::StaticClass())))
        {
            SelectorItems.Add(Hit.GetComponent());
        }
    }

    DrawSelector = false;
    return SelectorItems;
}

TArray<UObject*> ASAFHUD::GetSAFUnitsInSelectorRect() {
    TArray<UObject*> Objects;

    return Objects;
}
