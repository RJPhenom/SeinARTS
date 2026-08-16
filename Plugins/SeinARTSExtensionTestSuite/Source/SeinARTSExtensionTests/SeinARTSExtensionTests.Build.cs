using UnrealBuildTool;

public class SeinARTSExtensionTests : ModuleRules
{
	public SeinARTSExtensionTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"Slate",
			"CQTest",
			"SeinARTSTestSupport",
			"SeinARTSCore",
			"SeinARTSCoreEntity",
			"SeinARTSFramework",
			"SeinARTSNet",
			"SeinARTSNavigation",
			"SeinARTSLevelData",
			"SeinARTSMovement",
			"SeinARTSSquad",
			"SeinARTSCover",
			"SeinARTSCoverSquad",
			"SeinARTSMovementPlus"
		});
	}
}
