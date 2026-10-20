// Copyright Pr_UEDraw. All Rights Reserved.

using UnrealBuildTool;

public class USCADAWidget : ModuleRules
{
	public USCADAWidget(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			// SCADAVector2DBinding 使用 FCachedPropertyPath/PropertyPathHelpers（经 UMG 公共头传递包含，
			// 但 UMG 的公共依赖不提供导入库，链接需显式声明）
			"PropertyPath"
		});
	}
}
