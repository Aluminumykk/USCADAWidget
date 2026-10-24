// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "SCADAWidgetTypes.h"

/**
 * SCADA 画布：SConstraintCanvas 子类（与 UMG Canvas Panel 同底座，子控件以绝对设计坐标定位、可在设计器中自由拖动），
 * 在子控件下层绘制可选的背景网格。
 * 所有子图元使用画布本地坐标（左上角为原点），缩放对子内容整体缩放渲染（纯渲染变换，数据层不变）。
 * 缩放系数两个来源：手动 Zoom（ScaleMode=None），或按 CanvasSize 设计基准 × 实际分配尺寸自动计算
 * （ScaleMode=Fit/Fill/Stretch，分辨率自适应）。后续扩展平移、对齐吸附等画面配置能力。
 */
class SSCADACanvas : public SConstraintCanvas
{
public:
	SLATE_BEGIN_ARGS(SSCADACanvas)
		: _ShowGrid(true)
		, _GridSize(16.0f)
		, _GridColor(FLinearColor(1.f, 1.f, 1.f, 0.08f))
		, _Zoom(1.0f)
		, _DesignSize(FVector2D(1920.0, 1080.0))
		, _ScaleMode(ESCADACanvasScaleMode::None)
	{
	}
		SLATE_ATTRIBUTE(bool, ShowGrid)
		SLATE_ATTRIBUTE(float, GridSize)
		SLATE_ATTRIBUTE(FLinearColor, GridColor)
		SLATE_ATTRIBUTE(float, Zoom)
		SLATE_ATTRIBUTE(FVector2D, DesignSize)
		SLATE_ATTRIBUTE(ESCADACanvasScaleMode, ScaleMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetShowGrid(const TAttribute<bool>& InShowGrid);
	void SetGridSize(const TAttribute<float>& InGridSize);
	void SetGridColor(const TAttribute<FLinearColor>& InColor);
	void SetZoom(const TAttribute<float>& InZoom);
	void SetDesignSize(const TAttribute<FVector2D>& InDesignSize);
	void SetScaleMode(const TAttribute<ESCADACanvasScaleMode>& InScaleMode);

	/** 最近一次排布实际生效的缩放系数（手动 Zoom 或自动适配结果，逐轴） */
	FVector2D GetLastEffectiveScale() const { return LastEffectiveScale; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;

	/** 布局期望尺寸改由 CanvasSize 驱动（替换基类"子控件槽矩形并集"，见 cpp 注释）：
	 *  ScaleMode=None → CanvasSize×Zoom；≠None → CanvasSize（设计基准尺寸）。
	 *  使 UMG「所需（Desired）」预览下画布显示框恒等于设计尺寸（×Zoom）== 网格范围，
	 *  与"画布适配内容"收缩后的视觉内容贴边一致。 */
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	/** 按缩放模式与实际分配尺寸计算生效缩放系数（逐轴；None = 手动 Zoom 标量） */
	FVector2D ComputeEffectiveScale(const FVector2D& AreaSize) const;

	/** 生成按生效缩放变换后的子级几何（缩放≈1 时原样返回）。
	 *  OnArrangeChildren（命中/槽框）与 OnPaint（绘制）都必须用它——基类 SConstraintCanvas::OnPaint
	 *  会拿传入几何自己重新排布子控件绘制，不经 OnArrangeChildren，两处不传同一个缩放几何就会
	 *  "槽框缩放而内容不缩放"。 */
	FGeometry MakeScaledChildGeometry(const FGeometry& AllottedGeometry) const;

	TAttribute<bool> ShowGrid;
	TAttribute<float> GridSize;
	TAttribute<FLinearColor> GridColor;
	TAttribute<float> Zoom;
	TAttribute<FVector2D> DesignSize;
	TAttribute<ESCADACanvasScaleMode> ScaleMode;

	/** OnArrangeChildren 缓存的生效缩放，供 UMG 层/设计器扩展读取（手柄坐标换算） */
	mutable FVector2D LastEffectiveScale = FVector2D(1.0, 1.0);
};
