using UnrealBuildTool;

public class VRCalibrationCore : ModuleRules
{
    public VRCalibrationCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "HeadMountedDisplay", "XRBase", "OculusXRAnchors" });
    }
}
