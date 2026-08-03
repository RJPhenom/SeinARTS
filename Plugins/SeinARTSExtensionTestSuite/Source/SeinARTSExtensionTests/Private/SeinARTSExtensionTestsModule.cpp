#include "Modules/ModuleManager.h"

#include "Movement/SeinVehicleGymTestTypes.h"
#include "Serialization/SeinMovementStateCoverage.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "SeinMovementSubsystem.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSExtensionTests, Log, All);

class FSeinARTSExtensionTestsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		PoolObjectCodecHandles.Reset();
		CoverageHandles.Reset();
		RegisterVehicleGymAbilityCodec();
		RegisterVehicleGymCoverage(
			USeinVehicleGymWheeledPlanner::StaticClass());
		RegisterVehicleGymCoverage(
			USeinVehicleGymTrackedPlanner::StaticClass());
	}

	virtual void ShutdownModule() override
	{
		PoolObjectCodecHandles.Reset();
		WithdrawVehicleGymCoverage();
	}

	virtual void PreUnloadCallback() override
	{
		PoolObjectCodecHandles.Reset();
		for (TObjectIterator<USeinMovementSubsystem> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject))
			{
				It->ReleaseNativeClassStateForModuleUnload(
					TEXT("SeinARTSExtensionTests"));
			}
		}
		WithdrawVehicleGymCoverage();
	}

private:
	void WithdrawVehicleGymCoverage()
	{
		FString Error;
		if (!FSeinMovementStateCoverageRegistry::UnregisterAll(
				CoverageHandles, &Error))
		{
			UE_LOG(LogSeinARTSExtensionTests, Error,
				TEXT("Vehicle Gym state coverage withdrawal failed: %s"),
				*Error);
		}
	}

	void RegisterVehicleGymAbilityCodec()
	{
		FSeinPoolObjectCodecDescriptor Descriptor;
		Descriptor.NativeAnchor = USeinVehicleGymAbility::StaticClass();
		Descriptor.Kind = ESeinPoolObjectKind::Ability;
		Descriptor.StableProviderId =
			TEXT("seinarts.extensiontests.pool.ability.vehicle-gym.reflection");
		Descriptor.StateSchemaVersion = 2;
		Descriptor.BehaviorRevision = 1;
		Descriptor.CodecRevision = 3;
		Descriptor.MaxStateBytes =
			FSeinPoolObjectCodecRegistry::MaxStateBytes;
		Descriptor.bAllowBlueprintChildren = false;

		FString Error;
		FSeinPoolObjectCodecRegistrationHandle Handle =
			FSeinPoolObjectCodecRegistry::Register(
				TEXT("SeinARTSExtensionTests.VehicleGym"),
				Descriptor,
				FSeinPoolObjectCodecRegistry::MakeReflectedOps(),
				&Error);
		if (!Handle.IsValid())
		{
			UE_LOG(LogSeinARTSExtensionTests, Error,
				TEXT("Vehicle Gym ability codec registration failed: %s"),
				*Error);
			return;
		}
		PoolObjectCodecHandles.Add(MoveTemp(Handle));
	}

	void RegisterVehicleGymCoverage(const UClass* NativeClass)
	{
		FSeinMovementStateCoverageDescriptor Descriptor;
		Descriptor.NativeClass = NativeClass;
		Descriptor.Coverage =
			ESeinMovementStateCoverage::ReflectedComplete;

		FString Error;
		FSeinMovementStateCoverageRegistrationHandle Handle =
			FSeinMovementStateCoverageRegistry::Register(
				TEXT("SeinARTSExtensionTests.VehicleGym"),
				Descriptor,
				&Error);
		if (!Handle.IsValid())
		{
			UE_LOG(LogSeinARTSExtensionTests, Error,
				TEXT("Vehicle Gym state coverage registration failed for '%s': %s"),
				NativeClass ? *NativeClass->GetPathName() : TEXT("<null>"),
				*Error);
			return;
		}
		CoverageHandles.Add(MoveTemp(Handle));
	}

	TArray<FSeinMovementStateCoverageRegistrationHandle> CoverageHandles;
	TArray<FSeinPoolObjectCodecRegistrationHandle> PoolObjectCodecHandles;
};

IMPLEMENT_MODULE(FSeinARTSExtensionTestsModule, SeinARTSExtensionTests)
