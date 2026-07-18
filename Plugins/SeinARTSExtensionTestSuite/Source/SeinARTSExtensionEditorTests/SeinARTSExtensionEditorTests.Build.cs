using UnrealBuildTool;

public class SeinARTSExtensionEditorTests : ModuleRules
{
	public SeinARTSExtensionEditorTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"CQTest",
			"UnrealEd",
			"Kismet",
			"KismetCompiler",
			"AssetRegistry",
			"FunctionalTesting",
			"SeinARTSTestSupport",
			"SeinARTSCore",
			"SeinARTSCoreEntity",
			"SeinARTSFramework",
			"SeinARTSEditor",
			"SeinARTSSquad",
			"SeinARTSCover",
			"SeinARTSCoverEditor",
			"SeinARTSCoverSquad",
			"SeinARTSMovementPlus"
		});
	}
}
