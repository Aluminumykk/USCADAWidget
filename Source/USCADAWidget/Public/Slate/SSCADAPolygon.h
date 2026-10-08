// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SSCADALine.h"
#include "SCADATypes.h"

/**
 * 底层 Slate 多边形控件：闭合折线（首尾相连）+ 填充（对应 WinCC 的“多边形”对象）。
 * 继承 SSCADALine，复用其像素对齐、虚线/点线模式采样、圆连接（Round Join）与
 * 全局闪烁相位逻辑；绘制顺序为“先填充、后边框”：
 *  - 填充（FillPattern=Solid）：耳切三角化（支持凹多边形）+ 一次 MakeCustomVerts 实心三角扇；
 *    三角化失败时静默跳过（只画边框）。
 *  - 边框：闭合路径；实线 = 一次 MakeLines + 每个顶点盖连接盘；虚线系 = 圆角采样 +
 *    BuildSegmentPoints(Trim=0) 逐段绘制，点段画旋转实心方块。
 * 闪烁时边框与填充都切到 FlashColor。无箭头/端点样式。
 * 点坐标为控件本地空间（Local space），绘制前做像素对齐（与线同一规则）。
 */
class USCADAWIDGET_API SSCADAPolygon : public SSCADALine
{
public:
	SLATE_BEGIN_ARGS(SSCADAPolygon)
		: _FillColor(FLinearColor(241.0f / 255.0f, 241.0f / 255.0f, 242.0f / 255.0f))
		, _FillPattern(ESCADAFillPattern::Solid)
	{
	}
		SLATE_ATTRIBUTE(FLinearColor, FillColor)
		SLATE_ATTRIBUTE(ESCADAFillPattern, FillPattern)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 填充颜色（背景色） */
	void SetFillColor(const TAttribute<FLinearColor>& InColor);
	/** 填充图案：实心 / 透明 */
	void SetFillPattern(const TAttribute<ESCADAFillPattern>& InPattern);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** 画多边形填充：耳切三角化（支持凹多边形）+ 一次 MakeCustomVerts 实心三角扇，
	 *  白色资源句柄回退（与 SSCADALine::AppendArrow 同一条路径），纯色由顶点 tint 决定。
	 *  三角化失败（退化/自交/共线）时静默返回。 */
	void DrawPolygonFill(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
		const TArray<FVector2D>& Vertices, const FLinearColor& InColor, ESlateDrawEffect DrawEffects) const;

private:
	/** 填充颜色（背景色，默认 WinCC 式浅灰） */
	TAttribute<FLinearColor> FillColor;
	/** 填充图案：实心 / 透明 */
	TAttribute<ESCADAFillPattern> FillPattern;
};
