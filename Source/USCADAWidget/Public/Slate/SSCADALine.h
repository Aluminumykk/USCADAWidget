// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "SCADATypes.h"

/**
 * 底层 Slate 画线控件：绘制直线/折线（支持虚线、点线、端点箭头）。
 * 点坐标为控件本地空间（Local space），绘制前会做像素对齐（四舍五入到就近整数坐标）。
 */
class USCADAWIDGET_API SSCADALine : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSCADALine)
		: _Points()
		, _Color(FLinearColor::White)
		, _Thickness(2.0f)
		, _LineStyle(ESCADALineStyle::Solid)
		, _LineStartStyle(ESCADALineEndStyle::None)
		, _LineEndStyle(ESCADALineEndStyle::None)
		, _LineCap(ESCADALineCap::None)
		, _bFlashEnabled(false)
		, _FlashColor(FLinearColor::Red)
		, _DashLength(12.0f)
		, _GapLength(6.0f)
		, _ArrowSize(10.0f)
	{
	}
		SLATE_ATTRIBUTE(TArray<FVector2D>, Points)
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_ATTRIBUTE(float, Thickness)
		SLATE_ATTRIBUTE(ESCADALineStyle, LineStyle)
		SLATE_ATTRIBUTE(ESCADALineEndStyle, LineStartStyle)
		SLATE_ATTRIBUTE(ESCADALineEndStyle, LineEndStyle)
		SLATE_ATTRIBUTE(ESCADALineCap, LineCap)
		SLATE_ATTRIBUTE(bool, bFlashEnabled)
		SLATE_ATTRIBUTE(FLinearColor, FlashColor)
		SLATE_ATTRIBUTE(float, DashLength)
		SLATE_ATTRIBUTE(float, GapLength)
		SLATE_ATTRIBUTE(float, ArrowSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetPoints(const TAttribute<TArray<FVector2D>>& InPoints);
	void SetColor(const TAttribute<FLinearColor>& InColor);
	void SetThickness(const TAttribute<float>& InThickness);
	void SetLineStyle(const TAttribute<ESCADALineStyle>& InStyle);
	void SetLineStartStyle(const TAttribute<ESCADALineEndStyle>& InStyle);
	void SetLineEndStyle(const TAttribute<ESCADALineEndStyle>& InStyle);
	void SetLineCap(const TAttribute<ESCADALineCap>& InCap);
	void SetFlashEnabled(const TAttribute<bool>& bInEnabled);
	void SetFlashColor(const TAttribute<FLinearColor>& InColor);

	/** 每帧回调（供 UMG 层轮询槽矩形变化等）。Slate 层不直接依赖 UMG。 */
	TFunction<void()> ExternalTickCallback;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** 全局统一闪烁相位（所有图元同步，1 秒换相）。true = 显示正常色，false = 显示闪烁色 */
	static bool IsFlashPhaseNormal();

private:
	/** 按线型把折线拆成虚线/点线的独立线段点对列表（仅用于非实线样式；调用方需每两个点画一条独立线段）。
	 *  OutIsDot 与段索引一一对应，标记该段是“点”（画成 1:1 方块）还是“划”。
	 *  TrimStart/TrimEnd：折线首/尾各裁掉的长度（箭头区域不再画点/划，避免与箭头重叠）。 */
	void BuildSegmentPoints(const TArray<FVector2D>& Polyline, TArray<FVector2D>& OutSegments, TArray<bool>& OutIsDot, float TrimStart = 0.0f, float TrimEnd = 0.0f) const;
	/** 画实心箭头：单个三角形（顶点=Tip，底边=Base±Wing），DirIn 为指向线体内部的方向，InSize 为箭头长度。
	 *  一次 MakeCustomVerts 实心填充，顶点在绘制空间做像素对齐（与线体同一规则）；Color 为最终色（含闪烁/tint）。 */
	void AppendArrow(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry, const FVector2D& Tip, const FVector2D& DirIn, float InSize, ESlateDrawEffect DrawEffects, const FLinearColor& InColor) const;
	/** 画圆形线端：以端点为圆心的实心圆盘（直径 = 线宽），一次 MakeBox + 全圆圆角矩形笔刷。 */
	void AppendRoundCap(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry, const FVector2D& Tip, ESlateDrawEffect DrawEffects, const FLinearColor& InColor) const;
	/** 圆连接（Round Join）：把折线中间顶点替换为半径 Radius 的圆弧（二次贝塞尔采样成折线，控制点=原顶点）。
	 *  用于消除粗线在拐点处的接缝缺口：调用方固定传 线宽/2，细线圆弧极小不可感知。
	 *  Radius <= 0 或点数 < 3 → 原样输出；退化（共线/近掉头）顶点保持尖角。输出仍是一条完整折线。 */
	void BuildRoundedPolyline(const TArray<FVector2D>& In, float Radius, TArray<FVector2D>& Out) const;

private:
	TAttribute<TArray<FVector2D>> Points;
	TAttribute<FLinearColor> Color;
	TAttribute<float> Thickness;
	TAttribute<ESCADALineStyle> LineStyle;
	TAttribute<ESCADALineEndStyle> LineStartStyle;
	TAttribute<ESCADALineEndStyle> LineEndStyle;
	TAttribute<ESCADALineCap> LineCap;
	TAttribute<bool> bFlashEnabled;
	TAttribute<FLinearColor> FlashColor;
	bool bLastFlashPhaseNormal = true;
	float DashLength = 12.0f;
	float GapLength = 6.0f;
	float ArrowSize = 10.0f;
};
