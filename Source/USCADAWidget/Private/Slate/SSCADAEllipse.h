// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SSCADAPolygon.h"
#include "SCADAWidgetTypes.h"

/**
 * 底层 Slate 椭圆控件（对应 WinCC 的“椭圆”对象）。
 * 继承 SSCADAPolygon：X/Y 半径变化时把椭圆周按角度均布采样成闭合周点列写入 Points，
 * 填充（凸多边形耳切必成功）、实线边框（环带四边形条带 + 轮廓 AA 覆层）、虚线圆角采样、
 * 闪烁全部复用父类管线；正圆（RadiusX=RadiusY）走父类的 RoundedBox SDF 解析快速路径。
 * 周点列是控件本地空间：中心恒为 (RadiusX, RadiusY)（槽矩形 = 椭圆包围盒）。
 * 采样数自适应周长（约每 4px 一段，Clamp 32~256），粗线/大半径下圆仍光滑。
 */
class SSCADAEllipse : public SSCADAPolygon
{
public:
	SLATE_BEGIN_ARGS(SSCADAEllipse)
		: _RadiusX(50.0f)
		, _RadiusY(30.0f)
	{
	}
		SLATE_ARGUMENT(float, RadiusX)
		SLATE_ARGUMENT(float, RadiusY)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** X 半径（本地空间，像素，≥1） */
	void SetRadiusX(float InRadiusX);
	/** Y 半径（本地空间，像素，≥1） */
	void SetRadiusY(float InRadiusY);
	/** 显式指定本地中心（默认 = (RadiusX, RadiusY)，即槽左上角锚定）。
	 *  圆控件在设计器拖动中槽不是正方形，圆心不一定在槽内 (R,R) 处，由控件层换算后传入。 */
	void SetLocalCenter(FVector2D InCenter);

	/** 批量几何推送：一次设置两轴半径 + 显式本地中心，只重采样一次
	 *  （等价于 SetRadiusX + SetRadiusY + SetLocalCenter 连调，消除 UMG 层成串推送的重复采样） */
	void SetGeometry(float InRadiusX, float InRadiusY, FVector2D InLocalCenter);

protected:
	/** RadiusX=RadiusY（正圆）时报告解析几何：SDF 精确整圆（数据层半径为整数，两半径恒同步，
	 *  圆控件走这条路径；椭圆控件非等轴时自动回退采样管线）。 */
	virtual bool TryGetAnalyticBox(FAnalyticBox& OutBox) const override;

	/** 椭圆轮廓是曲线：绘制层不做整数取整（见基类说明） */
	virtual bool PaintsSmoothContour() const override { return true; }

private:
	/** 按当前半径/本地中心重采样椭圆周点列并写入 Points */
	void ResamplePerimeter();

private:
	float RadiusX = 50.0f;
	float RadiusY = 30.0f;
	/** 本地中心（默认跟随半径 = 槽左上角锚定；SetLocalCenter 后变为显式指定） */
	FVector2D LocalCenter = FVector2D(50.0f, 30.0f);
	bool bCenterExplicit = false;
};
