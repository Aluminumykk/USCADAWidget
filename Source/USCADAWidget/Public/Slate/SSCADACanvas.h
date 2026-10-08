// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Layout/SConstraintCanvas.h"

/**
 * SCADA 画布：SConstraintCanvas 子类（与 UMG Canvas Panel 同底座，子控件以绝对设计坐标定位、可在设计器中自由拖动），
 * 在子控件下层绘制可选的背景网格。
 * 所有子图元使用画布本地坐标（左上角为原点），Zoom 对子内容整体缩放渲染，
 * 便于跨平台/分辨率适配。后续扩展平移、对齐吸附等画面配置能力。
 */
class USCADAWIDGET_API SSCADACanvas : public SConstraintCanvas
{
public:
	SLATE_BEGIN_ARGS(SSCADACanvas)
		: _ShowGrid(true)
		, _GridSize(16.0f)
		, _GridColor(FLinearColor(1.f, 1.f, 1.f, 0.08f))
		, _Zoom(1.0f)
	{
	}
		SLATE_ATTRIBUTE(bool, ShowGrid)
		SLATE_ATTRIBUTE(float, GridSize)
		SLATE_ATTRIBUTE(FLinearColor, GridColor)
		SLATE_ATTRIBUTE(float, Zoom)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetShowGrid(const TAttribute<bool>& InShowGrid);
	void SetGridSize(const TAttribute<float>& InGridSize);
	void SetGridColor(const TAttribute<FLinearColor>& InColor);
	void SetZoom(const TAttribute<float>& InZoom);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;

private:
	TAttribute<bool> ShowGrid;
	TAttribute<float> GridSize;
	TAttribute<FLinearColor> GridColor;
	TAttribute<float> Zoom;
};
