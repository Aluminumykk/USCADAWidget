// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCADAWidgetTypes.generated.h"

/** 线条样式（与 WinCC 线型下拉框对应） */
UENUM(BlueprintType)
enum class ESCADALineStyle : uint8
{
	Solid      UMETA(DisplayName = "实线 Solid"),
	Dashed     UMETA(DisplayName = "虚线 Dashed"),
	Dotted     UMETA(DisplayName = "点 Dotted"),
	DashDot    UMETA(DisplayName = "点划线 Dash-Dot"),
	DashDotDot UMETA(DisplayName = "双点划线 Dash-Dot-Dot")
};

/** 线端装饰样式（起点、终点独立设置，对应 WinCC 的“线起始端样式 / 线端样式”） */
UENUM(BlueprintType)
enum class ESCADALineEndStyle : uint8
{
	/** 默认 = 无装饰 */
	None  UMETA(DisplayName = "默认 None"),
	/** 实心填充箭头 */
	Arrow UMETA(DisplayName = "箭头 Arrow")
};

/** 线端形状 */
UENUM(BlueprintType)
enum class ESCADALineCap : uint8
{
	/** 无：端点平切 */
	None  UMETA(DisplayName = "无 None"),
	/** 圆形：端点处画一个直径等于线宽、向外凸出的实心圆盘 */
	Round UMETA(DisplayName = "圆形 Round")
};

/** 多边形填充图案（对应 WinCC 的“背景填充图案”） */
UENUM(BlueprintType)
enum class ESCADAFillPattern : uint8
{
	/** 实心填充 */
	Solid UMETA(DisplayName = "实心 Solid"),
	/** 透明（不填充，只画边框） */
	Transparent UMETA(DisplayName = "透明 Transparent")
};

/** 画布缩放模式（分辨率自适应）：以 CanvasSize 为设计基准，按实际分配尺寸自动算缩放系数 */
UENUM(BlueprintType)
enum class ESCADACanvasScaleMode : uint8
{
	/** 不自动缩放：使用手动 Zoom 系数（默认，旧行为） */
	None    UMETA(DisplayName = "无（手动 Zoom） None"),
	/** 等比缩放，设计区域完整显示在可视区域内（可能留白边） */
	Fit     UMETA(DisplayName = "等比适应 Fit"),
	/** 等比缩放填满可视区域（超出部分被裁切） */
	Fill    UMETA(DisplayName = "等比填满 Fill"),
	/** 非等比拉伸铺满可视区域：0,0→CanvasSize 恒等于可视区域四角（画面比例可能变形） */
	Stretch UMETA(DisplayName = "拉伸铺满 Stretch")
};
