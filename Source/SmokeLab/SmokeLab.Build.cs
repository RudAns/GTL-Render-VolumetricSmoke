using UnrealBuildTool;

public class SmokeLab : ModuleRules
{
	public SmokeLab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore",
			"NetCore", "RenderCore", "RHI", "PhysicsCore", "SmokeLabShaders"
		});
	}
}
