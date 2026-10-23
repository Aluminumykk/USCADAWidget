// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DesignerExtension.h"
#include "Layout/Geometry.h"
#include "WidgetReference.h"

class UWidget;
class USCADACanvasWidget;

/**
 * UMG 设计器扩展：单独选中 SCADA Canvas 时，在画布可视左上角内侧浮两个竖排按钮——
 * 「画布适配内容」：把画布 CanvasSize 收缩到内容（视觉）包围盒尺寸，全部子控件整体平移
 *   使内容左上角落到 (0,0)（四周接壤、无留白；典型场景：SVG 导入生成的画布资产被删改内容后尺寸失配）。
 * 「内容适配画布」（反向操作）：CanvasSize 不变，内容整体等比缩放+居中到画布内完整显示
 *   （类比 ScaleMode=Fit；线宽/圆角/字号/嵌套画布 Zoom 等内容内在尺度随比例缩放）。
 *
 * 机制仿 FSCADAVertexDragExtension：
 * - ExtendSelection 向设计器表面添加 FDesignerSurfaceElement（RelativeFromParent），
 *   Offset 为 TAttribute 每帧重算 → 按钮恒浮在画布可视区域的左上角内侧，
 *   与画布 Zoom / 预览缩放的任何倍率无关（换算全走绝对↔本地变换）。
 * - 半透明深色底垫在按钮之下，保证不被画布内容挡住、任何缩放下可见。
 * - 点击 = BeginTransaction + 对 Template / Preview 两实例各执行一次操作
 *   （双实例陷阱：只写一份编译/保存后回退）+ EndTransaction + MarkDesignModifed。
 *   撤销快照经 Template 侧画布与其子控件、槽的 Modify() 覆盖（Preview 为 transient 不进事务）。
 * - 按钮点击后的可见反馈 = 画布尺寸/内容位置/缩放变化本身，不做额外状态。
 *
 * 限定根画布场景：嵌套画布拟合后内容归到该画布自己的左上角 / 缩放也只发生在其内部，
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

	/** 共享点击流程：开事务 → Template 标脏并对 Template/Preview 两实例各执行一次 Operation →
	 *  关事务 + MarkDesignModifed。Template 不可得返回 false（调用方回 Unhandled） */
	bool ApplyToBothInstances(TFunctionRef<void(USCADACanvasWidget*)> Operation, const FText& SessionName);

	/** 「画布适配内容」点击：对两实例各执行一次 FitCanvasToContent */
	FReply OnFitCanvasToContentClicked();

	/** 「内容适配画布」点击：对两实例各执行一次 FitContentToCanvas */
	FReply OnFitContentToCanvasClicked();

private:
	/** 按钮距画布可视左上角的内偏移（设计器表面像素） */
	static constexpr float ButtonInset = 4.0f;
};
