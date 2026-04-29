//@Locketgoma

using UnrealBuildTool;

public class MetalFX : ModuleRules
{
	public MetalFX(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DeveloperSettings", 
				"RenderCore"
			});
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"Renderer",
				"SlateCore",
				"RHI",
				"Projects"
			});
		
		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PrivateDependencyModuleNames.Add("MetalCPP");
			PrivateDependencyModuleNames.Add("MetalRHI");
            PublicFrameworks.AddRange(new string[] {
                "Metal",
	            "Foundation",
                "MetalFX"   
            });

			PublicDefinitions.Add("WITH_METAL_PLATFORM = 1");
		}
		else
		{
			PublicDefinitions.Add("WITH_METAL_PLATFORM = 0");
		}
			
	}
}
