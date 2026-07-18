#include "CQTest.h"
#include "Simulation/ComponentStorage.h"
#include "TestTypes/SeinComponentStorageTestTypes.h"

int32 FSeinComponentStorageLifecycleProbe::ConstructionCount = 0;
int32 FSeinComponentStorageLifecycleProbe::DestructionCount = 0;

namespace UE::SeinARTSTests
{
	TEST(ComponentStorageLifecycle, "SeinARTS.Unit.Entity")
	{
		UScriptStruct* ProbeType = FSeinComponentStorageLifecycleProbe::StaticStruct();
		FSeinComponentStorageLifecycleProbe::ResetCounts();

		{
			FSeinGenericComponentStorage Storage(ProbeType, 1);
			const FSeinEntityHandle Handle(1, 1);

			ASSERT_THAT(AreEqual(2, FSeinComponentStorageLifecycleProbe::ConstructionCount));
			ASSERT_THAT(AreEqual(0, FSeinComponentStorageLifecycleProbe::DestructionCount));

			Storage.AddComponent(Handle, nullptr);
			ASSERT_THAT(AreEqual(3, FSeinComponentStorageLifecycleProbe::ConstructionCount));
			ASSERT_THAT(AreEqual(1, FSeinComponentStorageLifecycleProbe::DestructionCount));

			Storage.RemoveComponent(Handle);
			ASSERT_THAT(AreEqual(4, FSeinComponentStorageLifecycleProbe::ConstructionCount));
			ASSERT_THAT(AreEqual(2, FSeinComponentStorageLifecycleProbe::DestructionCount));

			Storage.AddComponent(Handle, nullptr);
			Storage.Clear();
			ASSERT_THAT(AreEqual(6, FSeinComponentStorageLifecycleProbe::ConstructionCount));
			ASSERT_THAT(AreEqual(4, FSeinComponentStorageLifecycleProbe::DestructionCount));
		}

		ASSERT_THAT(AreEqual(
			FSeinComponentStorageLifecycleProbe::ConstructionCount,
			FSeinComponentStorageLifecycleProbe::DestructionCount));
	}
}
