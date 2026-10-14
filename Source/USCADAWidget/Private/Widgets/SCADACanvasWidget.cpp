// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADACanvasWidget.h"

#include "Slate/SSCADACanvas.h"
#include "Widgets/SCADACanvasSlot.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedPtr<SSCADACanvas> USCADACanvasWidget::GetSCADACanvas() const
{
	// RebuildWidget 只会创建 SSCADACanvas（SConstraintCanvas 子类），静态转换安全
	return StaticCastSharedPtr<SSCADACanvas>(MyCanvas);
}

void USCADACanvasWidget::SetShowGrid(bool bInShowGrid)
{
	bShowGrid = bInShowGrid;
	if (TSharedPtr<SSCADACanvas> Canvas = GetSCADACanvas())
	{
		Canvas->SetShowGrid(bShowGrid);
	}
}

void USCADACanvasWidget::SetGridSize(float InGridSize)
{
	GridSize = InGridSize;
	if (TSharedPtr<SSCADACanvas> Canvas = GetSCADACanvas())
	{
		Canvas->SetGridSize(GridSize);
	}
}

void USCADACanvasWidget::SetGridColor(FLinearColor InColor)
{
	GridColor = InColor;
	if (TSharedPtr<SSCADACanvas> Canvas = GetSCADACanvas())
	{
		Canvas->SetGridColor(GridColor);
	}
}

void USCADACanvasWidget::SetZoom(float InZoom)
{
	Zoom = InZoom;
	if (TSharedPtr<SSCADACanvas> Canvas = GetSCADACanvas())
	{
		Canvas->SetZoom(Zoom);
	}
}

UClass* USCADACanvasWidget::GetSlotClass() const
{
	return USCADACanvasSlot::StaticClass();
}

TSharedRef<SWidget> USCADACanvasWidget::RebuildWidget()
{
	// 不调 Super（基类会创建普通 SConstraintCanvas）。创建 SSCADACanvas 并赋给**基类**
	// MyCanvas——GetCanvasWidget()/GetGeometryForSlot()/UCanvasPanelSlot::SaveBaseLayout/
	// RebaseLayout（设计器本体拖动、锚点 gizmo）都依赖基类成员有效。
	TSharedRef<SSCADACanvas> Canvas = SNew(SSCADACanvas);
	MyCanvas = Canvas;

	for (UPanelSlot* PanelSlot : Slots)
	{
		if (UCanvasPanelSlot* TypedSlot = Cast<UCanvasPanelSlot>(PanelSlot))
		{
			TypedSlot->Parent = this;
			TypedSlot->BuildSlot(Canvas);
		}
	}

	return Canvas;
}

void USCADACanvasWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (TSharedPtr<SSCADACanvas> Canvas = GetSCADACanvas())
	{
		Canvas->SetShowGrid(bShowGrid);
		Canvas->SetGridSize(GridSize);
		Canvas->SetGridColor(GridColor);
		Canvas->SetZoom(Zoom);
	}
}

#if WITH_EDITOR
const FText USCADACanvasWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
