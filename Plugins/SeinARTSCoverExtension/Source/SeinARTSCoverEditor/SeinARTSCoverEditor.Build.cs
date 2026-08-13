// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

public class SeinARTSCoverEditor : ModuleRules
{
	public SeinARTSCoverEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"SlateCore",                    // FReply is public API
			"PropertyEditor",               // IPropertyTypeCustomization is public API
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"InputCore",
			"UnrealEd",
			"Kismet",                       // FBlueprintEditorUtils — propagate BP-CDO edits through the BP editor refresh pipeline
			"ComponentVisualizers",
			"GameplayTags",
			"SeinARTSCore",
			"SeinARTSCoreEntity",           // FInstancedStruct walk in the bridge draw delegate
			"SeinARTSCover",
			"SeinARTSEditor",               // RegisterComponentDataDraw for cover Area + slot viz on the entity bridge
		});
	}
}
