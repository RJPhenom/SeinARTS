#pragma once

#include "Formations/SeinFormation.h"
#include "SeinFormationExecutionTestTypes.generated.h"

/** Deliberately omits the exact-native admission override. */
UCLASS()
class USeinUnadmittedNativeFormationTest : public USeinFormation
{
	GENERATED_BODY()
};

/** Admitted native test formation that violates the runtime purity contract. */
UCLASS()
class USeinMutatingFormationTest : public USeinFormation
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 InvocationCount = 0;

	virtual bool IsStatelessExecutionAdmitted(FString& OutError) const override
	{
		return AdmitStatelessNativeAnchor(StaticClass(), OutError);
	}

	virtual FSeinFormationLayout BuildFormation_Implementation(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const FSeinOrderTarget& Target) override;
};
