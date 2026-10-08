// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCADATypes.generated.h"

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
	/** 圆形：端点处画一个直径等于线宽、向外凸出的实心半圆 */
	Round UMETA(DisplayName = "圆形 Round")
};
