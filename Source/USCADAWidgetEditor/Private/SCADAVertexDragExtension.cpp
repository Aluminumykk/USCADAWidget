// Copyright Pr_UEDraw. All Rights Reserved.

#include "SCADAVertexDragExtension.h"

#include "Widgets/SCADACanvasWidget.h"
#include "Widgets/SCADALineWidget.h"
#include "Widgets/SCADAPolylineWidget.h"

#include "Components/Widget.h"
#include "IUMGDesigner.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "USCADAWidgetEditor"

FSCADAVertexDragExtension::FSCADAVertexDragExtension()
{
	ExtensionId = FName(TEXT("SCADAVertexDrag"));
}

bool FSCADAVertexDragExtension::CanExtendSelection(const TArray<FWidgetReference>& Selection) const
{
	if (Selection.Num() != 1)
	{
		return false;
	}
	const UWidget* Template = Selection[0].GetTemplate();
	return Template != nullptr &&
		(Template->IsA<USCADAPolylineWidget>() || Template->IsA<USCADALineWidget>());
}

void FSCADAVertexDragExtension::ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements)
{
	SelectionCache = Selection;

	HandleWidgets.Reset();
	for (int32 Index = 0; Index < HandlePoolSize; ++Index)
	{
		TSharedPtr<SWidget> Handle =
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
			.Visibility(this, &FSCADAVertexDragExtension::GetHandleVisibility, Index)
			.OnMouseButtonDown(this, &FSCADAVertexDragExtension::HandleBeginDrag, Index)
			.OnMouseMove(this, &FSCADAVertexDragExtension::HandleDragging, Index)
			.OnMouseButtonUp(this, &FSCADAVertexDragExtension::HandleEndDrag, Index)
			.Padding(FMargin(0))
			[
				SNew(SBox)
				.WidthOverride(HandleSize)
				.HeightOverride(HandleSize)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(this, &FSCADAVertexDragExtension::GetHandleColor, Index)
				]
			];

		Handle->SlatePrepass();
		HandleWidgets.Add(Handle);

		TAttribute<FVector2D> Offset = TAttribute<FVector2D>::Create(
			TAttribute<FVector2D>::FGetter::CreateSP(SharedThis(this), &FSCADAVertexDragExtension::GetHandleOffset, Index));
		SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(
			Handle.ToSharedRef(), EExtensionLayoutLocation::RelativeFromParent, Offset, FVector2D(0, 0)));
	}
}

UWidget* FSCADAVertexDragExtension::GetTemplateWidget() const
{
	return SelectionCache.Num() == 1 ? SelectionCache[0].GetTemplate() : nullptr;
}

UWidget* FSCADAVertexDragExtension::GetPreviewWidget() const
{
	return SelectionCache.Num() == 1 ? SelectionCache[0].GetPreview() : nullptr;
}

int32 FSCADAVertexDragExtension::GetVertexCount() const
{
	if (const USCADAPolylineWidget* Polyline = Cast<USCADAPolylineWidget>(GetTemplateWidget()))
	{
		return Polyline->Points.Num();
	}
	if (GetTemplateWidget() != nullptr && GetTemplateWidget()->IsA<USCADALineWidget>())
	{
		return 2;
	}
	return 0;
}

bool FSCADAVertexDragExtension::GetVertex(int32 Index, FVector2D& OutDesignPos) const
{
	if (const USCADAPolylineWidget* Polyline = Cast<USCADAPolylineWidget>(GetTemplateWidget()))
	{
		if (Polyline->Points.IsValidIndex(Index))
		{
			OutDesignPos = Polyline->Points[Index];
			return true;
		}
		return false;
	}
	if (const USCADALineWidget* Line = Cast<USCADALineWidget>(GetTemplateWidget()))
	{
		if (Index == 0)
		{
			OutDesignPos = Line->StartPoint;
			return true;
		}
		if (Index == 1)
		{
			OutDesignPos = Line->EndPoint;
			return true;
		}
	}
	return false;
}

void FSCADAVertexDragExtension::SetVertexOnInstance(UWidget* Widget, int32 Index, const FVector2D& NewDesignPos) const
{
	if (USCADAPolylineWidget* Polyline = Cast<USCADAPolylineWidget>(Widget))
	{
		if (Polyline->Points.IsValidIndex(Index))
		{
			TArray<FVector2D> NewPoints = Polyline->Points;
			NewPoints[Index] = NewDesignPos;
			Polyline->SetPoints(NewPoints);
		}
		return;
	}
	if (USCADALineWidget* Line = Cast<USCADALineWidget>(Widget))
	{
		if (Index == 0)
		{
			Line->SetStartPoint(NewDesignPos);
		}
		else if (Index == 1)
		{
			Line->SetEndPoint(NewDesignPos);
		}
	}
}

float FSCADAVertexDragExtension::GetCanvasZoom() const
{
	if (const UWidget* Preview = GetPreviewWidget())
	{
		if (const USCADACanvasWidget* Canvas = Cast<USCADACanvasWidget>(Preview->GetParent()))
		{
			return FMath::Max(Canvas->Zoom, 0.01f);
		}
	}
	return 1.0f;
}

bool FSCADAVertexDragExtension::GetParentGeometry(FGeometry& OutGeometry) const
{
	return Designer != nullptr && SelectionCache.Num() == 1 &&
		Designer->GetWidgetParentGeometry(SelectionCache[0], OutGeometry);
}

