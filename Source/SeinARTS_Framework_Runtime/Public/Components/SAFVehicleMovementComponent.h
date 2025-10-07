#pragma once

#include "Enums/SAFVehicleDriveTypes.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "SAFVehicleMovementComponent.generated.h"

UCLASS(ClassGroup=(SeinARTS), BlueprintType, Blueprintable)
class SEINARTS_FRAMEWORK_RUNTIME_API USAFVehicleMovementComponent : public UFloatingPawnMovement {

  GENERATED_BODY()

public:

  USAFVehicleMovementComponent();

  // Steering
  // =================================================================================================================================
  // Sets the drive type of this vehicle movement component.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|VehicleMovementComponent")
  ESAFVehicleDriveType DriveType = ESAFVehicleDriveType::Tracked;

  // Maximum turn rate in degrees.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|VehicleMovementComponent")
  float MaxTurnRateDeg = 60.f;

  // If a movement point is below this Dot threshold and within the Distance threshold, then this
  // vehicle movement component will reverse to that point rather than rotate and drive to it.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|VehicleMovementComponent", meta=(ClampMin="-1.0", ClampMax="1.0"))
  float ReverseEngageDotThreshold = -0.5f;

  // If a movement point is within this Distance threshold and below the Dot threshold, then this
  // vehicle movement component will reverse to that point rather than rotate and drive to it.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|VehicleMovementComponent", meta=(ClampMin="0"))
  float ReverseEngageDistanceThreshold = 2500.f;

  // Max speed when reversing, differs from overall max speed.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|VehicleMovementComponent", meta=(ClampMin="0"))
  float ReverseMaxSpeed = 300.f;

  // Sets the world-space goal used by the “close & behind” auto-reverse rule.
  // When set, regular MoveTo will choose to drive in reverse **only if** the goal is
  // both (a) behind the vehicle by at least ReverseEngageDotThreshold and (b) within
  // ReverseEngageDistanceThreshold. Call this when issuing a MoveTo; calling again
  // updates the goal. This does not issue movement by itself.
  UFUNCTION(BlueprintCallable, Category="SeinARTS|VehicleMovementComponent")
  void SetReverseCheckGoal(const FVector& InGoal) { bHasReverseGoal = true; ReverseGoal = InGoal; }

  // Clears the auto-reverse goal so regular MoveTo will no longer reverse due to the
  // “close & behind” rule. Use after reaching a destination or before issuing moves
  // where reverse should not be considered. (Does not stop movement.)
  UFUNCTION(BlueprintCallable, Category="SeinARTS|VehicleMovementComponent")
  void ClearReverseCheckGoal() { bHasReverseGoal = false; }

  // Tracked
  // ================================================================================================
  /** Curve representing forward throttle vs. direction misalignment.
   *  X:  Misalignment angle in degrees (0..180) between facing and desired move
   * direction. 0° means perfectly aligned, 180° means pointing exactly away.
   *  Y:  Throttle multiplier (0..1) applied to MaxSpeed while aligning. For turning
   * in place, set to 0 from 180 to 90 and then curve towards 1 for 'creeping' 
   * forward while turning effect.  
   * 
   * If the curve has no keys, a reasonable default fallback curve is used:
   *  0°    =   1
   *  8°    =   0.4
   *  15°   =   0.2
   *  45°   =   0.1
   *  90°   =   0
   *  180°  =   0 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|VehicleMovementComponent|Tracked")
  FRuntimeFloatCurve ThrottleVsMisalignmentDeg;

  // Wheeled
  // =================================================================================================================================
  // Represents the distance between axels (cm), affects turn radius.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|VehicleMovementComponent|Wheeled", meta=(ClampMin="1"))
  float Wheelbase = 220.f;

  // Maximum front wheel steering angle.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|VehicleMovementComponent|Wheeled", meta=(ClampMin="1", ClampMax="60"))
  float MaxSteerAngleDeg = 60.f;

  // How fast we approach desired steer angle.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|VehicleMovementComponent|Wheeled", meta=(ClampMin="0.1", ClampMax="10"))
  float SteerResponse = 3.f;

  // AI path following entry point
  virtual void RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) override;
  virtual void StopActiveMovement() override;
  virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

  bool      bMoveRequested    = false;
  bool      bHasReverseGoal   = false;
  float     CurrentSteerDeg   = 0.f;
  float     DesiredSpeed      = 0.f;
  float     StopSpeedEpsilon  = 1.f;
  FVector   DesiredMoveDir    = FVector::ZeroVector;
  FVector   ReverseGoal       = FVector::ZeroVector;

  // Internals
  // =================================================================================================================================
  /** Checks the tracked forward velocity curve and returns the output for the misalignment input point. If no curve is set, sets the 
   * default curve (once) and returns the check against the default curve. */
  float EvalTrackedThrottle(float MisalignmentDeg) const;

  /** Smooths forward velocity changes, preventing instant direction changes when issuing a reverse move during an active forward move. */
  static float SmoothStepForwardVelocity(float DeltaTime, float CurrSignedSpeed, float TargetSignedSpeed, float InAcceleration, float InDeceleration);

  /** Ticks movement if drive type is set to Tracked. */
  void TickTrackedMovement(float DeltaTime, bool bUseReverse, FVector Forward, FVector Desired, float Yaw);

  /** Ticks movement if drive type is set to Wheeled. */
  void TickWheeledMovement(float DeltaTime, bool bUseReverse, FVector Forward, FVector Desired, float Yaw);

  /** Ticks movement if drive type is set to Hover. */
  void TickHoverMovement(float DeltaTime, bool bUseReverse, FVector Forward, FVector Desired, float Yaw);

  /** Applies update calculations to the actualy velocity of the movement component. */
  void ApplyTickMovement(float DeltaTime, FRotator NewRot, bool bUseReverse, float TargetSpeedUnsigned);
};
