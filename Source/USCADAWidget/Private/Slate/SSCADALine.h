// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCADAWidgetTypes.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/RenderingCommon.h"

/**
 * 底层 Slate 画线控件：绘制直线/折线（支持虚线、点线、端点箭头）。
 * 点坐标为控件本地空间（Local space），绘制前会做像素对齐（四舍五入到就近整数坐标）。
 */
class SSCADALine : public SLeafWidget
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
		, _bFlashInverted(false)
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
		SLATE_ATTRIBUTE(bool, bFlashInverted)
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
	void SetFlashInverted(const TAttribute<bool>& bInInverted);
	/** 跳过中心线的 Slate 层像素取整（亚像素点列，SVG path 采样导入的小尺寸图形用；
	 *  默认 false 保持像素对齐约定）。变更参与几何缓存键。 */
	void SetSkipPixelSnap(bool bInSkip) { bSkipPixelSnap = bInSkip; }

	/** 每帧回调（供 UMG 层轮询槽矩形变化等）。Slate 层不直接依赖 UMG。 */
	TFunction<void()> ExternalTickCallback;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** 全局统一闪烁相位（所有图元同步，1 秒换相）。true = 正常组显示正常色，false = 正常组显示闪烁色。
	 *  翻转组图元（bFlashInverted=true）取色与此相反，由同一时刻换相。 */
	static bool IsFlashPhaseNormal();

protected:
	/** 按线型把折线拆成虚线/点线的独立线段点对列表（仅用于非实线样式；调用方需每两个点画一条独立线段）。
	 *  OutIsDot 与段索引一一对应，标记该段是“点”（画成 1:1 方块）还是“划”。
	 *  TrimStart/TrimEnd：折线首/尾各裁掉的长度（箭头区域不再画点/划，避免与箭头重叠）。 */
	void BuildSegmentPoints(const TArray<FVector2D>& Polyline, TArray<FVector2D>& OutSegments, TArray<bool>& OutIsDot, float TrimStart = 0.0f, float TrimEnd = 0.0f) const;
	/** 画实心箭头：单个三角形（顶点=Tip，底边=Base±Wing），DirIn 为指向线体内部的方向，InSize 为箭头长度。
	 *  一次 MakeCustomVerts 实心填充，顶点在绘制空间做像素对齐（与线体同一规则）；Color 为最终色（含闪烁/tint）。 */
	void AppendArrow(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry, const FVector2D& Tip, const FVector2D& DirIn, float InSize, ESlateDrawEffect DrawEffects, const FLinearColor& InColor) const;
	/** 画圆形线端：以端点为圆心的实心圆盘（直径 = 线宽），一次 MakeBox + 全圆圆角矩形笔刷。 */
	void AppendRoundCap(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry, const FVector2D& Tip, ESlateDrawEffect DrawEffects, const FLinearColor& InColor) const;
	/** 中心线拐角圆弧替换：把折线中间顶点替换为半径 Radius 的圆弧（二次贝塞尔采样成折线，控制点=原顶点）。
	 *  实线笔画包络路径传固定 CornerArcRadius（10px，与线宽无关，drawio rounded 语义）；
	 *  虚线路径传 线宽/2，让划线模式沿圆角流动。
	 *  Radius <= 0 或点数 < 3 → 原样输出；退化（共线/近掉头）顶点保持尖角。输出仍是一条完整折线。 */
	void BuildRoundedPolyline(const TArray<FVector2D>& In, float Radius, TArray<FVector2D>& Out) const;
	/** CustomVerts 图元（多边形填充/环带、箭头、点方块）无抗锯齿的**轮廓抗锯齿覆层**：
	 *  沿闭合轮廓画一次同色闭口 AA 线（MakeLines bAntialias=true，厚度 3 屏幕像素——
	 *  5.7 的 thickness 参数即屏幕像素语义）。两侧各 1px 渐变 + 中间 1px 实心核，正好
	 *  覆盖被覆边缘的 ±1px 像素量化区，外包络成为覆层的渐变边；颜色与被覆图元完全
	 *  一致，不形成可见描边。注意：厚度传 1 只有两个软边没有实心核，盖不住硬台阶
	 *  （实测教训）；屏幕宽度固定，任何缩放下柔边外观一致。 */
	void DrawContourAA(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FPaintGeometry& PaintGeom,
		const TArray<FVector2D>& ClosedLoop, const FLinearColor& InColor, ESlateDrawEffect DrawEffects) const;

