// Copyright Pr_UEDraw. All Rights Reserved.

#include "Slate/SSCADACanvas.h"

#include "Rendering/DrawElements.h"

void SSCADACanvas::Construct(const FArguments& InArgs)
{
	ShowGrid = InArgs._ShowGrid;
	GridSize = InArgs._GridSize;
	GridColor = InArgs._GridColor;
	Zoom = InArgs._Zoom;
}

void SSCADACanvas::SetShowGrid(const TAttribute<bool>& InShowGrid) { ShowGrid = InShowGrid; }
void SSCADACanvas::SetGridSize(const TAttribute<float>& InGridSize) { GridSize = InGridSize; }
void SSCADACanvas::SetGridColor(const TAttribute<FLinearColor>& InColor) { GridColor = InColor; }
void SSCADACanvas::SetZoom(const TAttribute<float>& InZoom) { Zoom = InZoom; }

int32 SSCADACanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (ShowGrid.Get())
	{
		const float Cell = FMath::Max(GridSize.Get() * Zoom.Get(), 2.0f);
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const FLinearColor LineColor = GridColor.Get() * InWidgetStyle.GetColorAndOpacityTint();
		const FPaintGeometry PaintGeom = AllottedGeometry.ToPaintGeometry();

		// 注意：MakeLines 会把传入的点连成一条折线，
		// 不相连的网格线必须每根独立调用一次，且坐标对齐到整数像素。
		auto DrawGridLine = [&](const FVector2D& A, const FVector2D& B)
		{
			TArray<FVector2D> Pair = {
				FVector2D(FMath::RoundToFloat(A.X), FMath::RoundToFloat(A.Y)),
				FVector2D(FMath::RoundToFloat(B.X), FMath::RoundToFloat(B.Y))
			};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeom, Pair, ESlateDrawEffect::None, LineColor, /*bAntialias=*/false, 1.0f);
		};

		for (float X = 0.f; X <= Size.X; X += Cell)
		{
			DrawGridLine(FVector2D(X, 0.f), FVector2D(X, Size.Y));
		}
		for (float Y = 0.f; Y <= Size.Y; Y += Cell)
		{
			DrawGridLine(FVector2D(0.f, Y), FVector2D(Size.X, Y));
		}
		++LayerId;
	}

	return SConstraintCanvas::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void SSCADACanvas::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const float Z = FMath::Max(Zoom.Get(), 0.01f);
	if (FMath::IsNearlyEqual(Z, 1.0f))
	{
		SConstraintCanvas::OnArrangeChildren(AllottedGeometry, ArrangedChildren);
		return;
	}

	// 子内容按 Zoom 整体缩放渲染（以左上角为缩放原点）：
	// 子图元内部仍使用画布本地坐标（设计坐标），无需关心平台缩放。
	const FGeometry ScaledGeometry = AllottedGeometry.MakeChild(
		FSlateRenderTransform(FScale2f(Z)),
		FVector2f::ZeroVector);

	SConstraintCanvas::OnArrangeChildren(ScaledGeometry, ArrangedChildren);
}
