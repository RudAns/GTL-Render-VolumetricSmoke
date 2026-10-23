using UnrealBuildTool;

public class VolumetricSmokeTarget : TargetRules
{
	public VolumetricSmokeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "SmokeLab", "SmokeLabShaders" });
	}
}
