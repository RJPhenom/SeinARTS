using UnrealBuildTool;

public class SeinARTSTestSupport : ModuleRules
{
	public SeinARTSTestSupport(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"CQTest",
			"GameplayTags",
			"SeinARTSCore",
			"SeinARTSCoreEntity",
			"SeinARTSCombat",
			"SeinARTSLevelData"
		});
	}
}
