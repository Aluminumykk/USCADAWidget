// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SSCADAPolygon.h"
#include "SCADAWidgetTypes.h"

/**
 * 底层 Slate 矩形控件（对应 WinCC 的“矩形”对象，支持圆角）。
 * 继承 SSCADAPolygon：尺寸/圆角变化时把（圆角）矩形周按“4 边 + 4 个椭圆角弧”
 * 采样成周点列写入 Points，填充、实线边框（环带四边形条带 + 轮廓 AA 覆层）、虚线圆角采样、
 * 闪烁全部复用父类管线；直角或等半径圆角（RX=RY）走父类的 RoundedBox SDF 解析快速路径。
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

	/** 批量几何推送：一次设置宽/高/两轴圆角半径，只重采样一次
	 *  （等价于 SetWidth + SetHeight + SetCornerRadiusX + SetCornerRadiusY 连调，消除 UMG 层成串推送的重复采样） */
	void SetGeometry(float InWidth, float InHeight, float InCornerRX, float InCornerRY);

protected:
	/** 直角（两轴圆角任一为 0）或等半径圆角（RX=RY）时报告解析几何：SDF 直角/等半径圆角矩形
	 *  （两轴椭圆角 SDF 每角单标量表示不了，自动回退采样管线）。 */
	virtual bool TryGetAnalyticBox(FAnalyticBox& OutBox) const override;

	/** 存在椭圆角弧采样时轮廓含曲线，绘制层不做整数取整（直边端点本就是整数，不受影响） */
	virtual bool PaintsSmoothContour() const override { return CornerRadiusX > 0.0f && CornerRadiusY > 0.0f; }

private:
	/** 按当前尺寸/圆角重采样周点列并写入 Points（本地空间 (0,0,Width,Height)） */
	void ResamplePerimeter();

private:
	float Width = 100.0f;
	float Height = 60.0f;
	float CornerRadiusX = 0.0f;
	float CornerRadiusY = 0.0f;
};
