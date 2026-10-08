// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADACanvasWidget.h"

#include "Slate/SSCADACanvas.h"
#include "Widgets/SCADACanvasSlot.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

UCanvasPanelSlot* USCADACanvasWidget::AddChildToCanvas(UWidget* Content)
{
	return Cast<UCanvasPanelSlot>(AddChild(Content));
}

void USCADACanvasWidget::SetShowGrid(bool bInShowGrid)
{
	bShowGrid = bInShowGrid;
	if (MyCanvas.IsValid())
	{
		MyCanvas->SetShowGrid(bShowGrid);
	}
}

void USCADACanvasWidget::SetGridSize(float InGridSize)
{
	GridSize = InGridSize;
	if (MyCanvas.IsValid())
	{
		MyCanvas->SetGridSize(GridSize);
	}
}

void USCADACanvasWidget::SetGridColor(FLinearColor InColor)
{
	GridColor = InColor;
	if (MyCanvas.IsValid())
	{
		MyCanvas->SetGridColor(GridColor);
	}
}

void USCADACanvasWidget::SetZoom(float InZoom)
{
	Zoom = InZoom;
	if (MyCanvas.IsValid())
	{
		MyCanvas->SetZoom(Zoom);
	}
}

void USCADACanvasWidget::OnSlotAdded(UPanelSlot* InSlot)
{
	if (MyCanvas.IsValid())
	{
		CastChecked<UCanvasPanelSlot>(InSlot)->BuildSlot(MyCanvas.ToSharedRef());
	}
}

void USCADACanvasWidget::OnSlotRemoved(UPanelSlot* InSlot)
{
	if (MyCanvas.IsValid())
	{
		TSharedPtr<SWidget> Widget = InSlot->Content->GetCachedWidget();
		if (Widget.IsValid())
		{
			MyCanvas->RemoveSlot(Widget.ToSharedRef());
		}
	}
}

UClass* USCADACanvasWidget::GetSlotClass() const
{
	return USCADACanvasSlot::StaticClass();
}

TSharedRef<SWidget> USCADACanvasWidget::RebuildWidget()
{
	MyCanvas = SNew(SSCADACanvas);

	for (UPanelSlot* PanelSlot : Slots)
	{
		if (UCanvasPanelSlot* TypedSlot = Cast<UCanvasPanelSlot>(PanelSlot))
		{
			TypedSlot->Parent = this;
			TypedSlot->BuildSlot(MyCanvas.ToSharedRef());
		}
	}

	return MyCanvas.ToSharedRef();
}

void USCADACanvasWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (MyCanvas.IsValid())
	{
		MyCanvas->SetShowGrid(bShowGrid);
		MyCanvas->SetGridSize(GridSize);
		MyCanvas->SetGridColor(GridColor);
		MyCanvas->SetZoom(Zoom);
	}
}

void USCADACanvasWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyCanvas.Reset();
}

#if WITH_EDITOR
const FText USCADACanvasWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
