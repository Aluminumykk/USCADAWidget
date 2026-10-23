// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADACanvasWidget.h"

#include "Slate/SSCADACanvas.h"
#include "Widgets/SCADACanvasSlot.h"
#include "Widgets/SCADAPrimitiveWidgetBase.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Slate/WidgetTransform.h"
#include "Rendering/SlateRenderTransform.h"
#include "Math/TransformCalculus2D.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

namespace
{
	/** 恒等判定：RenderTransform 未做任何变换（Translation 零 / Scale=1 / Shear 零 / Angle=0）。
	 *  未变换的子控件直接采用基准包围盒走快路径，避免浮点变换误差污染现有整数像素对齐结果 */
	bool IsIdentityRenderTransform(const FWidgetTransform& RenderTransform)
	{
		return RenderTransform.Translation.IsNearlyZero()
			&& RenderTransform.Scale.Equals(FVector2D::One())
			&& RenderTransform.Shear.IsNearlyZero()
			&& FMath::IsNearlyZero(RenderTransform.Angle);
	}

	/** 把基准矩形四角按 RenderTransform 绕 Pivot 点变换后取 AABB（视觉包围盒）。
	 *  变换数学逐字复刻引擎实际渲染路径，不是凭记忆自创：
	 *  - FWidgetTransform→FSlateRenderTransform：`FWidgetTransform::ToSlateRenderTransform()`
	 *    （Engine/Source/Runtime/UMG/Public/Slate/WidgetTransform.h）=
	 *    `Concatenate(Scale, Shear, FQuat2D(radians(Angle)), Translation)`。引擎 Concatenate 的
	 *    参数顺序语义是**左参数先作用于点**（见 Core/Public/Math/TransformCalculus2D.h 的
	 *    `Concatenate(Transform,Translation)` 等特化：左侧先作用、Translation 最后作平移加成），
	 *    即先缩放 → 错切 → 旋转，平移最后（平移发生在父坐标系，不被 Angle 旋转）。
	 *  - Pivot 应用方式：子几何的 `AccumulatedRenderTransform = Concatenate(Inverse(T(pc)), RT,
	 *    T(pc), Layout, Parent)`（Engine/Source/Runtime/SlateCore/Public/Layout/Geometry.h
	 *    TGeometry 构造函数），pc = Pivot × 子控件本地尺寸——先把 pc 移到原点 → 应用 RT →
	 *    移回 pc，即以 pc 为不动点绕 Pivot 变换（Pivot 是相对布局分配子控件尺寸的归一化值）。
	 *  本函数只做 Pivot 三段合成（子本地坐标系，原点 = 基准矩形左上角），
	 *  基准矩形原点 ↔ 画布设计坐标的平移由调用方加减完成。 */
	void TransformRectByRenderTransform(const FWidgetTransform& RenderTransform, const FVector2D& Pivot,
		const FVector2D& LocalSize, const FVector2D& RectMin, const FVector2D& RectMax,
		FVector2D& OutMin, FVector2D& OutMax)
	{
		// Pivot 归一化 → 像素偏移（对应引擎 TransformPoint(FScale2D(size), pivot)）
		const FVector2D PivotLocal(Pivot.X * LocalSize.X, Pivot.Y * LocalSize.Y);
		const FSlateRenderTransform PivotShift(PivotLocal);
		const FSlateRenderTransform Local = Concatenate(
			Inverse(PivotShift), RenderTransform.ToSlateRenderTransform(), PivotShift);

		// 四角变换取 AABB（5.7 的 TVector2 无 Max() 哨兵静态函数，用首角初始化）
		OutMin = OutMax = RectMin + Local.TransformPoint(FVector2D::ZeroVector);
		const FVector2D Size = RectMax - RectMin;
		const FVector2D CornerOffsets[3] = {
			FVector2D(Size.X, 0.0), Size, FVector2D(0.0, Size.Y),
		};
		for (const FVector2D& Offset : CornerOffsets)
		{
			const FVector2D Corner = RectMin + Local.TransformPoint(Offset);
			OutMin.X = FMath::Min(OutMin.X, Corner.X);
			OutMin.Y = FMath::Min(OutMin.Y, Corner.Y);
			OutMax.X = FMath::Max(OutMax.X, Corner.X);
			OutMax.Y = FMath::Max(OutMax.Y, Corner.Y);
		}
	}
}

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

