// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SSCADAEllipse.h"

/**
 * 底层 Slate 正多边形控件（正三角形/正六边形等；为旋转动画元件服务——泵叶轮、风扇）。
 * 继承 SSCADAEllipse 的参数化骨架（半径 + 显式本地中心 → 重采样），周点列改为正 N 边形
 * 顶点（由 Sides/StartAngle/Radius/LocalCenter 推导）；填充/闭合边框/虚线/闪烁复用多边形管线。
 * 本地空间恒为外接圆的外切正方形 (0,0,2R,2R)——槽包围盒中心恒等于多边形几何中心，
 * RenderTransform 旋转轴心（Pivot 0.5,0.5）精确落在几何中心，旋转无晃动。
 * 顶点为推导浮点值、不参与数据层取整：正三角形等无理数边比图形因此精确存在。
 */
class SSCADARegularPolygon : public SSCADAEllipse
{
public:
	SLATE_BEGIN_ARGS(SSCADARegularPolygon)
		: _Sides(3)
		, _StartAngle(-90.0f)
	{
	}
		SLATE_ARGUMENT(int32, Sides)
		SLATE_ARGUMENT(float, StartAngle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 边数（≥3） */
	void SetSides(int32 InSides);
	/** 起始角（度，-90 = 第一个顶点朝上；屏幕坐标 Y 向下，角度顺时针递增） */
	void SetStartAngle(float InStartAngle);

protected:
	/** 正多边形不是圆：禁用父类正圆 SDF 解析快速路径，恒走采样多边形管线 */
	virtual bool TryGetAnalyticBox(FAnalyticBox& OutBox) const override { return false; }

	/** 稀疏顶点（N 个真顶点）：恢复整体轮廓圆角化（父类椭圆对密集采样点列关闭了它）——
	 *  正三角形等的拐角因此获得与 Line/Polyline 包络一致的圆弧过渡 */
	virtual float GetBorderCornerArcRadius() const override { return SCADAStroke::CornerArcRadius; }

	/** 边数少（三角形等直边图形）顶点取整更利落；边数多（近似圆）按平滑曲线免取整 */
	virtual bool PaintsSmoothContour() const override { return Sides >= 8; }

	/** 由 Sides/StartAngle/RadiusX/LocalCenter 生成正 N 边形顶点写入 Points */
	virtual void ResamplePerimeter() override;

private:
	int32 Sides = 3;
	float StartAngle = -90.0f;
};
