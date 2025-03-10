


#include "Game/SAFHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "GameFramework/HUD.h"
#include "SAFObject.h"
#include "SAFUnitMember.h"

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
            bool invertW = SelectorX < SelectorW;
            bool invertH = SelectorY < SelectorH;

            float rectStartX = invertW ? SelectorW : SelectorX;
            float rectStartY = invertH ? SelectorH : SelectorY;

            float rectW = invertW ? (SelectorX - SelectorW) : (SelectorW - SelectorX);
            float rectH = invertH ? (SelectorY - SelectorH) : (SelectorH - SelectorY);

            DrawRect(SelectorFillColour, rectStartX, rectStartY, rectW, rectH);
        }
    }

}

void ASAFHUD::ReceiveSelectorStarted() {
    DrawSelector = true;
    GetOwningPlayerController()->GetMousePosition(SelectorX, SelectorY);
}

TArray<ASAFObject*> ASAFHUD::ReceiveSelectorEnded() {
    TArray<ASAFObject*> SelectorItems;

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
            SelectorItems.Add(HitComponent->GetOwner<ASAFObject>());
        }
    }

    DrawSelector = false;
    return SelectorItems;
}

TArray<ASAFObject*> ASAFHUD::GetSAFUnitsInSelectorRect() {
    TArray<ASAFObject*> Objects;

    return Objects;
}
