// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

public class SeinARTSGraphNodes : ModuleRules
{
	public SeinARTSGraphNodes(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
			"Kismet",                       // K2Node + FBlueprintEditorUtils
			"KismetCompiler",               // FKismetCompilerContext
			"BlueprintGraph",               // FBlueprintActionDatabaseRegistrar + K2Node_CallFunction
			"GraphEditor",
			"SeinARTSCore",
			"SeinARTSCoreEntity",           // FSeinComponent base + USeinComponentBPFL the K2 nodes wrap
		});
	}
}
