// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SSCADAPolygon.h"
#include "SCADATypes.h"

/**
 * 底层 Slate 矩形控件（对应 WinCC 的“矩形”对象，支持圆角）。
 * 继承 SSCADAPolygon：尺寸/圆角变化时把（圆角）矩形周按“4 边 + 4 个椭圆角弧”
 * 采样成周点列写入 Points，填充、实线边框+连接盘、虚线圆角采样、闪烁全部复用父类管线。
 * 周点列是控件本地空间：矩形恒为 (0, 0, Width, Height)（槽矩形 = 控件矩形）。
 * 圆角半径 = WinCC 的“圆角宽度/圆角高度”（椭圆角弧的两轴，0 = 直角）；
 * 每角弧采样数自适应弧长（约每 4px 一段，Clamp 8~64）。
 */
class USCADAWIDGET_API SSCADARectangle : public SSCADAPolygon
{
public:
	SLATE_BEGIN_ARGS(SSCADARectangle)
		: _Width(100.0f)
		, _Height(60.0f)
		, _CornerRadiusX(0.0f)
		, _CornerRadiusY(0.0f)
	{
	}
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(float, CornerRadiusX)
		SLATE_ARGUMENT(float, CornerRadiusY)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 宽（本地空间，像素，≥1） */
	void SetWidth(float InWidth);
	/** 高（本地空间，像素，≥1） */
	void SetHeight(float InHeight);
	/** 圆角宽度（角弧 X 半径，像素，0 = 直角，≤ min(宽,高)/2） */
	void SetCornerRadiusX(float InRadiusX);
	/** 圆角高度（角弧 Y 半径，像素，0 = 直角，≤ min(宽,高)/2） */
	void SetCornerRadiusY(float InRadiusY);

private:
	/** 按当前尺寸/圆角重采样周点列并写入 Points（本地空间 (0,0,Width,Height)） */
	void ResamplePerimeter();

private:
	float Width = 100.0f;
	float Height = 60.0f;
	float CornerRadiusX = 0.0f;
	float CornerRadiusY = 0.0f;
};
