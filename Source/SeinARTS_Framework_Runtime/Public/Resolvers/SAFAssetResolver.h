#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"

class USAFAsset;

/** SAFAssetResolver
 * 
 * Template resolver for resolving TSoftObjectPtrsany UObject type (works for USAFAsset subclasses, 
 * eliminates deprecated soft-ptr conversion warnings). */
namespace SAFAssetResolver {
	template<typename T>
	FORCEINLINE T* ResolveAsset(const TSoftObjectPtr<T>& Asset)	{
		if (T* Loaded = Asset.Get()) return Loaded;
		return Asset.IsNull() ? nullptr : Asset.LoadSynchronous();
	}
}
