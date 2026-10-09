// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SSCADAPolygon.h"
#include "SCADATypes.h"

/**
 * 底层 Slate 椭圆控件（对应 WinCC 的“椭圆”对象）。
 * 继承 SSCADAPolygon：X/Y 半径变化时把椭圆周按角度均布采样成闭合周点列写入 Points，
 * 填充（凸多边形耳切必成功）、实线边框+连接盘、虚线圆角采样、闪烁全部复用父类管线。
 * 周点列是控件本地空间：中心恒为 (RadiusX, RadiusY)（槽矩形 = 椭圆包围盒）。
 * 采样数自适应周长（约每 4px 一段，Clamp 32~256），粗线/大半径下圆仍光滑。
 */
class USCADAWIDGET_API SSCADAEllipse : public SSCADAPolygon
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
