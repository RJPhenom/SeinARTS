#pragma once

#include "System/SeinCoverSystem.h"
#include "CoverSnapshotRestoreTestTypes.generated.h"

/**
 * Compatibility probe for a custom Cover implementation written against the
 * original RegisterProvider/UnregisterProvider surface. It intentionally
 * inherits the base lifecycle and owns an append-ordered provider index.
 */
UCLASS()
class USeinCoverRegistryCompatibilityTestSystem : public USeinCoverSystem
{
	GENERATED_BODY()

public:
	virtual void RegisterProvider(
		FSeinEntityHandle ProviderHandle) override
	{
		Providers.Add(ProviderHandle);
	}

	virtual void UnregisterProvider(
		FSeinEntityHandle ProviderHandle) override
	{
		Providers.Remove(ProviderHandle);
	}

	const TArray<FSeinEntityHandle>& GetProviders() const
	{
		return Providers;
	}

private:
	TArray<FSeinEntityHandle> Providers;
};
