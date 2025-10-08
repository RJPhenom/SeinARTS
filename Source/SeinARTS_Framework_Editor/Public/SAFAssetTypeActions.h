#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "AssetTypeCategories.h"
#include "AssetTypeActions/AssetTypeActions_Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Blueprint/BlueprintSupport.h"
#include "Kismet2/KismetEditorUtilities.h"

class USAFAsset;
class ASAFActor;

// Type Actions for "SeinARTS Asset" Data Assets
// ===================================================================================================================================================
class FAssetTypeActions_SAFAsset : public FAssetTypeActions_Base {
public:
	virtual FText   GetName()               const   override { return NSLOCTEXT("AssetTypeActions", "FAssetTypeActions_SAFAsset", "SeinARTS Asset"); }
	virtual FColor  GetTypeColor()          const   override { return FColor::FromHex(TEXT("C91D55")); }
	virtual UClass* GetSupportedClass()     const   override;
	virtual uint32  GetCategories()                 override;
};