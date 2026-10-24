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

FVector2D SSCADACanvas::ComputeDesiredSize(float /*LayoutScaleMultiplier*/) const
{
	// 基类 SConstraintCanvas::ComputeDesiredSize 返回"非折叠子控件的槽偏移+尺寸并集"——
	// 两处与实际显示对不上：① RenderTransform 是渲染层效果不参与布局，旋转过的内容
	// 槽并集 ≠ 视觉范围，「画布适配内容」（收缩到视觉包围盒）后 Desired 与 CanvasSize 矛盾；
	// ② 内容小于/为空时 Desired 塌缩，设计器「所需」预览下画布框与网格范围互相错位。
	// 本实现纯计算无副作用（布局热路径）：Desired 恒由 CanvasSize（设计基准，≥1 防退化）驱动。
	// - ScaleMode=None：再乘 Zoom——Zoom 是对整画内容的渲染缩放，实际占用就是 CanvasSize×Zoom。
	// - ScaleMode≠None：返回 CanvasSize——内容随实际分配尺寸自动适配，Desired 只提供设计
	//   基准；父级给固定尺寸分配时 Desired 根本不参与（实际尺寸由槽/父布局决定）。
	// 影响面：仅 UMG「所需（Desired）」预览、AutoSize 槽、以及未被父级强制分配尺寸的
	// 运行时布局；网格/内容绘制本就按分配几何走，Desired 与其无关。
	const FVector2D Design(
		FMath::Max(DesignSize.Get().X, 1.0),
		FMath::Max(DesignSize.Get().Y, 1.0));
	if (ScaleMode.Get() != ESCADACanvasScaleMode::None)
	{
		return Design;
	}
	const float Z = FMath::Max(Zoom.Get(), 0.01f);
	return Design * Z;
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
