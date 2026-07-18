using UnrealBuildTool;

public class SeinARTSFrameworkTests : ModuleRules
{
	public SeinARTSFrameworkTests(ReadOnlyTargetRules Target) : base(Target)
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
			"SeinARTSLevelData",
			"SeinARTSNavigation",
			"SeinARTSMovement",
			"SeinARTSFogOfWar",
			"SeinARTSNet",
			"SeinARTSFramework",
			"SeinARTSUIToolkit"
		});
	}
}
