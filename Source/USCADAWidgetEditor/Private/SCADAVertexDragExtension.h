// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DesignerExtension.h"
#include "Layout/Geometry.h"
#include "Layout/Visibility.h"
#include "Styling/SlateBrush.h"
#include "WidgetReference.h"

class SWidget;
class UWidget;

/**
 * UMG 设计器扩展：选中 SCADA 图元时在画布上为每个可拖点显示手柄，直接拖动修改坐标。
 * 支持：折线（每个折点 Points[i]）、线段（StartPoint/EndPoint 两端点，索引 0/1）。
 *
 * 机制仿引擎内置 FCanvasSlotExtension（锚点 gizmo）：
 * - ExtendSelection 向设计器表面添加 FDesignerSurfaceElement（RelativeFromParent），
 *   Offset 为 TAttribute 每帧重算 → 手柄自动跟随顶点（含画布 Zoom 与预览缩放）。
 * - 手柄池固定 64 个，未用的 Collapsed：面板增删点后无需重新选中，spare 手柄自动显隐
 *   （设计器只在选中变化时重建扩展元素，见 SDesignerView::CreateExtensionWidgetsForSelection）。
 * - 拖动 = 鼠标捕获 + BeginTransaction/EndTransaction（撤销）+ 增量映射（顶点不跳到
 *   光标中心）+ 每帧同时写 Template/Preview 两个实例（双实例陷阱：两边槽缓存都要一致）。
 * - 写回走控件公开接口（折线 SetPoints / 线段 SetStartPoint、SetEndPoint）：内部完成
 *   像素取整 + 槽包围盒同步 + Slate 推送，运行时模块零改动。
 */
class FSCADAVertexDragExtension : public FDesignerExtension
{
public:
	FSCADAVertexDragExtension();

	virtual bool CanExtendSelection(const TArray<FWidgetReference>& Selection) const override;
	virtual void ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements) override;

private:
	/** 手柄边长（设计器像素；Offset 里做居中修正） */
	static constexpr float HandleSize = 10.0f;
	/** 手柄池容量（顶点数超过后需重新选中刷新，SCADA 图元实际远达不到） */
	static constexpr int32 HandlePoolSize = 64;

	UWidget* GetTemplateWidget() const;
	UWidget* GetPreviewWidget() const;

	/** 统一顶点访问：折线 = Points 数组；线段 = StartPoint/EndPoint（索引 0/1）。
	 *  读取一律以 Template（持久化真值）为准 */
	int32 GetVertexCount() const;
	bool GetVertex(int32 Index, FVector2D& OutDesignPos) const;
	/** 顶点新值写入指定实例（走公开 Set 接口：取整 + 槽同步 + Slate 推送） */
	void SetVertexOnInstance(UWidget* Widget, int32 Index, const FVector2D& NewDesignPos) const;

	/** 画布 Zoom（父控件为 SCADA Canvas 时取其 Zoom，否则按 1） */
	float GetCanvasZoom() const;
	/** 选中图元的父（画布）控件几何 */
	bool GetParentGeometry(FGeometry& OutGeometry) const;
	/** 画布设计坐标 → 设计器表面 Offset（RelativeFromParent 原点 = 画布左上角） */
	FVector2D DesignToHandleOffset(const FVector2D& DesignPos) const;
	/** 鼠标屏幕坐标 → 画布设计坐标 */
	FVector2D ScreenToDesign(const FVector2D& ScreenPos) const;

	// 手柄属性绑定
	FVector2D GetHandleOffset(int32 Index) const;
	EVisibility GetHandleVisibility(int32 Index) const;
	FSlateColor GetHandleColor(int32 Index) const;

	// 拖动状态机
	FReply HandleBeginDrag(const FGeometry& Geometry, const FPointerEvent& Event, int32 Index);
	FReply HandleDragging(const FGeometry& Geometry, const FPointerEvent& Event, int32 Index);
	FReply HandleEndDrag(const FGeometry& Geometry, const FPointerEvent& Event, int32 Index);

	/** 顶点新值写入 Template + Preview 双实例 */
	void ApplyVertexPosition(int32 Index, const FVector2D& NewDesignPos);

private:
	/** 手柄池（与 SurfaceElements 一一对应） */
	TArray<TSharedPtr<SWidget>> HandleWidgets;

	bool bDragging = false;
	int32 DragIndex = INDEX_NONE;
	/** 拖动起始时的顶点值（画布设计坐标；增量映射基准，避免顶点跳到光标中心） */
	FVector2D DragStartVertex = FVector2D::ZeroVector;
	/** 鼠标按下处的画布设计坐标 */
	FVector2D DragStartDesignPos = FVector2D::ZeroVector;
};
