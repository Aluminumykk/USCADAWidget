// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADACanvasSlot.h"

#include "Widgets/SCADALineWidget.h"
#include "Widgets/SCADAPolylineWidget.h"
#include "Widgets/SCADAPolygonWidget.h"
#include "Widgets/SCADAEllipseWidget.h"
#include "Widgets/SCADACircleWidget.h"
#include "Widgets/SCADARectangleWidget.h"

#if WITH_EDITOR
void USCADACanvasSlot::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NotifyContentSlotGeometryChanged();
}

void USCADACanvasSlot::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	NotifyContentSlotGeometryChanged();
}

bool USCADACanvasSlot::DragDropPreviewByDesigner(const FVector2D& LocalCursorPosition, const TOptional<int32>& XGridSnapSize, const TOptional<int32>& YGridSnapSize)
{
	// Super 内部直接 SetPosition/SetSize（不走属性通知），之后主动通知内容控件，
	// 让新拖放的控件在放置当下就把几何数据认领到落点（而不是靠 Tick 轮询兜底，
	// 那样 archetype 认领会晚于预览实例创建，松手瞬间显示仍在原点）。
	const bool bChanged = Super::DragDropPreviewByDesigner(LocalCursorPosition, XGridSnapSize, YGridSnapSize);
	NotifyContentSlotGeometryChanged();
	return bChanged;
}

bool USCADACanvasSlot::NudgeByDesigner(const FVector2D& NudgeDirection, const TOptional<int32>& GridSnapSize)
{
	// 方向键微调固定 1px 步进：设计器传入的 NudgeDirection 已是 方向×GridSnapSize
	// （SDesignerView::OnKeyDown 直接乘网格大小），只取符号、步长恒为 1；
	// 也不做引擎原版的网格吸附取整和拉伸锚点尺寸联动（图元数据是唯一真值，微调只平移、不缩放）。
	if (NudgeDirection.IsNearlyZero())
	{
		return false;
	}
	const FVector2D Step(
		NudgeDirection.X > 0.0 ? 1.0 : (NudgeDirection.X < 0.0 ? -1.0 : 0.0),
		NudgeDirection.Y > 0.0 ? 1.0 : (NudgeDirection.Y < 0.0 ? -1.0 : 0.0));
	SetPosition(GetPosition() + Step);
	NotifyContentSlotGeometryChanged();
	return true;
}
#endif

void USCADACanvasSlot::NotifyContentSlotGeometryChanged()
{
	if (USCADALineWidget* Line = Cast<USCADALineWidget>(Content))
	{
		Line->ForceSyncFromSlotRect();
	}
	else if (USCADAPolylineWidget* Polyline = Cast<USCADAPolylineWidget>(Content))
	{
		Polyline->ForceSyncFromSlotRect();
	}
	else if (USCADAPolygonWidget* Polygon = Cast<USCADAPolygonWidget>(Content))
	{
		Polygon->ForceSyncFromSlotRect();
	}
	else if (USCADAEllipseWidget* Ellipse = Cast<USCADAEllipseWidget>(Content))
	{
		Ellipse->ForceSyncFromSlotRect();
	}
	else if (USCADACircleWidget* Circle = Cast<USCADACircleWidget>(Content))
	{
		Circle->ForceSyncFromSlotRect();
	}
	else if (USCADARectangleWidget* Rectangle = Cast<USCADARectangleWidget>(Content))
	{
		Rectangle->ForceSyncFromSlotRect();
	}
}
