#include "CQTest.h"

#include "Actions/SeinMoveToAction.h"
#include "TestTypes/SeinMoveToLifecycleTestTypes.h"
#include "Testing/SeinMoveToActionContinuationTestAccess.h"
#include "UObject/Package.h"

namespace UE::SeinARTSTests
{
	TEST(MoveToContinuationMapsEveryFutureFieldExactly,
		"SeinARTS.Unit.Movement.Continuation")
	{
		USeinMoveToLifecycleTestMovement* Movement =
			NewObject<USeinMoveToLifecycleTestMovement>(
				GetTransientPackage());
		USeinMoveToAction* Source =
			NewObject<USeinMoveToAction>(
				GetTransientPackage());
		ASSERT_THAT(IsNotNull(Movement));
		ASSERT_THAT(IsNotNull(Source));

		FMoveToActionContinuationTestAccess::
			SeedEveryMappedField(*Source, Movement);
		USeinMoveToAction* Clone =
			FMoveToActionContinuationTestAccess::
				CloneMappedFields(
					*Source, *GetTransientPackage());
		ASSERT_THAT(IsNotNull(Clone));

		FString Error;
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				MappedFieldsEqual(
					*Source, *Clone, Error)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::
				DiagnosticsWereReset(*Clone)));
	}
}
