using UnrealBuildTool;

public class SeinARTS_Framework_Editor: ModuleRules
{
    public SeinARTS_Framework_Editor(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", 
            "CoreUObject", 
            "Engine",
            "Slate",
            "SlateCore",
            "UnrealEd",
            "SeinARTS_Framework_Runtime"
        });
    }
}
