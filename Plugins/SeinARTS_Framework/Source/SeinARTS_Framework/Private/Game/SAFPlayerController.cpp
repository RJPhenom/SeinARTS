


#include "Game/SAFPlayerController.h"
#include "EnhancedInput/Public/InputActionValue.h"
#include "SAFObject.h"
#include "SAFHUD.h"
#include "SAFISelection.h"
#include "SAFIOrder.h"

ASAFPlayerController::ASAFPlayerController()
{
	
}

void ASAFPlayerController::BeginPlay() {
	Super::BeginPlay();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		Subsystem->AddMappingContext(DefaultMappingContext, 0);
}

void ASAFPlayerController::SetupInputComponent() {
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent)) {

		// Action Bindings
		EnhancedInputComponent->BindAction(SelectionAction, ETriggerEvent::Started, this, &ASAFPlayerController::StartSelector);
		EnhancedInputComponent->BindAction(SelectionAction, ETriggerEvent::Completed, this, &ASAFPlayerController::EndSelector);
		EnhancedInputComponent->BindAction(OrderAction, ETriggerEvent::Started, this, &ASAFPlayerController::StartSelector);
		EnhancedInputComponent->BindAction(ShiftCommandAction, ETriggerEvent::Started, this, &ASAFPlayerController::StartSelector);
		EnhancedInputComponent->BindAction(AlternateAction, ETriggerEvent::Started, this, &ASAFPlayerController::StartSelector);
		EnhancedInputComponent->BindAction(ControlAction, ETriggerEvent::Started, this, &ASAFPlayerController::StartSelector);

		// Action Values
		ShiftCommandBinding = &EnhancedInputComponent->BindActionValue(ShiftCommandAction);
		AlternateBinding = &EnhancedInputComponent->BindActionValue(AlternateAction);
		ControlBinding = &EnhancedInputComponent->BindActionValue(ControlAction);
	}

}

TArray<ASAFObject*> ASAFPlayerController::GetSelection() {
	TArray<ASAFObject*> SelectedSAFObjects;

	for (TWeakObjectPtr<ASAFObject> SAFObject : Selection) {
		if (SAFObject.IsValid()) SelectedSAFObjects.Add(SAFObject.Get());
	}

	return SelectedSAFObjects;
}

ASAFObject* ASAFPlayerController::GetSelected() {
	if (!Selected.Get()) {
		if (Selection.Num() <= 0) {
			UE_LOG(LogTemp, Error, TEXT("Could not retrieve active selected unit because selection is empty!"));
			return nullptr;
		}

		else {
			Selected = Selection[0];
		}
	}

	return Selected.Get();
}

bool ASAFPlayerController::SetSelection(TArray<ASAFObject*> InSelection) {
	TArray<TWeakObjectPtr<ASAFObject>> SAFObjects;

	for (ASAFObject* SAFObject : InSelection) {
		if (SAFObject 
			&& SAFObject->Implements<USAFISelection>() 
			&& ISAFISelection::Execute_GetSelectable(SAFObject)) 
		{
			SAFObjects.Add(SAFObject);
		}

		else {
			return false;
		}
	}

	Deselect(GetSelection());
	Selection = SAFObjects;

	return true;
}

void ASAFPlayerController::Select(TArray<ASAFObject*> Targets, bool Additive) {
	if (!Additive) Deselect(GetSelection());

	TArray<TWeakObjectPtr<ASAFObject>> SAFObjectsToAppend;
	for (ASAFObject* SAFObject : Targets) {
		if (SAFObject 
			&& SAFObject->Implements<USAFISelection>() 
			&& ISAFISelection::Execute_GetSelectable(SAFObject)) 
		{
			SAFObjectsToAppend.Add(TWeakObjectPtr<ASAFObject>(SAFObject));
		}
	};

	// TODO: run check if Targ is uniform array of owned units, or singlur unowned unit

	for (TWeakObjectPtr<ASAFObject> SAFObject : SAFObjectsToAppend) {
		ISAFISelection::Execute_OnSelect(SAFObject.Get(), this);
	}
}

void ASAFPlayerController::Deselect(TArray<ASAFObject*> Targets) {
	for (ASAFObject* SAFObject : Targets) {
		if (SAFObject && SAFObject->Implements<USAFISelection>()) ISAFISelection::Execute_OnDeselect(SAFObject, this);
		Selection.Remove(TWeakObjectPtr<ASAFObject>(SAFObject));
	};
}

void ASAFPlayerController::StartSelector(const FInputActionValue& value) {
	ASAFHUD* HUD = Cast<ASAFHUD>(GetHUD());

	if (HUD) {
		HUD->ReceiveSelectorStarted();
	}

	else {
		UE_LOG(LogTemp, Warning,
			TEXT("Cast to SAFHUD Failed. Your HUD Class may be inheriting from the base class. ")
			TEXT("Please check class settings for your HUD and ensure the parent class is set to 'SAFHUD'.")
		);
	}
}

void ASAFPlayerController::EndSelector(const FInputActionValue& value) {
	ASAFHUD* HUD = Cast<ASAFHUD>(GetHUD());
	TArray<ASAFObject*> SelectedItems;

	if (HUD) {
		 //SelectedItems = HUD->ReceiveSelectorEnded(); TODO: fix
	}

	else {
		UE_LOG(LogTemp, Warning,
			TEXT("Cast to SAFHUD Failed. Your HUD Class may be inheriting from the base class. ")
			TEXT("Please check class settings for your HUD and ensure the parent class is set to 'SAFHUD'.")
		);
	}

	bool Additive = ShiftCommandBinding->GetValue().Get<bool>();
	Select(SelectedItems, Additive);
}

void ASAFPlayerController::Server_IssueOrder_Implementation(const TArray<ASAFObject*>& Recipients, FVector PayloadPos, ASAFObject* PayloadTarg, bool Additive) {

}

bool ASAFPlayerController::Server_IssueOrder_Validate(const TArray<ASAFObject*>& Recipients, FVector PayloadPos, ASAFObject* PayloadTarg, bool Additive) {
	return true;
}
