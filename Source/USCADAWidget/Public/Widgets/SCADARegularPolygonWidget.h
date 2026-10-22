// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "Widgets/SCADACircleWidget.h"
#include "SCADARegularPolygonWidget.generated.h"

class SSCADARegularPolygon;

/**
 * SCADA 正多边形控件（正三角形/正六边形等；为旋转动画元件服务——泵叶轮、风扇）。
 * 数据模型 = Center（圆心）+ Radius（外接圆半径）+ Sides（边数）+ StartAngle（起始角），
 * 顶点由参数推导、**不参与数据层像素取整**——正三角形（边长比含 √3）等无理数图形因此精确存在。
 * 槽矩形恒等于外接圆的外切正方形 (Center - R, 2R)：**槽包围盒中心恒等于多边形几何中心**，
 * RenderTransform 旋转轴心（Pivot 0.5,0.5）精确落在几何中心，旋转动画零晃动
 * （普通三角形/任意多边形的槽包围盒中心 ≠ 几何中心，绕 Pivot 旋转会晃）。
 * 继承圆控件：槽双向同步/绝对映射/逐轴锚定/拖边角行为与圆完全一致；
 * 无逐顶点手柄（顶点是推导值，不可逐点编辑）。
 * 旋转动画请用 RenderTransformAngle（UMG Animation 驱动），Sides/StartAngle 是形状参数不做动画。
 */
UCLASS(meta = (DisplayName = "SCADA Regular Polygon", Category = "SCADA"))
class USCADAWIDGET_API USCADARegularPolygonWidget : public USCADACircleWidget
{
	GENERATED_BODY()

public:
	/** 边数（3~64；3 = 正三角形，6 = 正六边形） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|RegularPolygon", meta = (ClampMin = "3", ClampMax = "64"))
	int32 Sides = 3;

	/** 起始角（度）：第一个顶点相对圆心的方向，-90 = 顶点朝上（屏幕坐标 Y 向下，角度顺时针递增） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|RegularPolygon", meta = (Delta = "1.0", LinearDeltaSensitivity = "1"))
	float StartAngle = -90.0f;

	/** 读取边数（纯读取不绕过封装，写请走 Set*） */
	UFUNCTION(BlueprintPure, Category = "SCADA|RegularPolygon")
	int32 GetSides() const { return Sides; }

	/** 读取起始角 */
	UFUNCTION(BlueprintPure, Category = "SCADA|RegularPolygon")
	float GetStartAngle() const { return StartAngle; }

	UFUNCTION(BlueprintCallable, Category = "SCADA|RegularPolygon")
	void SetSides(int32 InSides);

	UFUNCTION(BlueprintCallable, Category = "SCADA|RegularPolygon")
	void SetStartAngle(float InStartAngle);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;

	/** 数据层对齐：Sides 钳 ≥3，StartAngle 取整（与面板显示一致） */
	virtual void SnapPropertiesToPixel() override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

private:
	TSharedPtr<SSCADARegularPolygon> GetRegularPolygon() const;
};
