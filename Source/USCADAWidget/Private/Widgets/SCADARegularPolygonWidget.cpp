// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADARegularPolygonWidget.h"

#include "Slate/SSCADARegularPolygon.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADARegularPolygonWidget::RebuildWidget()
{
	MyCircle = SNew(SSCADARegularPolygon);

	// 与圆控件同理：挂接 Slate Tick 回调 + 注册核心 Ticker 兜底（编辑器双实例各自 Tick）
	SetupSlotSyncCallbacks(MyCircle.Get());

	return MyCircle.ToSharedRef();
}

void USCADARegularPolygonWidget::SynchronizeProperties()
{
	// 父类完成：槽对齐到几何数据 + 半径/本地中心推送（SetGeometry 触发一次重采样）+ 颜色/线型/闪烁推送
	Super::SynchronizeProperties();

	// 形状参数在几何推送之后写入，各触发一次重采样（Sides/StartAngle 编辑频率低，不做合并优化）
	if (TSharedPtr<SSCADARegularPolygon> Poly = GetRegularPolygon())
	{
		Poly->SetSides(Sides);
		Poly->SetStartAngle(StartAngle);
	}
}

void USCADARegularPolygonWidget::SnapPropertiesToPixel()
{
	Super::SnapPropertiesToPixel();

	Sides = FMath::Max(Sides, 3);
	StartAngle = FMath::RoundToFloat(StartAngle);
}

void USCADARegularPolygonWidget::SetSides(int32 InSides)
{
	Sides = FMath::Max(InSides, 3);

	if (TSharedPtr<SSCADARegularPolygon> Poly = GetRegularPolygon())
	{
		Poly->SetSides(Sides);
	}
}

void USCADARegularPolygonWidget::SetStartAngle(float InStartAngle)
{
	StartAngle = FMath::RoundToFloat(InStartAngle);

	if (TSharedPtr<SSCADARegularPolygon> Poly = GetRegularPolygon())
	{
		Poly->SetStartAngle(StartAngle);
	}
}

#if WITH_EDITOR
const FText USCADARegularPolygonWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

TSharedPtr<SSCADARegularPolygon> USCADARegularPolygonWidget::GetRegularPolygon() const
{
	// RebuildWidget 创建的实际类型恒为 SSCADARegularPolygon（MyCircle 声明类型是父类 SSCADAEllipse）
	return StaticCastSharedPtr<SSCADARegularPolygon>(MyCircle);
}

#undef LOCTEXT_NAMESPACE
