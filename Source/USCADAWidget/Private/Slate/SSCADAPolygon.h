// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slate/SSCADALine.h"
#include "SCADAWidgetTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"

/**
 * 底层 Slate 多边形控件：闭合折线（首尾相连）+ 填充（对应 WinCC 的“多边形”对象）。
 * 继承 SSCADALine，复用其像素对齐、虚线/点线模式采样、拐角圆弧替换与
 * 全局闪烁相位逻辑；绘制顺序为“先填充、后边框”：
 *  - **解析几何快速路径（优先于采样管线）**：子类重写 TryGetAnalyticBox 返回 true 时
 *    （精确圆 / 直角或等半径圆角矩形），实线样式下单次 MakeBox + FSlateRoundedBoxBrush
 *    完成填充+内画边框——引擎解析距离场 SDF 着色器（零折线感 + 解析抗锯齿，
 *    SlateShaderCommon.ush GetRoundedBoxDistance）；边框 Width/圆角都是局部单位，
 *    随渲染变换视觉等比，无需 RenderScale 补偿。条件不满足（虚线样式 / 内缩会坍塌 /
 *    两轴椭圆角 / 不等轴椭圆）自动回退下面的采样管线，语义不变。
 *  - 填充（FillPattern=Solid）：耳切三角化（支持凹多边形）+ 一次 MakeCustomVerts 实心三角扇；
 *    三角化失败时静默跳过（只画边框）。
 *  - 边框：**内画 + 显式更高绘制层**——几何数据（Points/半径/矩形尺寸）= 外轮廓 =
 *    视觉边界 = 槽包围盒。**实线边框 = 环带四边形条带**：外环 = 原始轮廓（尖角精确到
 *    几何顶点）、内环 = 内缩 T 的轮廓（BuildOffsetContour 斜接偏移），逐边以两端点构成
 *    两个三角形（外边缘 i→j + 内边缘 j→i，DrawTriangleList 一次画出），相邻四边形共享
 *    边、零缝隙、无失败模式。**不能**拼“外环正向 + 内环倒序”的单环多边形做耳切——单
 *    桥接的环带在闭合边一侧根本没被围入多边形（最后一段环带丢失）；描边方案在尖角处靠
 *    圆盘/斜接拐弯也盖不住角尖会露出填充。填充画在 LayerId、边框整体抬到 LayerId+1（Slate 同层内
 *    MakeCustomVerts 与 MakeLines/MakeBox 合批不保证提交顺序，显式分层是严格层序）。
 *    Thickness=0 即不画边框且图形不会"少一圈"。虚线边框维持描边方案
 *    （虚线本身到处断裂，角部缺陷不显著）：内缩 T/2 环 + BuildRoundedPolyline +
 *    BuildSegmentPoints(Trim=0) 逐段绘制，点段画旋转实心方块。
 * 闪烁只切填充色（FlashColor），边框保持本色（WinCC 语义：黑边白圆红闪 = 黑边红闪）。
 * 无箭头/端点样式。
 * 点坐标为控件本地空间（Local space），绘制前做像素对齐（与线同一规则）。
 */
class SSCADAPolygon : public SSCADALine
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

protected:
	/** RoundedBox SDF 解析几何参数（本地空间） */
	struct FAnalyticBox
	{
		FVector2D TopLeft = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		float CornerRadius = 0.0f;
	};

	/** 子类当前几何能精确表示为单个圆角矩形时返回 true（OnPaint 仅在 LineStyle=Solid 时探测，
	 *  并由调用方额外校验边框宽度不会使内轮廓坍塌 T < min(宽,高)/2）。返回 true 时本控件的
	 *  填充+实线边框改由一次 MakeBox（RoundedBox SDF）绘制：解析曲线 + 解析覆盖率抗锯齿，
	 *  绕开采样点列/耳切三角化/环带条带；其余样式与情况仍走采样管线。
	 *  实现者：SSCADAEllipse（RadiusX=RadiusY 的正圆）、SSCADARectangle（直角或 RX=RY 等半径圆角）。 */
	virtual bool TryGetAnalyticBox(FAnalyticBox& OutBox) const { return false; }

	/** 本控件当前绘制的轮廓是否为曲线（椭圆周 / 椭圆角弧）。true 时 OnPaint 跳过 Slate 层
	 *  整数取整（SnapToPixel 与 CustomVerts 顶点取整）——顶点取整会对密集采样的曲线在
	 *  每段间引入 ~0.3px 阶梯抖动，被 1px AA 覆层放大成肉眼可见的波浪；直线类图元保持
	 *  true（像素对齐约定对其有益）。SDF 快速路径不经过此开关。 */
	virtual bool PaintsSmoothContour() const { return false; }

	/** SDF 填充+边框 brush（OnPaint 每次改写圆角/边框参数；本体恒白色，填充色经 MakeBox Tint 传入） */
	mutable FSlateRoundedBoxBrush AnalyticFillBrush = FSlateRoundedBoxBrush(FLinearColor::White);

private:
	/** 本次绘制是否为平滑曲线轮廓（OnPaint 开头由 PaintsSmoothContour() 设置，仅同帧内有效，
	 *  DrawTriangleList 据此选择顶点取整模式） */
	mutable bool bSmoothContourActive = false;

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
