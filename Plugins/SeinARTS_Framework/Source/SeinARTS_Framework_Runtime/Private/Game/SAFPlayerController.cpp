


#include "Game/SAFPlayerController.h"
#include "EnhancedInput/Public/InputActionValue.h"
#include "SAFSelectionComponent.h"
#include "SAFHUD.h"
#include "SAFISelection.h"
#include "SAFIOrder.h"

ASAFPlayerController::ASAFPlayerController()
{
	
}

void ASAFPlayerController::BeginPlay() {
	Super::BeginPlay();

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (IsValid(Subsystem)) Subsystem->AddMappingContext(DefaultMappingContext, 0);

	SetShowMouseCursor(true);
}

void ASAFPlayerController::SetupInputComponent() {
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent)) {

		// Action Bindings
		EnhancedInputComponent->BindAction(SelectionAction, ETriggerEvent::Started, this, &ASAFPlayerController::StartSelector);
		EnhancedInputComponent->BindAction(SelectionAction, ETriggerEvent::Completed, this, &ASAFPlayerController::EndSelector);
		EnhancedInputComponent->BindAction(SelectionAction, ETriggerEvent::Canceled, this, &ASAFPlayerController::EndSelector);
		EnhancedInputComponent->BindAction(OrderAction, ETriggerEvent::Started, this, &ASAFPlayerController::StartSelector);

		// Action Values
		ShiftCommandBinding = &EnhancedInputComponent->BindActionValue(ShiftCommandAction);
		AlternateBinding = &EnhancedInputComponent->BindActionValue(AlternateAction);
		ControlBinding = &EnhancedInputComponent->BindActionValue(ControlAction);
	}

}

void ASAFPlayerController::Select(TArray<AActor*> Targets, bool Additive, bool Subtractive) {
	if (!Additive && !Subtractive) EmptySelection();

	TArray<AActor*> SelectableActors;
	for (AActor* Actor : Targets) {
		USAFSelectionComponent* SelectionComponent = nullptr;
		if (IsValid(Actor)) SelectionComponent = Actor->FindComponentByClass<USAFSelectionComponent>();

		if (SelectionComponent) {
			SelectableActors.Add(Actor);
			SelectionComponent->OnSelect(this);

			// Debug
			if (GEngine && EnableSelectorDebugMessages) {
				FString Msg = FString::Printf(TEXT("Added actor '%s' to the selection."), *Actor->GetName());
				GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Cyan, Msg);
			}
		}
	};

	if (Subtractive) Deselect(SelectableActors);
	else if (Additive) Selection.Append(SelectableActors);
	else Selection = SelectableActors;
}

void ASAFPlayerController::Deselect(TArray<AActor*> Targets) {
	for (AActor* Actor : Targets) {
		USAFSelectionComponent* SelectionComponent = nullptr;
		if (IsValid(Actor)) SelectionComponent = Actor->FindComponentByClass<USAFSelectionComponent>();

		if (SelectionComponent) {
			SelectionComponent->OnDeselect(this);
			Selection.Remove(Actor);

			// Debug
			if (GEngine && EnableSelectorDebugMessages) {
				FString Msg = FString::Printf(TEXT("Removed actor '%s' from the selection."), *Actor->GetName());
				GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Cyan, Msg);
			}
		}
	};
}

void ASAFPlayerController::EmptySelection() {
	Deselect(Selection);

	// Debug
	if (GEngine && EnableSelectorDebugMessages) {
		FString Msg = FString(TEXT("Selection Cleared."));
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Cyan, Msg);
	}
}

void ASAFPlayerController::StartSelector(const FInputActionValue& value) {
	ASAFHUD* HUD = Cast<ASAFHUD>(GetHUD());

	if (IsValid(HUD)) {
		HUD->ReceiveSelectorStarted();
	}

	else {
		UE_LOG(LogTemp, Warning,
		TEXT("Cast to SAFHUD Failed. Your HUD Class may be inheriting from the base class. "
		"Please check class settings for your HUD and ensure the parent class is set to 'SAFHUD'."));
	}
}

void ASAFPlayerController::EndSelector(const FInputActionValue& value) {
	ASAFHUD* HUD = Cast<ASAFHUD>(GetHUD());
	TArray<AActor*> SelectedActors;

	if (IsValid(HUD)) {
		SelectedActors = HUD->ReceiveSelectorEnded();
	}

	else {
		UE_LOG(LogTemp, Warning,
		TEXT("Cast to SAFHUD Failed. Your HUD Class may be inheriting from the base class. "
		"Please check class settings for your HUD and ensure the parent class is set to 'SAFHUD'."));
	}

	bool Additive = ShiftCommandBinding->GetValue().Get<bool>();
	bool Subtractive = ControlBinding->GetValue().Get<bool>();
	Select(SelectedActors, Additive, Subtractive);
}

void ASAFPlayerController::Server_IssueOrder_Implementation(const TArray<AActor*>& Recipients, FVector PayloadPos, AActor* PayloadTarg, bool Additive) {

}

bool ASAFPlayerController::Server_IssueOrder_Validate(const TArray<AActor*>& Recipients, FVector PayloadPos, AActor* PayloadTarg, bool Additive) {
	return true;
}
