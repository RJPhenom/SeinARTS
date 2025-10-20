#include "Assets/SAFPawnAsset.h"

#if WITH_EDITOR
#include "Components/SAFMovementComponent.h"
#endif

USAFPawnAsset::USAFPawnAsset(const FObjectInitializer& ObjectInitializer) 
: Super(ObjectInitializer) {
}

void USAFPawnAsset::PostLoad() {
	Super::PostLoad();
}

#if WITH_EDITOR
void USAFPawnAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(USAFPawnAsset, MovementMode)) {
		// Create a temporary movement component to get the default values for the new mode
		USAFMovementComponent* TempMovementComponent = NewObject<USAFMovementComponent>();
		TempMovementComponent->MovementMode = MovementMode;
		TempMovementComponent->ApplyMovementModeDefaults();
		
				// Movement Properties					// Seeded default values
				MaxSpeed 								= TempMovementComponent->MaxSpeed;
				Acceleration 							= TempMovementComponent->Acceleration;
				Deceleration 							= TempMovementComponent->Deceleration;
				MaxRotationRate 						= TempMovementComponent->MaxRotationRate;
				ReverseEngageDotThreshold 				= TempMovementComponent->ReverseEngageDotThreshold;
				ReverseEngageDistanceThreshold 			= TempMovementComponent->ReverseEngageDistanceThreshold;
				ReverseMaxSpeed 						= TempMovementComponent->ReverseMaxSpeed;

		switch (MovementMode) {
			case ESAFMovementMode::Infantry:
				Infantry_bAllowStrafe 					= TempMovementComponent->Infantry_bAllowStrafe;
				Infantry_StrafingDotThreshold 			= TempMovementComponent->Infantry_StrafingDotThreshold;
				break;
				
			case ESAFMovementMode::Tracked:
				Tracked_ThrottleVsMisalignmentDeg 		= TempMovementComponent->Tracked_ThrottleVsMisalignmentDeg;
				break;
				
			case ESAFMovementMode::Wheeled:
				Wheeled_Wheelbase 						= TempMovementComponent->Wheeled_Wheelbase;
				Wheeled_MaxSteerAngleDeg 				= TempMovementComponent->Wheeled_MaxSteerAngleDeg;
				Wheeled_SteerResponse 					= TempMovementComponent->Wheeled_SteerResponse;
				break;
				
			case ESAFMovementMode::Hover:
				break;
			
			default:
				break;
		}
		
		// Clean up the temporary component
		TempMovementComponent->MarkAsGarbage();
	}
}
#endif