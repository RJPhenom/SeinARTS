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
			"Slate",
			"CQTest",
			"SeinARTSTestSupport",
			"SeinARTSCore",
			"SeinARTSCoreEntity",
			"SeinARTSNavigation",
			"SeinARTSMovement",
			"SeinARTSSquad",
			"SeinARTSCover",
			"SeinARTSCoverSquad",
			"SeinARTSMovementPlus"
		});
	}
}