bool USCADACanvasWidget::ComputeVisualContentBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	// 无子控件 → false（"适配内容/内容适配画布"两个操作都直接什么都不做）
	if (Slots.Num() == 0)
	{
		return false;
	}

	// 汇总内容并集包围盒：图元取几何数据包围盒（数据层是唯一真值，槽矩形恒等于它）；
	// 普通控件（Text、嵌套画布等非图元基类子类）取其槽位置 + 尺寸。
	// 初始值取首个有效子内容（5.7 的 TVector2 无 Max()/Lowest() 哨兵静态函数，不能用极值初始化）
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

		// 计入子控件的 RenderTransform（UE 原生 Translation/Angle/Scale/Shear + RenderTransformPivot，
		// SVG 导入 rotate 分组画布同机制）：拟合的是**视觉**包围盒。恒等走快路径（浮点误差
		// 不污染未变换控件的整数结果）；否则四角绕 Pivot 变换后取 AABB（见上方函数注释）。
		// LocalSize 用基准矩形尺寸：图元槽矩形 == 数据包围盒（正向同步不变式），非图元
		// 基准矩形即槽位置+尺寸，两种情形下都正是布局分配给子控件的尺寸。
		const FWidgetTransform& ChildRenderTransform = Child->GetRenderTransform();
		if (!IsIdentityRenderTransform(ChildRenderTransform))
		{
			FVector2D VisMin, VisMax;
			TransformRectByRenderTransform(ChildRenderTransform, Child->GetRenderTransformPivot(),
				Max - Min, Min, Max, VisMin, VisMax);
			Min = VisMin;
			Max = VisMax;
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

	OutMin = BoundsMin;
	OutMax = BoundsMax;
	// 全部子槽都取不到（理论兜底）同样返回 false：两个适配操作都不动画布/内容
	return bHasContent;
}

void USCADACanvasWidget::FitCanvasToContent()
{
	FVector2D BoundsMin, BoundsMax;
	if (!ComputeVisualContentBounds(BoundsMin, BoundsMax))
	{
		return;
	}

	// 1) 内容尺寸 = 包围盒尺寸，逐轴 Clamp ≥1（点/单点线等退化图元包围盒可能为零）
	FVector2D ContentSize = BoundsMax - BoundsMin;
	ContentSize.X = FMath::Max(ContentSize.X, 1.0);
	ContentSize.Y = FMath::Max(ContentSize.Y, 1.0);

	// 2) 整体平移使内容包围盒左上角落到 (0,0)
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
				// 普通控件没有槽同步状态机，直接写槽安全；RenderTransform.Translation 是
				// 相对控件自身的内容，平移不改变它（视觉自然跟随）
				CanvasSlot->SetPosition(CanvasSlot->GetPosition() + Delta);
			}
		}
	}

	// 3) 画布收缩到内容尺寸（四周接壤、无留白）
	SetCanvasSize(ContentSize);
}

