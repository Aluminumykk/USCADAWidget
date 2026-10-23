// Copyright Pr_UEDraw. All Rights Reserved.

#include "SCADACanvasFitExtension.h"

#include "Widgets/SCADACanvasWidget.h"

#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "IUMGDesigner.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "USCADAWidgetEditor"

FSCADACanvasFitExtension::FSCADACanvasFitExtension()
{
	ExtensionId = FName(TEXT("SCADACanvasFit"));
}

bool FSCADACanvasFitExtension::CanExtendSelection(const TArray<FWidgetReference>& Selection) const
{
	if (Selection.Num() != 1)
	{
		return false;
	}
	const UWidget* Template = Selection[0].GetTemplate();
	return Template != nullptr && Template->IsA<USCADACanvasWidget>();
}

void FSCADACanvasFitExtension::ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements)
{
	SelectionCache = Selection;

	// 半透明深色底（与顶点手柄同样用 WhiteBrush 纯色着色）垫底，保证按钮不被画布内容挡住；
	// 竖排两个操作按钮：画布适配内容（收缩画布）/ 内容适配画布（等比缩放内容）
	const TSharedRef<SWidget> FitButtons =
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.6f))
		.Padding(3.0f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.ContentPadding(FMargin(8.0f, 3.0f))
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("FitCanvasToContentButton", "画布适配内容"))
				.ToolTipText(LOCTEXT("FitCanvasToContentTooltip", "把画布尺寸收缩到内容包围盒，子控件整体平移到四周接壤"))
				.OnClicked(this, &FSCADACanvasFitExtension::OnFitCanvasToContentClicked)
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(8.0f, 3.0f))
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("FitContentToCanvasButton", "内容适配画布"))
				.ToolTipText(LOCTEXT("FitContentToCanvasTooltip", "画布尺寸不变，把内容等比缩放并居中到画布内完整显示（线宽/圆角/字号等随比例缩放）"))
				.OnClicked(this, &FSCADACanvasFitExtension::OnFitContentToCanvasClicked)
			]
		];

	FitButtons->SlatePrepass();

	// Offset 每帧重算：按钮恒浮在画布可视左上角内侧，跟随画布移动/缩放（与顶点手柄同一机制）
	TAttribute<FVector2D> Offset = TAttribute<FVector2D>::Create(
		TAttribute<FVector2D>::FGetter::CreateSP(SharedThis(this), &FSCADACanvasFitExtension::GetButtonOffset));
	SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(
		FitButtons, EExtensionLayoutLocation::RelativeFromParent, Offset, FVector2D(0, 0)));
}

UWidget* FSCADACanvasFitExtension::GetTemplateWidget() const
{
	return SelectionCache.Num() == 1 ? SelectionCache[0].GetTemplate() : nullptr;
}

UWidget* FSCADACanvasFitExtension::GetPreviewWidget() const
{
	return SelectionCache.Num() == 1 ? SelectionCache[0].GetPreview() : nullptr;
}

FVector2D FSCADACanvasFitExtension::GetButtonOffset() const
{
	// SDesignerView::GetExtensionPosition 对 RelativeFromParent 的计算是
	// FinalPosition = 设计器表面(父容器 local(0,0)) + Offset，所以本 Offset =
	// 设计器表面(画布左上角) - 设计器表面(父容器左上角) + 内偏移。
	// 换算全部走绝对↔本地变换（同顶点扩展 DesignToHandleOffset），画布 Zoom /
	// 预览缩放任意倍率下按钮都稳定贴在画布可视左上角内侧。
	if (Designer == nullptr || SelectionCache.Num() != 1)
	{
		return FVector2D::ZeroVector;
	}

	FGeometry CanvasGeometry, ParentGeometry;
	if (!Designer->GetWidgetGeometry(SelectionCache[0], CanvasGeometry)
		|| !Designer->GetWidgetParentGeometry(SelectionCache[0], ParentGeometry))
	{
		return FVector2D::ZeroVector;
	}

	const FGeometry DesignerGeometry = Designer->GetDesignerGeometry();
	const FVector2D CanvasTopLeft = DesignerGeometry.AbsoluteToLocal(CanvasGeometry.LocalToAbsolute(FVector2D::ZeroVector));
	const FVector2D ParentTopLeft = DesignerGeometry.AbsoluteToLocal(ParentGeometry.LocalToAbsolute(FVector2D::ZeroVector));
	return CanvasTopLeft - ParentTopLeft + FVector2D(ButtonInset, ButtonInset);
}

void FSCADACanvasFitExtension::ModifyInstanceForUndo(UWidget* CanvasWidget)
{
	CanvasWidget->Modify();
	// 画布尺寸改动在画布对象、图元几何数据在各子控件对象、槽矩形在各槽对象（相互独立的
	// UObject）——事务只序列化被 Modify() 的对象，必须分别标脏才能完整撤销
	if (UPanelWidget* Panel = Cast<UPanelWidget>(CanvasWidget))
	{
		for (UPanelSlot* PanelSlot : Panel->GetSlots())
		{
			UWidget* Child = (PanelSlot != nullptr) ? PanelSlot->Content : nullptr;
			if (Child == nullptr)
			{
				continue;
			}
			Child->Modify();
			PanelSlot->Modify();
		}
	}
}

bool FSCADACanvasFitExtension::ApplyToBothInstances(TFunctionRef<void(USCADACanvasWidget*)> Operation, const FText& SessionName)
{
	USCADACanvasWidget* Template = Cast<USCADACanvasWidget>(GetTemplateWidget());
	if (Template == nullptr)
	{
		return false;
	}

	BeginTransaction(SessionName);

	// 双实例陷阱：Template（Outer=WidgetTree，持久化真值）与 Preview（Outer=WidgetTree_0，
	// 负责显示）是两棵不同的对象树，两棵各执行一次；只写一份编译/保存后回退。
	// 各自的操作对象是自己的 Slots/子控件，数据两棵一致 → 两棵算出的缩放/平移量相同。
	ModifyInstanceForUndo(Template);
	Operation(Template);
	if (USCADACanvasWidget* Preview = Cast<USCADACanvasWidget>(GetPreviewWidget()))
	{
		Operation(Preview);
	}

	EndTransaction();

	if (Designer != nullptr)
	{
		Designer->MarkDesignModifed(false);
	}

	// 画布尺寸/内容位置/内容缩放变化本身就是可见反馈，不做额外提示
	return true;
}

FReply FSCADACanvasFitExtension::OnFitCanvasToContentClicked()
{
	if (!ApplyToBothInstances(
		[](USCADACanvasWidget* Canvas) { Canvas->FitCanvasToContent(); },
		LOCTEXT("FitCanvasToContentTransaction", "画布适配内容")))
	{
		return FReply::Unhandled();
	}
	return FReply::Handled();
}

FReply FSCADACanvasFitExtension::OnFitContentToCanvasClicked()
{
	if (!ApplyToBothInstances(
		[](USCADACanvasWidget* Canvas) { Canvas->FitContentToCanvas(); },
		LOCTEXT("FitContentToCanvasTransaction", "内容适配画布")))
	{
		return FReply::Unhandled();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
