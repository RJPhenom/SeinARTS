#pragma once

#include "CoreMinimal.h"
#include "Enums/SAFResourceRoundingPolicies.h"
#include "SAFResources.generated.h"

/**
 * FSAFResources
 *
 * Fixed-size bundle for 12 resource slots.
 * Use helper methods to do safe math and indexed access. 
 */
USTRUCT(BlueprintType)
struct SEINARTS_FRAMEWORK_RUNTIME_API FSAFResources {

    GENERATED_BODY()

public:

    static constexpr int32 NumSlots = 12;

    /** Designer-friendly verbose fields (replicate cleanly via owning UPROPERTY). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource1  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource2  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource3  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource4  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource5  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource6  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource7  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource8  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource9  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource10 = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource11 = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Production") int32 Resource12 = 0;

    /** Indexed getter (out of ranges indices return zero). */
    FORCEINLINE int32 Get(int32 Slot) const {
        switch (Slot) {
            case 1:  return Resource1;  
            case 2:  return Resource2;  
            case 3:  return Resource3;  
            case 4:  return Resource4;
            case 5:  return Resource5;  
            case 6:  return Resource6;  
            case 7:  return Resource7;  
            case 8:  return Resource8;
            case 9:  return Resource9;  
            case 10: return Resource10; 
            case 11: return Resource11; 
            case 12: return Resource12;
            default: return 0;
        }
    }

    /** Indexed setter (out of range indicies are ignored). */
    FORCEINLINE void Set(int32 Slot, int32 Value) {
        switch (Slot) {
            case 1:  Resource1  = Value; break; 
            case 2:  Resource2  = Value; break; 
            case 3:  Resource3  = Value; break; 
            case 4:  Resource4  = Value; break;
            case 5:  Resource5  = Value; break; 
            case 6:  Resource6  = Value; break; 
            case 7:  Resource7  = Value; break; 
            case 8:  Resource8  = Value; break;
            case 9:  Resource9  = Value; break; 
            case 10: Resource10 = Value; break; 
            case 11: Resource11 = Value; break; 
            case 12: Resource12 = Value; break;
            default: break;
        }
    }

    /** Returns true if all resource values are zero. */
    FORCEINLINE bool IsZero() const {
        return Resource1==0 && Resource2==0 && Resource3==0 && Resource4==0 && Resource5==0 && Resource6==0 &&
               Resource7==0 && Resource8==0 && Resource9==0 && Resource10==0 && Resource11==0 && Resource12==0;
    }

    /** Compares values against another bundle, returns true if this bundle is entirely greater or equal to the other bundle. */
    FORCEINLINE bool AllGreaterOrEqualTo(const FSAFResources& Other) const {
        return Resource1>=Other.Resource1 && Resource2>=Other.Resource2 && Resource3>=Other.Resource3 && Resource4>=Other.Resource4 &&
               Resource5>=Other.Resource5 && Resource6>=Other.Resource6 && Resource7>=Other.Resource7 && Resource8>=Other.Resource8 &&
               Resource9>=Other.Resource9 && Resource10>=Other.Resource10 && Resource11>=Other.Resource11 && Resource12>=Other.Resource12;
    }

    /** Adds a bundle to this bundle. */
    FORCEINLINE void Add(const FSAFResources& Other) {
        Resource1+=Other.Resource1; Resource2+=Other.Resource2; Resource3+=Other.Resource3; Resource4+=Other.Resource4;
        Resource5+=Other.Resource5; Resource6+=Other.Resource6; Resource7+=Other.Resource7; Resource8+=Other.Resource8;
        Resource9+=Other.Resource9; Resource10+=Other.Resource10; Resource11+=Other.Resource11; Resource12+=Other.Resource12;
    }

    /** Subtracts a bundle from this bundle. */
    FORCEINLINE void Sub(const FSAFResources& Other) {
        Resource1-=Other.Resource1; Resource2-=Other.Resource2; Resource3-=Other.Resource3; Resource4-=Other.Resource4;
        Resource5-=Other.Resource5; Resource6-=Other.Resource6; Resource7-=Other.Resource7; Resource8-=Other.Resource8;
        Resource9-=Other.Resource9; Resource10-=Other.Resource10; Resource11-=Other.Resource11; Resource12-=Other.Resource12;
    }

    /** Clamps all values in this bundle to a value of zero or more. */
    FORCEINLINE void ClampNonNegative() {
        Resource1=FMath::Max(0,Resource1); Resource2=FMath::Max(0,Resource2); Resource3=FMath::Max(0,Resource3); Resource4=FMath::Max(0,Resource4);
        Resource5=FMath::Max(0,Resource5); Resource6=FMath::Max(0,Resource6); Resource7=FMath::Max(0,Resource7); Resource8=FMath::Max(0,Resource8);
        Resource9=FMath::Max(0,Resource9); Resource10=FMath::Max(0,Resource10); Resource11=FMath::Max(0,Resource11); Resource12=FMath::Max(0,Resource12);
    }

    /** Helper for converting float costs to non-negative ints per policy. */
    FORCEINLINE static int32 ToIntByPolicy(float Value, ESAFResourceRoundingPolicy Policy) {
        int32 Out = 0;
        switch (Policy) {
            case ESAFResourceRoundingPolicy::Floor: Out = FMath::FloorToInt(Value); break;
            case ESAFResourceRoundingPolicy::Round: Out = FMath::RoundToInt(Value); break;
            case ESAFResourceRoundingPolicy::Ceil:  Out = FMath::CeilToInt(Value);  break;
            default: Out = FMath::RoundToInt(Value); break;
        }

        return FMath::Max(0, Out);
    }
};