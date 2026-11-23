using UnrealBuildTool;

public class VRCalibrationMarker : ModuleRules
{
    public VRCalibrationMarker(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "VRCalibrationCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "InputCore", "MRUtilityKit", "OculusXRAnchors", "OpenCVHelper", "OpenCV", "RHI", "RenderCore" });
    }
}
