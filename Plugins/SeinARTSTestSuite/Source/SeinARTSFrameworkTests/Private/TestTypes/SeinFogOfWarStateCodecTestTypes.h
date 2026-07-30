#pragma once

#include "Default/SeinFogOfWarDefault.h"
#include "SeinFogOfWarStateCodecTestTypes.generated.h"

/**
 * Native fog subclass that deliberately owns no state codec claim.
 *
 * The default fog codec opts into safe data-only Blueprint children. This
 * native type verifies that the opt-in can never flow through a native layer,
 * even if that layer also carries CLASS_CompiledFromBlueprint.
 */
UCLASS()
class USeinFogOfWarNativeSubclassWithoutCodecTest
	: public USeinFogOfWarDefault
{
	GENERATED_BODY()
};
