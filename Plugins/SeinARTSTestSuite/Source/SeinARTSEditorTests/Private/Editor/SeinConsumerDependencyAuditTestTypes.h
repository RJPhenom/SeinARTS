/** Test-only asset for consumer dependency admission checks. */
#pragma once
#include "Engine/DataAsset.h"
#include "UObject/ObjectSaveContext.h"
#include "SeinConsumerDependencyAuditTestTypes.generated.h"

UCLASS()
class USeinConsumerDependencyAuditTestAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY() FString LivePath;
	UPROPERTY() TSoftObjectPtr<UObject> SoftPath;
	bool bClearOnSave = false;
	virtual void PreSave(FObjectPreSaveContext Context) override
	{
		if (bClearOnSave) LivePath.Reset();
		Super::PreSave(Context);
	}
};
