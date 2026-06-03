using UnrealBuildTool;

public class SeinARTSMovement : ModuleRules
{
    public SeinARTSMovement(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore", "SeinARTSCoreEntity",
            "SeinARTSNavigation",
            "GameplayTags"
        });

        // Editor-only deps: the active-move debug ticker reaches into the
        // editor viewport iterator to gate per-world drawing on the editor's
        // Navigation showflag (mirrors USeinARTSNavigationModule's pattern).
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd"
            });
        }
    }
}
