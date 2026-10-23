// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DesignerExtension.h"
#include "Layout/Geometry.h"
#include "WidgetReference.h"

class UWidget;

/**
 * UMG 设计器扩展：单独选中 SCADA Canvas 时，在画布可视左上角内侧浮一个"适配内容"按钮——
 * 把画布 CanvasSize 收缩到内容包围盒尺寸，全部子控件整体平移使内容左上角落到 (0,0)
 * （四周接壤、无留白；典型场景：SVG 导入生成的画布资产被删改内容后尺寸失配）。
 *
 * 机制仿 FSCADAVertexDragExtension：
 * - ExtendSelection 向设计器表面添加 FDesignerSurfaceElement（RelativeFromParent），
 *   Offset 为 TAttribute 每帧重算 → 按钮恒浮在画布可视区域的左上角内侧，
 *   与画布 Zoom / 预览缩放的任何倍率无关（换算全走绝对↔本地变换）。
 * - 半透明深色底垫在按钮之下，保证不被画布内容挡住、任何缩放下可见。
 * - 点击 = BeginTransaction + 对 Template / Preview 两实例各执行一次 FitCanvasToContent
 *   （双实例陷阱：只写一份编译/保存后回退）+ EndTransaction + MarkDesignModifed。
 *   撤销快照经 Template 侧画布与其子控件、槽的 Modify() 覆盖（Preview 为 transient 不进事务）。
 * - 按钮点击后的可见反馈 = 画布尺寸与内容位置变化本身，不做额外状态。
 *
 * 限定根画布场景：嵌套画布拟合后内容归到该画布自己的左上角，
 * 其在外层画布中的槽矩形不会自动补偿，外层布局需自行调整。
 */
class FSCADACanvasFitExtension : public FDesignerExtension
{
public:
	FSCADACanvasFitExtension();

	virtual bool CanExtendSelection(const TArray<FWidgetReference>& Selection) const override;
	virtual void ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements) override;

private:
	UWidget* GetTemplateWidget() const;
	UWidget* GetPreviewWidget() const;

	/** 按钮位置：画布可视左上角的设计器表面坐标 + 内偏移（RelativeFromParent 原点 = 选中控件
	 *  的父容器左上角，见 SDesignerView::GetExtensionPosition；减去父原点即换算到画布左上角，
	 *  与顶点扩展 DesignToHandleOffset 同一坐标空间） */
	FVector2D GetButtonOffset() const;

	/** 撤销簿记：把 Template 实例画布与其全部子控件及槽标脏进事务（CanvasSize 在画布、
	 *  几何数据在各子控件、槽矩形在各槽对象——事务只序列化被 Modify 的对象；
	 *  Preview 实例是 transient，撤销后由设计器从 Template 重建，不必标脏） */
	static void ModifyInstanceForUndo(UWidget* CanvasWidget);

	/** 按钮点击：开事务 → Template/Preview 各自 Modify + 适配 → 关事务 + MarkDesignModifed */
	FReply OnFitClicked();

private:
	/** 按钮距画布可视左上角的内偏移（设计器表面像素） */
	static constexpr float ButtonInset = 4.0f;
};
