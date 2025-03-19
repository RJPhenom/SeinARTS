using UnrealBuildTool;

public class SeinARTS_Framework_Editor: ModuleRules
{
    public SeinARTS_Framework_Editor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] {
            ModuleDirectory + "/Public",
            ModuleDirectory + "/Public/Game",
            ModuleDirectory + "/Public/GameState",
            ModuleDirectory + "/Public/Objects",
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UnrealEd",
            "Slate",
            "SlateCore",
            "EditorStyle",
            "PropertyEditor",
            "InputCore",
            "Projects",
            "ApplicationCore",
            "NavigationSystem",
            "SeinARTS_Framework_Runtime"
        });
    }
}
