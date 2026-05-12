//@Locketgoma

using UnrealBuildTool;
using System.IO;

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
				"RHICore",
				"Projects"
			});

		//------------------MetalFX Handling Branch------------------

		//해당 플러그인에서 지원하는 Apple Platfrom에서만 True 되도록 처리
		//TV OS / Vision OS 는 제외 (애초에 테스트 가능하지도 않고)
		bool bApplePlatfrom = false;

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			bApplePlatfrom = true;
    		PublicDefinitions.Add("WITH_METALFX_TARGET_MAC=1");
    		PublicDefinitions.Add("WITH_METALFX_TARGET_IOS=0");
		}
		
		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			bApplePlatfrom = true;
    		PublicDefinitions.Add("WITH_METALFX_TARGET_MAC=0");
    		PublicDefinitions.Add("WITH_METALFX_TARGET_IOS=1");
		}

		if(bApplePlatfrom)
		{
			PrivateDependencyModuleNames.Add("MetalCPP");
			PrivateDependencyModuleNames.Add("MetalRHI");
            PublicFrameworks.AddRange(new string[] {
                "Metal",
	            "Foundation",
                "MetalFX"   
            });
            
            PrivateIncludePaths.AddRange(new string[]
            {
                Path.Combine(EngineDirectory, "Source/Runtime/Apple/MetalRHI/Public"),
                Path.Combine(EngineDirectory, "Source/Runtime/Apple/MetalRHI/Private"),
                Path.Combine(EngineDirectory, "Source/ThirdParty/Apple/MetalShaderConverter/Include/common")
            });

			PublicDefinitions.Add("METALFX_PLUGIN_ENABLED = 1");

			//MetalFX Type 0 = Obj-C Native
			//iOS Version이 급격히 바뀐 경우 등에 사용

			//MetalFX Type 1 = MetalCPP Wrapper
			//안정된 디버그가 필요한 경우 등의 상황에서 사용
			
			//Must Select ONE, Not TOGETHER.
			PublicDefinitions.Add("METALFX_NATIVE=0");
			PublicDefinitions.Add("METALFX_METALCPP=1");
		}
		else
		{
			PublicDefinitions.Add("METALFX_PLUGIN_ENABLED=0");
  			PublicDefinitions.Add("WITH_METALFX_TARGET_MAC=0");
    		PublicDefinitions.Add("WITH_METALFX_TARGET_IOS=0");
			PublicDefinitions.Add("METALFX_NATIVE=0");
			PublicDefinitions.Add("METALFX_METALCPP=0");
		}
		
		//------------------MetalFX Handling Branch------------------ (End)
	}
}
