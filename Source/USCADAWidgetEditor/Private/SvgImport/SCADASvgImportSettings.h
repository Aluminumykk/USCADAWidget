// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SCADASvgImportSettings.generated.h"

/**
 * SVG 导入设置（Project Settings → Engine → SCADA SVG Import，编辑器模块自动发现注册）。
 * 作用于 .svg 拖入内容浏览器生成 Widget Blueprint 的导入过程（USCADASvgImportFactory）。
 */
UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "SCADA SVG Import"))
class USCADASvgImportSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 按内容包围盒裁剪 viewBox：导入后画布尺寸 = 图形实际范围（而不是 SVG 声明的 viewBox），
	 *  消除图形四周的留白（很多元件库文件的 viewBox 远大于实际图形）。
	 *  裁剪含描边半宽与 CropPadding 余量；分组的旋转动画锚点（Pivot）保持在组容器本地空间，不受裁剪影响。 */
	UPROPERTY(config, EditAnywhere, Category = "SVG Import")
	bool bCropViewBoxToContent = true;

	/** 裁剪时在内容包围盒四周额外保留的边距（SVG 坐标单位，可为负进一步收紧） */
	UPROPERTY(config, EditAnywhere, Category = "SVG Import", meta = (EditCondition = "bCropViewBoxToContent"))
	float CropPadding = 0.0f;
};
