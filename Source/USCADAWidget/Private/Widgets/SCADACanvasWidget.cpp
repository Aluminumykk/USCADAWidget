// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADACanvasWidget.h"

#include "Slate/SSCADACanvas.h"
#include "Widgets/SCADACanvasSlot.h"
#include "Widgets/SCADAPrimitiveWidgetBase.h"
#include "Components/CanvasPanelSlot.h"

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

void USCADACanvasWidget::SetCanvasSize(FVector2D InSize)
{
	CanvasSize = InSize;
	if (TSharedPtr<SSCADACanvas> Canvas = GetSCADACanvas())
	{
		Canvas->SetDesignSize(CanvasSize);
	}
}

void USCADACanvasWidget::SetScaleMode(ESCADACanvasScaleMode InMode)
{
	ScaleMode = InMode;
	if (TSharedPtr<SSCADACanvas> Canvas = GetSCADACanvas())
	{
		Canvas->SetScaleMode(ScaleMode);
	}
}

FVector2D USCADACanvasWidget::GetEffectiveCanvasScale() const
{
	if (TSharedPtr<SSCADACanvas> Canvas = GetSCADACanvas())
	{
		return Canvas->GetLastEffectiveScale();
	}
	return FVector2D(FMath::Max(Zoom, 0.01f));
}

void USCADACanvasWidget::FitCanvasToContent()
{
	// 无子控件 → 什么都不做（空画布保持原尺寸，不误触把画布缩成 1×1）
	if (Slots.Num() == 0)
	{
		return;
	}

	// 1) 汇总内容并集包围盒：图元取几何数据包围盒（数据层是唯一真值，槽矩形恒等于它）；
	//    普通控件（Text、嵌套画布等非图元基类子类）取其槽位置 + 尺寸。
	//    初始值取首个有效子内容（5.7 的 TVector2 无 Max()/Lowest() 哨兵静态函数，不能用极值初始化）
	FVector2D BoundsMin = FVector2D::ZeroVector;
	FVector2D BoundsMax = FVector2D::ZeroVector;
	bool bHasContent = false;

	for (UPanelSlot* PanelSlot : Slots)
	{
		UWidget* Child = (PanelSlot != nullptr && PanelSlot->Content != nullptr) ? PanelSlot->Content : nullptr;
		if (Child == nullptr)
		{
			continue;
		}

		FVector2D Min, Max;
		if (USCADAPrimitiveWidgetBase* Primitive = Cast<USCADAPrimitiveWidgetBase>(Child))
		{
			Primitive->GetGeometryBoundsPublic(Min, Max);
		}
		else if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Child->Slot))
		{
			// 普通控件允许负尺寸槽，按逐轴 min/max 规范化
			const FVector2D Pos = CanvasSlot->GetPosition();
			const FVector2D Corner = Pos + CanvasSlot->GetSize();
			Min = FVector2D(FMath::Min(Pos.X, Corner.X), FMath::Min(Pos.Y, Corner.Y));
			Max = FVector2D(FMath::Max(Pos.X, Corner.X), FMath::Max(Pos.Y, Corner.Y));
		}
		else
		{
			continue;
		}

		if (bHasContent)
		{
			BoundsMin = FVector2D::Min(BoundsMin, Min);
			BoundsMax = FVector2D::Max(BoundsMax, Max);
		}
		else
		{
			BoundsMin = Min;
			BoundsMax = Max;
			bHasContent = true;
		}
	}

	// 全部子槽都取不到（理论兜底）：同样什么都不做
	if (!bHasContent)
	{
		return;
	}

	// 2) 内容尺寸 = 包围盒尺寸，逐轴 Clamp ≥1（点/单点线等退化图元包围盒可能为零）
	FVector2D ContentSize = BoundsMax - BoundsMin;
	ContentSize.X = FMath::Max(ContentSize.X, 1.0);
	ContentSize.Y = FMath::Max(ContentSize.Y, 1.0);

	// 3) 整体平移使内容包围盒左上角落到 (0,0)
	const FVector2D Delta = -BoundsMin;
	if (!Delta.IsNearlyZero())
	{
		for (UPanelSlot* PanelSlot : Slots)
		{
			UWidget* Child = (PanelSlot != nullptr && PanelSlot->Content != nullptr) ? PanelSlot->Content : nullptr;
			if (Child == nullptr)
			{
				continue;
			}

			if (USCADAPrimitiveWidgetBase* Primitive = Cast<USCADAPrimitiveWidgetBase>(Child))
			{
				// 图元必须走正向同步通道（几何数据 + 槽矩形 + 槽缓存一起动），
				// 否则下一拍 CheckSlotSync 会把只动了槽/只动了数据的差态当成设计器输入做映射
				Primitive->TranslateGeometryAndSync(Delta);
			}
			else if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Child->Slot))
			{
				// 普通控件没有槽同步状态机，直接写槽安全
				CanvasSlot->SetPosition(CanvasSlot->GetPosition() + Delta);
			}
		}
	}

	// 4) 画布收缩到内容尺寸（四周接壤、无留白）
	SetCanvasSize(ContentSize);
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
		Canvas->SetDesignSize(CanvasSize);
		Canvas->SetScaleMode(ScaleMode);
	}
}

#if WITH_EDITOR
const FText USCADACanvasWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
