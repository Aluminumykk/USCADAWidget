// Copyright Pr_UEDraw. All Rights Reserved.

#include "SCADAVertexDragExtension.h"

#include "Widgets/SCADACanvasWidget.h"
#include "Widgets/SCADALineWidget.h"
#include "Widgets/SCADAPolygonWidget.h"
#include "Widgets/SCADAPolylineWidget.h"

#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
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
		(Template->IsA<USCADAPolylineWidget>() || Template->IsA<USCADALineWidget>() || Template->IsA<USCADAPolygonWidget>());
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
	if (const USCADAPolygonWidget* Polygon = Cast<USCADAPolygonWidget>(GetTemplateWidget()))
	{
		return Polygon->Points.Num();
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
	if (const USCADAPolygonWidget* Polygon = Cast<USCADAPolygonWidget>(GetTemplateWidget()))
	{
		if (Polygon->Points.IsValidIndex(Index))
		{
			OutDesignPos = Polygon->Points[Index];
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
	if (USCADAPolygonWidget* Polygon = Cast<USCADAPolygonWidget>(Widget))
	{
		if (Polygon->Points.IsValidIndex(Index))
		{
			TArray<FVector2D> NewPoints = Polygon->Points;
			NewPoints[Index] = NewDesignPos;
			Polygon->SetPoints(NewPoints);
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

FVector2D FSCADAVertexDragExtension::GetCanvasScale() const
{
	if (const UWidget* Preview = GetPreviewWidget())
	{
		if (const USCADACanvasWidget* Canvas = Cast<USCADACanvasWidget>(Preview->GetParent()))
		{
			const FVector2D Scale = Canvas->GetEffectiveCanvasScale();
			return FVector2D(FMath::Max(Scale.X, 0.01), FMath::Max(Scale.Y, 0.01));
		}
	}
	return FVector2D(1.0, 1.0);
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

	// 画布设计坐标 P → 画布本地坐标 P*Scale（SSCADACanvas 以 MakeChild(FScale2f(Scale)) 排布子内容，
	// 自动缩放模式下逐轴），再换算到设计器表面坐标（与 SDesignerView::GetExtensionPosition 的
	// RelativeFromParent 同一空间：FinalPosition = DesignerLocal(画布原点) + Offset）。减去半个手柄尺寸使手柄居中于顶点。
	const FVector2D Scale = GetCanvasScale();
	const FGeometry DesignerGeometry = Designer->GetDesignerGeometry();
	const FVector2D VertexAbsolute = ParentGeometry.LocalToAbsolute(DesignPos * Scale);
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
	return ParentGeometry.AbsoluteToLocal(ScreenPos) / GetCanvasScale();
}

FVector2D FSCADAVertexDragExtension::GetHandleOffset(int32 Index) const
{
	// spare 手柄（超出当前顶点数）早退，跳过顶点查找与几何换算
	if (Index >= GetVertexCount())
	{
		return FVector2D::ZeroVector;
	}

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
	FVector2D Vertex = FVector2D::ZeroVector;
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
		FinalizeDrag();

		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

void FSCADAVertexDragExtension::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// 收尾兜底：拖动进行中左键实际已松开、但 Up 事件未送达本手柄（切换选择/删除控件使扩展元素
	// 重建、手柄 SBorder 销毁，或设计器失焦/ESC 使 Slate 强制释放捕获）时主动收尾，避免遗留未
	// 关闭事务污染撤销栈与 bDragging 卡死。SDesignerView 每帧无条件 Tick 所有扩展（与选择状态、
	// 手柄是否存在无关），故手柄整个消失的场景也能覆盖。
	if (bDragging && !FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
	{
		FinalizeDrag();
	}
}

void FSCADAVertexDragExtension::FinalizeDrag()
{
	// 幂等守卫：Slate 在同一帧内先路由输入事件再 Tick 控件，正常路径（Up 送达 HandleEndDrag）
	// 已把 bDragging 复位，随后本帧 Tick 的兜底检测不会再进入。
	if (!bDragging)
	{
		return;
	}

	bDragging = false;
	DragIndex = INDEX_NONE;

	EndTransaction();

	if (Designer != nullptr)
	{
		Designer->MarkDesignModifed(false);
	}

	// 兜底路径不主动释放鼠标捕获：兜底触发即说明 Up 未送达，捕获已被 Slate 强制释放（捕获仍在
	// 时 Up 必被路由回本手柄走正常路径）；ReleaseAllPointerCapture 会误放其它控件的捕获，且
	// GetMouseCaptor 在 5.7 无公开查询入口。正常路径的释放仍由 HandleEndDrag 的 FReply 完成。
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
