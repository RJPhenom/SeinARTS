using UnrealBuildTool;

public class SeinARTSFrameworkTests : ModuleRules
{
	public SeinARTSFrameworkTests(ReadOnlyTargetRules Target) : base(Target)
	{
		// Test fixtures commonly reuse file-local helper names. Building each
		// source as its own translation unit keeps those fixtures independent.
		bUseUnity = false;

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
