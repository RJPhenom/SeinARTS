#include "Modules/ModuleManager.h"

#include "Abilities/SeinAbility.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Serialization/SeinMovementStateCoverage.h"
#include "SeinMovementSubsystem.h"
#include "TestTypes/SeinMoveToLifecycleTestTypes.h"
#include "UObject/UObjectIterator.h"

class FSeinARTSFrameworkTestsModule final
	: public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		PoolObjectCodecHandles.Reset();
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class
				|| Class->GetOutermost()->GetName()
					!= TEXT("/Script/SeinARTSFrameworkTests")
				|| Class->HasAnyClassFlags(
					CLASS_Abstract
						| CLASS_Deprecated
						| CLASS_NewerVersionExists
						| CLASS_CompiledFromBlueprint))
			{
				continue;
			}
			ESeinPoolObjectKind Kind;
			FString KindId;
			if (Class->IsChildOf(USeinAbility::StaticClass()))
			{
				Kind = ESeinPoolObjectKind::Ability;
				KindId = TEXT("ability");
			}
			else if (Class->IsChildOf(
				USeinCommandBrokerResolver::StaticClass()))
			{
				Kind =
					ESeinPoolObjectKind::CommandBrokerResolver;
				KindId = TEXT("resolver");
			}
			else
			{
				continue;
			}
			FString PoolError;
			if (!FSeinPoolObjectCodecRegistry::
				ValidateReflectedClassSchema(
					Class, PoolError))
			{
				// Not every test-only class is a snapshot fixture. Unsafe
				// reflected shapes intentionally remain unregistered so a
				// test must opt into an explicit canonical codec before use.
				continue;
			}

			FSeinPoolObjectCodecDescriptor PoolDescriptor;
			PoolDescriptor.NativeAnchor = Class;
			PoolDescriptor.Kind = Kind;
			PoolDescriptor.StableProviderId = FString::Printf(
				TEXT("seinarts.tests.pool.%s.%s.reflection"),
				*KindId,
				*Class->GetName().ToLower());
			PoolDescriptor.StateSchemaVersion = 1;
			PoolDescriptor.BehaviorRevision = 1;
			PoolDescriptor.CodecRevision = 2;
			PoolDescriptor.MaxStateBytes =
				FSeinPoolObjectCodecRegistry::MaxStateBytes;
			PoolDescriptor.bAllowBlueprintChildren = true;
			FSeinPoolObjectCodecRegistrationHandle PoolHandle =
				FSeinPoolObjectCodecRegistry::Register(
					TEXT("seinartsframeworktests"),
					PoolDescriptor,
					FSeinPoolObjectCodecRegistry::MakeReflectedOps(),
					&PoolError);
			if (!PoolHandle.IsValid())
			{
				UE_LOG(LogTemp, Error,
					TEXT("Framework-test pool codec for %s failed to register: %s"),
					*Class->GetPathName(),
					*PoolError);
				PoolObjectCodecHandles.Reset();
				break;
			}
			PoolObjectCodecHandles.Add(MoveTemp(PoolHandle));
		}

		FSeinMovementStateCoverageDescriptor Descriptor;
		Descriptor.NativeClass =
			USeinMoveToLifecycleTestMovement::StaticClass();
		Descriptor.Coverage =
			ESeinMovementStateCoverage::ReflectedComplete;

		FString Error;
		MovementCoverageHandle =
			FSeinMovementStateCoverageRegistry::Register(
				TEXT("SeinARTSFrameworkTests"),
				Descriptor,
				&Error);
		if (!MovementCoverageHandle.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("Framework-test movement coverage failed to register: %s"),
				*Error);
		}
	}

	virtual void PreUnloadCallback() override
	{
		PoolObjectCodecHandles.Reset();
		for (TObjectIterator<USeinMovementSubsystem> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject))
			{
				It->ReleaseNativeClassStateForModuleUnload(
					TEXT("SeinARTSFrameworkTests"));
			}
		}
		MovementCoverageHandle.Reset();
	}

	virtual void ShutdownModule() override
	{
		PoolObjectCodecHandles.Reset();
		MovementCoverageHandle.Reset();
	}

private:
	FSeinMovementStateCoverageRegistrationHandle
		MovementCoverageHandle;
	TArray<FSeinPoolObjectCodecRegistrationHandle>
		PoolObjectCodecHandles;
};

IMPLEMENT_MODULE(
	FSeinARTSFrameworkTestsModule,
	SeinARTSFrameworkTests)
