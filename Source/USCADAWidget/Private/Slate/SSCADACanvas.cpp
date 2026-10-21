// Copyright Pr_UEDraw. All Rights Reserved.

#include "Slate/SSCADACanvas.h"

#include "Rendering/DrawElements.h"

void SSCADACanvas::Construct(const FArguments& InArgs)
{
	ShowGrid = InArgs._ShowGrid;
	GridSize = InArgs._GridSize;
	GridColor = InArgs._GridColor;
	Zoom = InArgs._Zoom;
	DesignSize = InArgs._DesignSize;
	ScaleMode = InArgs._ScaleMode;
}

void SSCADACanvas::SetShowGrid(const TAttribute<bool>& InShowGrid) { ShowGrid = InShowGrid; }
void SSCADACanvas::SetGridSize(const TAttribute<float>& InGridSize) { GridSize = InGridSize; }
void SSCADACanvas::SetGridColor(const TAttribute<FLinearColor>& InColor) { GridColor = InColor; }
void SSCADACanvas::SetZoom(const TAttribute<float>& InZoom) { Zoom = InZoom; }
void SSCADACanvas::SetDesignSize(const TAttribute<FVector2D>& InDesignSize) { DesignSize = InDesignSize; }
void SSCADACanvas::SetScaleMode(const TAttribute<ESCADACanvasScaleMode>& InScaleMode) { ScaleMode = InScaleMode; }

FVector2D SSCADACanvas::ComputeEffectiveScale(const FVector2D& AreaSize) const
{
	const float Z = FMath::Max(Zoom.Get(), 0.01f);
	const ESCADACanvasScaleMode Mode = ScaleMode.Get();
	if (Mode == ESCADACanvasScaleMode::None)
	{
		return FVector2D(Z, Z);
	}

	const FVector2D Design(
		FMath::Max(DesignSize.Get().X, 1.0),
		FMath::Max(DesignSize.Get().Y, 1.0));
	const double ScaleX = AreaSize.X / Design.X;
	const double ScaleY = AreaSize.Y / Design.Y;

	switch (Mode)
	{
	case ESCADACanvasScaleMode::Fit:
	{
		const double S = FMath::Max(FMath::Min(ScaleX, ScaleY), 0.01);
		return FVector2D(S, S);
	}
	case ESCADACanvasScaleMode::Fill:
	{
		const double S = FMath::Max(FMath::Max(ScaleX, ScaleY), 0.01);
		return FVector2D(S, S);
	}
	case ESCADACanvasScaleMode::Stretch:
	default:
		return FVector2D(FMath::Max(ScaleX, 0.01), FMath::Max(ScaleY, 0.01));
	}
}

FGeometry SSCADACanvas::MakeScaledChildGeometry(const FGeometry& AllottedGeometry) const
{
	const FVector2D EffectiveScale = ComputeEffectiveScale(AllottedGeometry.GetLocalSize());
	if (FMath::IsNearlyEqual(EffectiveScale.X, 1.0) && FMath::IsNearlyEqual(EffectiveScale.Y, 1.0))
	{
		return AllottedGeometry;
	}

	// 子内容按生效缩放整体缩放渲染（以左上角为缩放原点，Stretch 模式下逐轴）：
	// 子图元内部仍使用画布本地坐标（设计坐标），无需关心平台缩放。
	return AllottedGeometry.MakeChild(
		FSlateRenderTransform(FScale2f((float)EffectiveScale.X, (float)EffectiveScale.Y)),
		FVector2f::ZeroVector);
}

int32 SSCADACanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (ShowGrid.Get())
	{
		// 网格单元 = 设计间距 × 生效缩放（与 OnArrangeChildren 同一系数，自动适配模式下逐轴）。
		// 间距小于 6px（缩得小）时整段跳过：2px 网格既糊又会爆出上千根线
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const FVector2D EffectiveScale = ComputeEffectiveScale(Size);
		const double CellX = GridSize.Get() * EffectiveScale.X;
		const double CellY = GridSize.Get() * EffectiveScale.Y;
		if (CellX >= 6.0 && CellY >= 6.0)
		{
			const FLinearColor LineColor = GridColor.Get() * InWidgetStyle.GetColorAndOpacityTint();
			const FPaintGeometry PaintGeom = AllottedGeometry.ToPaintGeometry();

			// 注意：MakeLines 会把传入的点连成一条折线，
			// 不相连的网格线必须每根独立调用一次，且坐标对齐到整数像素。
			TArray<FVector2D> Pair;
			Pair.SetNum(2);
			auto DrawGridLine = [&](const FVector2D& A, const FVector2D& B)
			{
				Pair[0] = FVector2D(FMath::RoundToFloat(A.X), FMath::RoundToFloat(A.Y));
				Pair[1] = FVector2D(FMath::RoundToFloat(B.X), FMath::RoundToFloat(B.Y));
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeom, Pair, ESlateDrawEffect::None, LineColor, /*bAntialias=*/false, 1.0f);
			};

			for (double X = 0.0; X <= Size.X; X += CellX)
			{
				DrawGridLine(FVector2D(X, 0.f), FVector2D(X, Size.Y));
			}
			for (double Y = 0.0; Y <= Size.Y; Y += CellY)
			{
				DrawGridLine(FVector2D(0.f, Y), FVector2D(Size.X, Y));
			}
			++LayerId;
		}
	}

	// 关键：基类 SConstraintCanvas::OnPaint 会用**传入的几何**自己重新排布子控件绘制
	// （不经 OnArrangeChildren），必须传入缩放后的子级几何，否则绘制不跟随缩放。
	return SConstraintCanvas::OnPaint(Args, MakeScaledChildGeometry(AllottedGeometry), MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void SSCADACanvas::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FGeometry ScaledGeometry = MakeScaledChildGeometry(AllottedGeometry);
	LastEffectiveScale = ComputeEffectiveScale(AllottedGeometry.GetLocalSize());
	SConstraintCanvas::OnArrangeChildren(ScaledGeometry, ArrangedChildren);
}
