using UnrealBuildTool;

public class SeinARTSEditorTests : ModuleRules
{
	public SeinARTSEditorTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"CQTest",
			"UnrealEd",
			"Kismet",
			"KismetCompiler",
			"AssetRegistry",
			"FunctionalTesting",
			"SeinARTSTestSupport",
			"SeinARTSCore",
			"SeinARTSCoreEntity",
			"SeinARTSLevelData",
			"SeinARTSFramework",
			"SeinARTSEditor",
			"SeinARTSGraphNodes",
			"SeinARTSFogOfWarEditor"
		});
	}
}
