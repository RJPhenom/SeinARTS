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
			"BlueprintGraph",
			"DataValidation",
			"Kismet",
			"KismetCompiler",
			"AssetRegistry",
			"AssetTools",
			"StructViewer",
			"GameplayTags",
			"GameplayTagsEditor",
			"FunctionalTesting",
			"SeinARTSTestSupport",
			"SeinARTSCore",
			"SeinARTSCoreEntity",
			"SeinARTSCombat",
			"SeinARTSNavigation",
			"SeinARTSMovement",
			"SeinARTSLevelData",
			"SeinARTSFogOfWar",
			"SeinARTSFramework",
			"SeinARTSEditor",
			"SeinARTSGraphNodes",
			"SeinARTSFogOfWarEditor"
		});
	}
}