void USCADACanvasWidget::FitContentToCanvas()
{
	FVector2D BoundsMin, BoundsMax;
	if (!ComputeVisualContentBounds(BoundsMin, BoundsMax))
	{
		return;
	}

	// 等比完整显示（类比 ScaleMode=Fit）：k = min(画布宽/内容宽, 画布高/内容高)，
	// 内容缩放 k 倍后居中留边。CanvasSize 保持不变。
	// 退化轴处理：内容宽或高 <1（纯水平/垂直线、点集）不参与 k 计算，k 取另一轴；
	// 该轴内容（退化为一条线/一个点）随公式被居中到画布中线。两轴都退化 = 没有
	// 可缩放的面积，不缩放不平移直接返回（避免除零/缩放爆炸）。
	const FVector2D ContentSize = BoundsMax - BoundsMin;
	const bool bValidX = ContentSize.X >= 1.0;
	const bool bValidY = ContentSize.Y >= 1.0;
	if (!bValidX && !bValidY)
	{
		return;
	}

	double Scale;
	if (bValidX && bValidY)
	{
		Scale = FMath::Min(CanvasSize.X / ContentSize.X, CanvasSize.Y / ContentSize.Y);
	}
	else if (bValidX)
	{
		Scale = CanvasSize.X / ContentSize.X;
	}
	else
	{
		Scale = CanvasSize.Y / ContentSize.Y;
	}

	// 居中：留边 = (CanvasSize - k*内容尺寸)/2；PostOffset 一次到位（先缩放后平移的合成），
	// 避免"先缩放到原点再平移"的两步取整误差。退化轴 ContentSize=0 时该式恰好把
	// 退化内容映射到画布该轴中心，语义正确无需特判。
	const FVector2D PostOffset = (CanvasSize - ContentSize * Scale) * 0.5 - BoundsMin * Scale;

	for (UPanelSlot* PanelSlot : Slots)
	{
		UWidget* Child = (PanelSlot != nullptr && PanelSlot->Content != nullptr) ? PanelSlot->Content : nullptr;
		if (Child == nullptr)
		{
			continue;
		}

		if (USCADAPrimitiveWidgetBase* Primitive = Cast<USCADAPrimitiveWidgetBase>(Child))
		{
			// 图元走正向通道：几何数据（含线宽/圆角等厚度类）p→p*k+PostOffset → 取整 → 对齐槽。
			// bSnapToPixel=false 的 SVG 采样点保持亚像素（其 Snap 只对齐线宽，语义不破坏）
			Primitive->ScaleGeometryAndSync((float)Scale, PostOffset);
		}
		else if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Child->Slot))
		{
			// 普通控件槽矩形随内容整体缩放平移（无槽同步状态机，直接写槽安全）
			CanvasSlot->SetPosition(CanvasSlot->GetPosition() * Scale + PostOffset);
			CanvasSlot->SetSize(CanvasSlot->GetSize() * Scale);
		}

		// 带 RenderTransform 的子控件（含 SVG rotate 分组画布）：Translation 是父坐标系量，
		// 内容整体缩放后必须同步乘 k，否则视觉与包围盒不一致；Angle/Scale/Shear 是
		// 控件自身相对属性不动，Pivot 相对控件尺寸（槽尺寸已缩放）也不动
		FWidgetTransform ChildRenderTransform = Child->GetRenderTransform();
		if (!ChildRenderTransform.Translation.IsNearlyZero())
		{
			ChildRenderTransform.Translation = ChildRenderTransform.Translation * Scale;
			Child->SetRenderTransform(ChildRenderTransform);
		}

		// "内容按控件内在属性绘制、不随槽尺寸自动缩放"的两类补偿：
		if (USCADACanvasWidget* Nested = Cast<USCADACanvasWidget>(Child))
		{
			// 嵌套画布 ScaleMode=None：内容按其自身 Zoom 渲染，槽尺寸变化不影响内容缩放——
			// 把 k 乘进 Zoom。ScaleMode≠None 时生效缩放由槽尺寸自动计算，内容随槽 Resize 自然跟随
			if (Nested->ScaleMode == ESCADACanvasScaleMode::None)
			{
				Nested->SetZoom(FMath::Max(Nested->Zoom * (float)Scale, 0.01f));
			}
		}
		else if (UTextBlock* TextBlock = Cast<UTextBlock>(Child))
		{
			// 文字字号是内在属性、不随槽尺寸缩放——同步乘 k（四舍五入取整、下限 1），
			// 走 GetFont/SetFont 读写并立即推送 Slate；文本内其它字号字段（OutlineSize 等）保持不动
			FSlateFontInfo Font = TextBlock->GetFont();
			Font.Size = FMath::Max(FMath::RoundToInt(Font.Size * (float)Scale), 1);
			TextBlock->SetFont(Font);
		}
	}

	// CanvasSize 保持不变——本操作只缩放/平移内容
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
