using UnrealBuildTool;

public class SeinARTSMovement : ModuleRules
{
    public SeinARTSMovement(ReadOnlyTargetRules Target) : base(Target)
    {
        // Keep movement codec/provider implementation helpers in distinct
        // translation units; Unreal unity merging breaks that isolation.
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore",
            "GameplayTags",   // FGameplayTag is exposed on the public mover/planner handle headers
            "SeinARTSCoreEntity",
            "SeinARTSNavigation" // FSeinPath and navigation request/result types are public API
        });

        // Filled navigation cells use the engine's shared white Canvas texture.
        PrivateDependencyModuleNames.Add("RenderCore");

        // Editor-only deps: the Extents ticker and redraw requests reach into the
        // editor viewport iterator. Navigation and Steering geometry draw through
        // their per-view Canvas callbacks.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd"
            });
        }
    }
}
