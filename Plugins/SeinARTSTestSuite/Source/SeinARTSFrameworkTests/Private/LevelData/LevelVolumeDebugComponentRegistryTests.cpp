#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Misc/ScopeExit.h"
#include "TestTypes/SeinLevelVolumeDebugComponentTestTypes.h"
#include "Volumes/SeinLevelVolume.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		int32 CountExactComponents(
			const ASeinLevelVolume& Volume,
			const UClass* ComponentClass)
		{
			TInlineComponentArray<UActorComponent*> Components(&Volume);
			int32 Count = 0;
			for (const UActorComponent* Component : Components)
			{
				if (IsValid(Component)
					&& Component->GetClass() == ComponentClass)
				{
					++Count;
				}
			}
			return Count;
		}

		int32 CountExactInstanceComponents(
			const ASeinLevelVolume& Volume,
			const UClass* ComponentClass)
		{
			int32 Count = 0;
			for (const UActorComponent* Component :
				Volume.GetInstanceComponents())
			{
				if (IsValid(Component)
					&& Component->GetClass() == ComponentClass)
				{
					++Count;
				}
			}
			return Count;
		}

		UActorComponent* FindExactComponent(
			const ASeinLevelVolume& Volume,
			const UClass* ComponentClass)
		{
			TInlineComponentArray<UActorComponent*> Components(&Volume);
			UActorComponent** Found = Components.FindByPredicate(
				[ComponentClass](const UActorComponent* Component)
				{
					return IsValid(Component)
						&& Component->GetClass() == ComponentClass;
				});
			return Found ? *Found : nullptr;
		}

		template <typename ComponentType>
		ComponentType* AddTransientInstanceComponent(
			ASeinLevelVolume& Volume)
		{
			ComponentType* Component = NewObject<ComponentType>(
				&Volume, NAME_None, RF_Transient);
			if (!Component)
			{
				return nullptr;
			}
			Volume.AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		}
	}

	TEST(LevelVolumeDebugRegistryReconcilesAndPurgesLiveVolumes,
		"SeinARTS.Unit.LevelData.DebugComponentRegistry")
	{
		UClass* ExactClass =
			USeinLevelVolumeDebugTestComponent::StaticClass();
		UClass* DerivedClass =
			USeinLevelVolumeDerivedDebugTestComponent::StaticClass();
		ASeinLevelVolume::UnregisterDebugComponentClass(ExactClass);
		ON_SCOPE_EXIT
		{
			ASeinLevelVolume::UnregisterDebugComponentClass(ExactClass);
		};

		FActorTestSpawner Spawner;
		ASeinLevelVolume& Volume =
			Spawner.SpawnActor<ASeinLevelVolume>();

		USeinLevelVolumeDerivedDebugTestComponent* Derived =
			AddTransientInstanceComponent<
				USeinLevelVolumeDerivedDebugTestComponent>(Volume);
		ASSERT_THAT(IsNotNull(Derived));

		// A derived instance does not satisfy the exact-class registration.
		ASeinLevelVolume::RegisterDebugComponentClass(ExactClass);
		ASSERT_THAT(AreEqual(
			1, CountExactComponents(Volume, ExactClass)));
		ASSERT_THAT(AreEqual(
			1, CountExactComponents(Volume, DerivedClass)));

		// Duplicate registration heals a missing live instance.
		UActorComponent* Registered =
			FindExactComponent(Volume, ExactClass);
		ASSERT_THAT(IsNotNull(Registered));
		Registered->DestroyComponent();
		ASSERT_THAT(AreEqual(
			0, CountExactComponents(Volume, ExactClass)));
		ASeinLevelVolume::RegisterDebugComponentClass(ExactClass);
		ASSERT_THAT(AreEqual(
			1, CountExactComponents(Volume, ExactClass)));

		// A healthy duplicate registration remains idempotent.
		ASeinLevelVolume::RegisterDebugComponentClass(ExactClass);
		ASSERT_THAT(AreEqual(
			1, CountExactComponents(Volume, ExactClass)));

		// Simulate a stale duplicate created by an earlier module generation.
		USeinLevelVolumeDebugTestComponent* Duplicate =
			AddTransientInstanceComponent<
				USeinLevelVolumeDebugTestComponent>(Volume);
		ASSERT_THAT(IsNotNull(Duplicate));
		ASSERT_THAT(AreEqual(
			2, CountExactInstanceComponents(Volume, ExactClass)));

		ASeinLevelVolume::UnregisterDebugComponentClass(ExactClass);
		ASSERT_THAT(AreEqual(
			0, CountExactComponents(Volume, ExactClass)));
		ASSERT_THAT(AreEqual(
			0, CountExactInstanceComponents(Volume, ExactClass)));
		ASSERT_THAT(AreEqual(
			1, CountExactComponents(Volume, DerivedClass)));

		// Teardown is idempotent and exact-class only.
		ASeinLevelVolume::UnregisterDebugComponentClass(ExactClass);
		ASSERT_THAT(AreEqual(
			1, CountExactComponents(Volume, DerivedClass)));
	}
}