FVector2D FSCADAVertexDragExtension::DesignToHandleOffset(const FVector2D& DesignPos) const
{
	FGeometry ParentGeometry;
	if (Designer == nullptr || !GetParentGeometry(ParentGeometry))
	{
		return FVector2D::ZeroVector;
	}

	// 画布设计坐标 P → 画布本地坐标 P*Zoom（SSCADACanvas 以 MakeChild(FScale2f(Zoom)) 排布子内容），
	// 再换算到设计器表面坐标（与 SDesignerView::GetExtensionPosition 的 RelativeFromParent 同一空间：
	// FinalPosition = DesignerLocal(画布原点) + Offset）。减去半个手柄尺寸使手柄居中于顶点。
	const float Zoom = GetCanvasZoom();
	const FGeometry DesignerGeometry = Designer->GetDesignerGeometry();
	const FVector2D VertexAbsolute = ParentGeometry.LocalToAbsolute(DesignPos * Zoom);
	const FVector2D OriginAbsolute = ParentGeometry.LocalToAbsolute(FVector2D::ZeroVector);
	return DesignerGeometry.AbsoluteToLocal(VertexAbsolute) - DesignerGeometry.AbsoluteToLocal(OriginAbsolute)
		- FVector2D(HandleSize * 0.5, HandleSize * 0.5);
}

FVector2D FSCADAVertexDragExtension::ScreenToDesign(const FVector2D& ScreenPos) const
{
	FGeometry ParentGeometry;
	if (!GetParentGeometry(ParentGeometry))
	{
		return FVector2D::ZeroVector;
	}
	return ParentGeometry.AbsoluteToLocal(ScreenPos) / GetCanvasZoom();
}

FVector2D FSCADAVertexDragExtension::GetHandleOffset(int32 Index) const
{
	FVector2D Vertex;
	if (!GetVertex(Index, Vertex))
	{
		return FVector2D::ZeroVector;
	}
	return DesignToHandleOffset(Vertex);
}

EVisibility FSCADAVertexDragExtension::GetHandleVisibility(int32 Index) const
{
	FVector2D Vertex;
	return GetVertex(Index, Vertex) ? EVisibility::Visible : EVisibility::Collapsed;
}

FSlateColor FSCADAVertexDragExtension::GetHandleColor(int32 Index) const
{
	const bool bHovered = HandleWidgets.IsValidIndex(Index) && HandleWidgets[Index].IsValid() && HandleWidgets[Index]->IsHovered();
	const bool bDragged = bDragging && DragIndex == Index;
	// 拖动/悬停高亮为亮黄，常态橙色
	return (bHovered || bDragged) ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor(1.0f, 0.5f, 0.0f);
}

FReply FSCADAVertexDragExtension::HandleBeginDrag(const FGeometry& Geometry, const FPointerEvent& Event, int32 Index)
{
	UWidget* Template = GetTemplateWidget();
	FVector2D Vertex;
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton &&
		Template != nullptr && GetVertex(Index, Vertex) &&
		HandleWidgets.IsValidIndex(Index) && HandleWidgets[Index].IsValid())
	{
		BeginTransaction(LOCTEXT("MoveSCADAVertex", "Move SCADA Vertex"));
		Template->Modify();

		bDragging = true;
		DragIndex = Index;
		DragStartVertex = Vertex;
		DragStartDesignPos = ScreenToDesign(Event.GetScreenSpacePosition());

		return FReply::Handled().CaptureMouse(HandleWidgets[Index].ToSharedRef());
	}

	return FReply::Unhandled();
}

FReply FSCADAVertexDragExtension::HandleDragging(const FGeometry& Geometry, const FPointerEvent& Event, int32 Index)
{
	if (bDragging && Index == DragIndex)
	{
		// 增量映射：顶点跟随鼠标位移，不跳到光标中心
		const FVector2D NewDesignPos = ScreenToDesign(Event.GetScreenSpacePosition());
		ApplyVertexPosition(Index, DragStartVertex + (NewDesignPos - DragStartDesignPos));
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply FSCADAVertexDragExtension::HandleEndDrag(const FGeometry& Geometry, const FPointerEvent& Event, int32 Index)
{
	if (bDragging && Index == DragIndex && Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		DragIndex = INDEX_NONE;

		EndTransaction();

		if (Designer != nullptr)
		{
			Designer->MarkDesignModifed(false);
		}

		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

void FSCADAVertexDragExtension::ApplyVertexPosition(int32 Index, const FVector2D& NewDesignPos)
{
	UWidget* Template = GetTemplateWidget();
	FVector2D Current;
	if (Template == nullptr || !GetVertex(Index, Current))
	{
		return;
	}

	// 公开 Set 接口内部完成像素取整 + 槽包围盒同步 + Slate 推送；同时写 Template + Preview
	// 双实例（双实例陷阱），两边槽缓存保持一致，CheckSlotSync 不会把写回误判成设计器输入。
	// 不走属性系统（SetPropertyValue 要求 PostEditChangeProperty 公开且 PreEditChange 链式
	// 重载可见，控件类不满足）；撤销由拖动开始时的 Modify() + 事务覆盖。
	SetVertexOnInstance(Template, Index, NewDesignPos);
	if (UWidget* Preview = GetPreviewWidget())
	{
		SetVertexOnInstance(Preview, Index, NewDesignPos);
	}
}

#undef LOCTEXT_NAMESPACE
