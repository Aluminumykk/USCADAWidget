// Copyright Pr_UEDraw. All Rights Reserved.

#include "SCADAPolylineVertexExtension.h"

#include "Widgets/SCADACanvasWidget.h"
#include "Widgets/SCADAPolylineWidget.h"

#include "IUMGDesigner.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "USCADAWidgetEditor"

FSCADAPolylineVertexExtension::FSCADAPolylineVertexExtension()
{
	ExtensionId = FName(TEXT("SCADAPolylineVertex"));
}

bool FSCADAPolylineVertexExtension::CanExtendSelection(const TArray<FWidgetReference>& Selection) const
{
	return Selection.Num() == 1 &&
		Selection[0].GetTemplate() != nullptr &&
		Selection[0].GetTemplate()->IsA<USCADAPolylineWidget>();
}

void FSCADAPolylineVertexExtension::ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements)
{
	SelectionCache = Selection;

	HandleWidgets.Reset();
	for (int32 Index = 0; Index < HandlePoolSize; ++Index)
	{
		TSharedPtr<SWidget> Handle =
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
			.Visibility(this, &FSCADAPolylineVertexExtension::GetHandleVisibility, Index)
			.OnMouseButtonDown(this, &FSCADAPolylineVertexExtension::HandleBeginDrag, Index)
			.OnMouseMove(this, &FSCADAPolylineVertexExtension::HandleDragging, Index)
			.OnMouseButtonUp(this, &FSCADAPolylineVertexExtension::HandleEndDrag, Index)
			.Padding(FMargin(0))
			[
				SNew(SBox)
				.WidthOverride(HandleSize)
				.HeightOverride(HandleSize)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(this, &FSCADAPolylineVertexExtension::GetHandleColor, Index)
				]
			];

		Handle->SlatePrepass();
		HandleWidgets.Add(Handle);

		TAttribute<FVector2D> Offset = TAttribute<FVector2D>::Create(
			TAttribute<FVector2D>::FGetter::CreateSP(SharedThis(this), &FSCADAPolylineVertexExtension::GetHandleOffset, Index));
		SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(
			Handle.ToSharedRef(), EExtensionLayoutLocation::RelativeFromParent, Offset, FVector2D(0, 0)));
	}
}

USCADAPolylineWidget* FSCADAPolylineVertexExtension::GetTemplatePolyline() const
{
	return SelectionCache.Num() == 1 ? Cast<USCADAPolylineWidget>(SelectionCache[0].GetTemplate()) : nullptr;
}

USCADAPolylineWidget* FSCADAPolylineVertexExtension::GetPreviewPolyline() const
{
	return SelectionCache.Num() == 1 ? Cast<USCADAPolylineWidget>(SelectionCache[0].GetPreview()) : nullptr;
}

float FSCADAPolylineVertexExtension::GetCanvasZoom() const
{
	if (const USCADAPolylineWidget* Preview = GetPreviewPolyline())
	{
		if (const USCADACanvasWidget* Canvas = Cast<USCADACanvasWidget>(Preview->GetParent()))
		{
			return FMath::Max(Canvas->Zoom, 0.01f);
		}
	}
	return 1.0f;
}

bool FSCADAPolylineVertexExtension::GetParentGeometry(FGeometry& OutGeometry) const
{
	return Designer != nullptr && SelectionCache.Num() == 1 &&
		Designer->GetWidgetParentGeometry(SelectionCache[0], OutGeometry);
}

FVector2D FSCADAPolylineVertexExtension::DesignToHandleOffset(const FVector2D& DesignPos) const
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

FVector2D FSCADAPolylineVertexExtension::ScreenToDesign(const FVector2D& ScreenPos) const
{
	FGeometry ParentGeometry;
	if (!GetParentGeometry(ParentGeometry))
	{
		return FVector2D::ZeroVector;
	}
	return ParentGeometry.AbsoluteToLocal(ScreenPos) / GetCanvasZoom();
}

FVector2D FSCADAPolylineVertexExtension::GetHandleOffset(int32 Index) const
{
	const USCADAPolylineWidget* Template = GetTemplatePolyline();
	if (Template == nullptr || !Template->Points.IsValidIndex(Index))
	{
		return FVector2D::ZeroVector;
	}
	return DesignToHandleOffset(Template->Points[Index]);
}

EVisibility FSCADAPolylineVertexExtension::GetHandleVisibility(int32 Index) const
{
	const USCADAPolylineWidget* Template = GetTemplatePolyline();
	return (Template != nullptr && Template->Points.IsValidIndex(Index)) ? EVisibility::Visible : EVisibility::Collapsed;
}

FSlateColor FSCADAPolylineVertexExtension::GetHandleColor(int32 Index) const
{
	const bool bHovered = HandleWidgets.IsValidIndex(Index) && HandleWidgets[Index].IsValid() && HandleWidgets[Index]->IsHovered();
	const bool bDragged = bDragging && DragIndex == Index;
	// 拖动/悬停高亮为亮黄，常态橙色
	return (bHovered || bDragged) ? FLinearColor(1.0f, 0.85f, 0.2f) : FLinearColor(1.0f, 0.5f, 0.0f);
}

FReply FSCADAPolylineVertexExtension::HandleBeginDrag(const FGeometry& Geometry, const FPointerEvent& Event, int32 Index)
{
	USCADAPolylineWidget* Template = GetTemplatePolyline();
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton &&
		Template != nullptr && Template->Points.IsValidIndex(Index) &&
		HandleWidgets.IsValidIndex(Index) && HandleWidgets[Index].IsValid())
	{
		BeginTransaction(LOCTEXT("MovePolylinePoint", "Move Polyline Point"));
		Template->Modify();

		bDragging = true;
		DragIndex = Index;
		DragStartVertex = Template->Points[Index];
		DragStartDesignPos = ScreenToDesign(Event.GetScreenSpacePosition());

		return FReply::Handled().CaptureMouse(HandleWidgets[Index].ToSharedRef());
	}

	return FReply::Unhandled();
}

FReply FSCADAPolylineVertexExtension::HandleDragging(const FGeometry& Geometry, const FPointerEvent& Event, int32 Index)
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

FReply FSCADAPolylineVertexExtension::HandleEndDrag(const FGeometry& Geometry, const FPointerEvent& Event, int32 Index)
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

void FSCADAPolylineVertexExtension::ApplyVertexPosition(int32 Index, const FVector2D& NewDesignPos)
{
	USCADAPolylineWidget* Template = GetTemplatePolyline();
	if (Template == nullptr || !Template->Points.IsValidIndex(Index))
	{
		return;
	}

	TArray<FVector2D> NewPoints = Template->Points;
	NewPoints[Index] = NewDesignPos;

	// 用公开 SetPoints（赋值 + 像素取整 + 槽包围盒同步 + Slate 推送）同时写
	// Template + Preview 双实例：两边槽缓存保持一致，CheckSlotSync 不会把写回
	// 误判成设计器输入。不走属性系统（SetPropertyValue 要求 PostEditChangeProperty
	// 公开且 PreEditChange 链式重载可见，控件类不满足；直接写 + 自身同步路径即可，
	// 撤销由拖动开始时的 Modify() + 事务覆盖）。
	Template->SetPoints(NewPoints);
	if (USCADAPolylineWidget* Preview = GetPreviewPolyline())
	{
		Preview->SetPoints(NewPoints);
	}
}

#undef LOCTEXT_NAMESPACE