protected:
	// protected：供子类（SSCADAPolygon 等）复用属性与绘制辅助
	TAttribute<TArray<FVector2D>> Points;
	TAttribute<FLinearColor> Color;
	TAttribute<float> Thickness;
	TAttribute<ESCADALineStyle> LineStyle;
	TAttribute<ESCADALineEndStyle> LineStartStyle;
	TAttribute<ESCADALineEndStyle> LineEndStyle;
	TAttribute<ESCADALineCap> LineCap;
	TAttribute<bool> bFlashEnabled;
	TAttribute<FLinearColor> FlashColor;
	/** 相位翻转（翻转组）：true = 与全局定时器取色相反（正常相位显示闪烁色、闪烁相位显示正常色） */
	TAttribute<bool> bFlashInverted;
	/** true = 中心线不做 SnapToPixel（亚像素点列直进包络/划段拆分；SVG path 采样导入用） */
	bool bSkipPixelSnap = false;
	bool bLastFlashPhaseNormal = true;
	float DashLength = 12.0f;
	float GapLength = 6.0f;
	float ArrowSize = 10.0f;

private:
	/** 几何脏标记缓存：OnPaint 的重几何（拐角圆弧替换 / 笔画包络 / 耳切三角化 / 划段拆分）
	 *  只在键变化时重算，命中时直接用缓存几何重建 draw elements（draw element 本就不能跨帧
	 *  持有，每帧照调 MakeCustomVerts/MakeLines/MakeBox，缓存的只是几何数据）。
	 *  键 = 影响几何的全部输入：Points 内容（逐元素精确比较）+ Thickness（包络半宽/圆角半径/
	 *  划模式点长/箭头裁剪量都依赖它）+ LineStyle + LineStartStyle/LineEndStyle（箭头样式经
	 *  裁剪量改变线体几何）+ LineCap。颜色/闪烁不进键（只影响 draw element 的颜色参数）。
	 *  DashLength/GapLength/ArrowSize 是 Construct 一次定值的普通成员（无 Setter），不进键。 */
	struct FGeometryCache
	{
		bool bValid = false;
		TArray<FVector2D> KeyPoints;
		float KeyThickness = 0.0f;
		ESCADALineStyle KeyLineStyle = ESCADALineStyle::Solid;
		bool KeySkipPixelSnap = false;
		ESCADALineEndStyle KeyStartStyle = ESCADALineEndStyle::None;
		ESCADALineEndStyle KeyEndStyle = ESCADALineEndStyle::None;
		ESCADALineCap KeyLineCap = ESCADALineCap::None;

		/** 取整后的中心线（虚线样式已做 T/2 圆角替换）：箭头/圆帽每帧定位也用它 */
		TArray<FVector2D> Centerline;

		// 实线路径缓存
		bool bEnvelopeValid = false;     // 包络构建成功（线宽>0 且 BuildStrokeOutline 未退化）
		bool bEnvelopeTrisValid = false; // 包络耳切三角化成功（极端尖角×超厚可能自交失败）
		TArray<FVector2D> EnvelopeClosed;      // 封闭包络（已回接首点，覆层直接用，不再每帧拷贝）
		TArray<FVector2D> EnvelopeTriVerts;    // 按三角形索引展开的填充顶点
		TArray<SlateIndex> EnvelopeIdentityIndices;

		// 虚线路径缓存（划段点列）
		TArray<FVector2D> DashPairs;
		TArray<bool> DashIsDot;
	};
	mutable FGeometryCache GeoCache;
};
