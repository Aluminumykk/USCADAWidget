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
 *  - 边框：**内画 + 显式更高绘制层**——几何数据（Points/半径/矩形尺寸）= 外轮廓 =
 *    视觉边界 = 槽包围盒。**实线边框 = 环带填充**：外环 = 原始轮廓（尖角精确到几何顶点）、
 *    内环 = 内缩 T 的轮廓（BuildOffsetContour 斜接偏移），合成单环多边形耳切三角化
 *    一次填充——不用描边，描边的角部圆盘/斜接盖不住尖角会露出填充，环带填充与外轮廓
 *    完全重合、零缝隙；填充画在 LayerId、边框整体抬到 LayerId+1（Slate 同层内
 *    MakeCustomVerts 与 MakeLines/MakeBox 合批不保证提交顺序，显式分层是严格层序）。
 *    Thickness=0 即不画边框且图形不会"少一圈"。虚线边框维持描边方案
 *    （虚线本身到处断裂，角部缺陷不显著）：内缩 T/2 环 + BuildRoundedPolyline +
 *    BuildSegmentPoints(Trim=0) 逐段绘制，点段画旋转实心方块。
 * 闪烁只切填充色（FlashColor），边框保持本色（WinCC 语义：黑边白圆红闪 = 黑边红闪）。
 * 无箭头/端点样式。
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
	/** 画多边形填充：耳切三角化（支持凹多边形）后用 DrawTriangleList 一次画出；
	 *  三角化失败（退化/自交/共线）时静默返回。 */
	void DrawPolygonFill(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
		const TArray<FVector2D>& Vertices, const FLinearColor& InColor, ESlateDrawEffect DrawEffects) const;

	/** 按索引画实心三角形列表（一次 MakeCustomVerts，白纹理回退，纯色顶点 tint） */
	void DrawTriangleList(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
		const TArray<FVector2D>& Vertices, const TArray<int32>& Indices, const FLinearColor& InColor, ESlateDrawEffect DrawEffects) const;

	/** 闭合轮廓整体偏移 OffsetDistance（边框外画用，正=向内 / 负=向外）：逐顶点取相邻
	 *  两边偏移线的斜接交点（miter，限幅 4x 防尖角爆炸），凹顶点由同一公式自然处理。
	 *  输出闭合点列（末尾回接首点）。|OffsetDistance|≈0 时输出原轮廓；
	 *  退化（零面积/共线）返回 false；向内偏移后轮廓坍塌（面积翻号，图形小于偏移距离）
	 *  返回 false（向外偏移不会坍塌）。 */
	bool BuildOffsetContour(const TArray<FVector2D>& Poly, float OffsetDistance, TArray<FVector2D>& OutClosedRing) const;

private:
	/** 填充颜色（背景色，默认 WinCC 式浅灰） */
	TAttribute<FLinearColor> FillColor;
	/** 填充图案：实心 / 透明 */
	TAttribute<ESCADAFillPattern> FillPattern;
};
