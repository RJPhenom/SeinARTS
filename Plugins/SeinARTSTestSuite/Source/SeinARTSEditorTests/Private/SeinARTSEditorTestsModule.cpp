#include "Modules/ModuleManager.h"

#include "Serialization/SeinMovementStateCoverage.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "SeinMovementSubsystem.h"
#include "TestTypes/SeinMoveToContinuationEditorTestTypes.h"
#include "UObject/UObjectIterator.h"

class FSeinARTSEditorTestsModule final
	: public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FSeinPoolObjectCodecDescriptor PoolDescriptor;
		PoolDescriptor.NativeAnchor =
			USeinMoveToContinuationEditorTestAbility::
				StaticClass();
		PoolDescriptor.Kind = ESeinPoolObjectKind::Ability;
		PoolDescriptor.StableProviderId =
			TEXT("seinarts.editor-tests.pool.move-continuation.reflection");
		PoolDescriptor.StateSchemaVersion = 2;
		PoolDescriptor.BehaviorRevision = 1;
		PoolDescriptor.CodecRevision = 3;
		PoolDescriptor.MaxStateBytes =
			FSeinPoolObjectCodecRegistry::MaxStateBytes;
		PoolDescriptor.bAllowBlueprintChildren = true;
		FString PoolError;
		PoolObjectCodecHandle =
			FSeinPoolObjectCodecRegistry::Register(
				TEXT("seinartseditortests"),
				PoolDescriptor,
				FSeinPoolObjectCodecRegistry::MakeReflectedOps(),
				&PoolError);
		if (!PoolObjectCodecHandle.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("Editor-test pool codec failed to register: %s"),
				*PoolError);
		}

		FSeinMovementStateCoverageDescriptor Descriptor;
		Descriptor.NativeClass =
			USeinMoveToContinuationEditorTestMovement::
				StaticClass();
		Descriptor.Coverage =
			ESeinMovementStateCoverage::ReflectedComplete;
		FString Error;
		MovementCoverageHandle =
			FSeinMovementStateCoverageRegistry::Register(
				TEXT("SeinARTSEditorTests"),
				Descriptor,
				&Error);
		if (!MovementCoverageHandle.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("Editor-test movement coverage failed to register: %s"),
				*Error);
		}
	}

	virtual void PreUnloadCallback() override
	{
		PoolObjectCodecHandle.Reset();
		for (TObjectIterator<USeinMovementSubsystem> It;
			It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject))
			{
				It->ReleaseNativeClassStateForModuleUnload(
					TEXT("SeinARTSEditorTests"));
			}
		}
		MovementCoverageHandle.Reset();
	}

	virtual void ShutdownModule() override
	{
		PoolObjectCodecHandle.Reset();
		MovementCoverageHandle.Reset();
	}

private:
	FSeinMovementStateCoverageRegistrationHandle
		MovementCoverageHandle;
	FSeinPoolObjectCodecRegistrationHandle
		PoolObjectCodecHandle;
};

IMPLEMENT_MODULE(
	FSeinARTSEditorTestsModule,
	SeinARTSEditorTests)
