// Copyright Pr_UEDraw. All Rights Reserved.

#include "SvgImport/SCADASvgImportFactory.h"

#include "SvgImport/SCADASvgDocument.h"
#include "SvgImport/SCADASvgImportSettings.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"

#include "SCADAWidgetTypes.h"
#include "Widgets/SCADACanvasWidget.h"
#include "Widgets/SCADALineWidget.h"
#include "Widgets/SCADAPolylineWidget.h"
#include "Widgets/SCADAPolygonWidget.h"
#include "Widgets/SCADAEllipseWidget.h"
#include "Widgets/SCADACircleWidget.h"
#include "Widgets/SCADARectangleWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogSCADASvgImport, Log, All);

namespace SCADASvgImport
{
	/** 生成合法且唯一的控件名（SVG id 净化；空/重名用 类型_序号 兜底） */
	static FName MakeWidgetName(const FString& Id, const TCHAR* Fallback, TSet<FString>& UsedNames)
	{
		FString Base;
		for (const TCHAR Ch : Id)
		{
			Base += (FChar::IsAlnum(Ch) || Ch == TEXT('_')) ? Ch : TEXT('_');
		}
		if (Base.IsEmpty() || FChar::IsDigit(Base[0]))
		{
			Base = FString::Printf(TEXT("%s%s"), Fallback, Base.IsEmpty() ? TEXT("") : *Base);
		}

		FString Candidate = Base;
		int32 Suffix = 1;
		while (UsedNames.Contains(Candidate))
		{
			Candidate = FString::Printf(TEXT("%s_%d"), *Base, Suffix++);
		}
		UsedNames.Add(Candidate);
		return FName(*Candidate);
	}

	/** 应用填充/描边样式到带填充语义的图元（Rect/Circle/Ellipse/Polygon 成员同名，模板复用） */
	template <typename TWidget>
	static void ApplyFillBorderStyle(TWidget* Widget, const FSCADASvgStyle& Style)
	{
		if (Style.Fill.IsSet())
		{
			Widget->FillColor = *Style.Fill;
			Widget->FillPattern = ESCADAFillPattern::Solid;
		}
		else
		{
			Widget->FillPattern = ESCADAFillPattern::Transparent;
		}

		if (Style.Stroke.IsSet() && Style.StrokeWidth > 0.0f)
		{
			Widget->BorderColor = *Style.Stroke;
			Widget->Thickness = Style.StrokeWidth;
		}
		else
		{
			Widget->Thickness = 0.0f;
		}
	}

	/** 应用描边样式到线类图元（Line/Polyline：无填充概念，Color = 描边色） */
	template <typename TWidget>
	static void ApplyStrokeOnlyStyle(TWidget* Widget, const FSCADASvgStyle& Style)
	{
		if (Style.Stroke.IsSet())
		{
			Widget->Color = *Style.Stroke;
		}
		Widget->Thickness = FMath::Max(Style.StrokeWidth, 0.0f);
	}

	/** 把子控件加到父画布并返回槽（统一设置位置/尺寸的入口） */
	static UCanvasPanelSlot* AddToCanvas(USCADACanvasWidget* ParentCanvas, UWidget* Child,
		const FVector2D& Position, const FVector2D& Size)
	{
		UCanvasPanelSlot* CanvasSlot = ParentCanvas->AddChildToCanvas(Child);
		CanvasSlot->SetPosition(Position);
		CanvasSlot->SetSize(Size);
		CanvasSlot->SetZOrder(0);
		return CanvasSlot;
	}

	/** 生成的控件默认不设为变量（IsVariable=false）——元件里需要蓝图/动画驱动的部件很少，
	 *  默认全是变量会污染 Widget Blueprint 的变量列表；要驱动的个别控件在设计器里手动勾 Is Variable 即可。
	 *  例外：带 rotate 的分组画布（旋转动画的直接驱动对象）保持变量。 */
	template <typename TWidget>
	static TWidget* ConstructNonVariable(UWidgetTree* WidgetTree, UClass* WidgetClass, FName Name)
	{
		TWidget* W = WidgetTree->ConstructWidget<TWidget>(WidgetClass, Name);
		W->bIsVariable = false;
		return W;
	}

	/** 递归构建：SVG 节点 → SCADA 图元控件（叶子坐标 = viewBox 空间，与画布设计坐标 1:1；
	 *  含 rotate 的组容器用 GroupContainerSize——组内子元素保持原始 SVG 坐标，与顶层裁剪无关） */
	static void BuildNode(UWidgetTree* WidgetTree, USCADACanvasWidget* ParentCanvas, const FVector2D& GroupContainerSize,
		const FSCADASvgNode& Node, TSet<FString>& UsedNames)
	{
		switch (Node.Kind)
		{
		case ESCADASvgNodeKind::Group:
		{
			// 分组 → 嵌套 SCADA 画布（尺寸 = 组容器坐标空间 = 裁剪前 viewBox，子元素坐标同空间 1:1）；
			// rotate(a,cx,cy) → RenderTransform：动画锚点保留，用户可直接驱动 Angle（如泵叶轮）
			USCADACanvasWidget* GroupCanvas = ConstructNonVariable<USCADACanvasWidget>(WidgetTree,
				USCADACanvasWidget::StaticClass(), MakeWidgetName(Node.Id, TEXT("Group"), UsedNames));
			GroupCanvas->CanvasSize = GroupContainerSize;
			GroupCanvas->ScaleMode = ESCADACanvasScaleMode::None;
			GroupCanvas->bShowGrid = false;
			AddToCanvas(ParentCanvas, GroupCanvas, Node.Translation, GroupContainerSize);

			if (Node.bHasRotation)
			{
				GroupCanvas->bIsVariable = true;
				GroupCanvas->SetRenderTransformAngle(Node.RotationAngle);
				GroupCanvas->SetRenderTransformPivot(FVector2D(
					GroupContainerSize.X > 0.0 ? Node.RotationCenter.X / GroupContainerSize.X : 0.5,
					GroupContainerSize.Y > 0.0 ? Node.RotationCenter.Y / GroupContainerSize.Y : 0.5));
			}

			for (const FSCADASvgNode& Child : Node.Children)
			{
				BuildNode(WidgetTree, GroupCanvas, GroupContainerSize, Child, UsedNames);
			}
			break;
		}
		case ESCADASvgNodeKind::Rect:
		{
			if (Node.P2.X <= 0.0 || Node.P2.Y <= 0.0)
			{
				UE_LOG(LogSCADASvgImport, Warning, TEXT("Rect '%s' 宽/高无效（%.1f x %.1f），已跳过"), *Node.Id, Node.P2.X, Node.P2.Y);
				return;
			}
			USCADARectangleWidget* W = ConstructNonVariable<USCADARectangleWidget>(WidgetTree,
				USCADARectangleWidget::StaticClass(), MakeWidgetName(Node.Id, TEXT("Rect"), UsedNames));
			W->Position = Node.P1;
			W->Size = Node.P2;
			W->CornerRadiusX = (float)Node.CornerRadius.X;
			W->CornerRadiusY = (float)Node.CornerRadius.Y;
			ApplyFillBorderStyle(W, Node.Style);
			AddToCanvas(ParentCanvas, W, Node.P1, Node.P2);
			break;
		}
		case ESCADASvgNodeKind::Circle:
		{
			if (Node.Radius <= 0.0f)
			{
				UE_LOG(LogSCADASvgImport, Warning, TEXT("Circle '%s' 半径无效，已跳过"), *Node.Id);
				return;
			}
			USCADACircleWidget* W = ConstructNonVariable<USCADACircleWidget>(WidgetTree,
				USCADACircleWidget::StaticClass(), MakeWidgetName(Node.Id, TEXT("Circle"), UsedNames));
			W->Center = Node.P1;
			W->Radius = Node.Radius;
			ApplyFillBorderStyle(W, Node.Style);
			AddToCanvas(ParentCanvas, W, Node.P1 - FVector2D(Node.Radius, Node.Radius), FVector2D(Node.Radius * 2.0f, Node.Radius * 2.0f));
			break;
		}
		case ESCADASvgNodeKind::Ellipse:
		{
			if (Node.P2.X <= 0.0 || Node.P2.Y <= 0.0)
			{
				UE_LOG(LogSCADASvgImport, Warning, TEXT("Ellipse '%s' 半径无效，已跳过"), *Node.Id);
				return;
			}
			USCADAEllipseWidget* W = ConstructNonVariable<USCADAEllipseWidget>(WidgetTree,
				USCADAEllipseWidget::StaticClass(), MakeWidgetName(Node.Id, TEXT("Ellipse"), UsedNames));
			W->Center = Node.P1;
			W->RadiusX = (float)Node.P2.X;
			W->RadiusY = (float)Node.P2.Y;
			ApplyFillBorderStyle(W, Node.Style);
			AddToCanvas(ParentCanvas, W, Node.P1 - Node.P2, Node.P2 * 2.0);
			break;
		}
		case ESCADASvgNodeKind::Line:
		{
			if (!Node.Style.Stroke.IsSet())
			{
				UE_LOG(LogSCADASvgImport, Warning, TEXT("Line '%s' 无 stroke（不可见），已跳过"), *Node.Id);
				return;
			}
			USCADALineWidget* W = ConstructNonVariable<USCADALineWidget>(WidgetTree,
				USCADALineWidget::StaticClass(), MakeWidgetName(Node.Id, TEXT("Line"), UsedNames));
			W->StartPoint = Node.P1;
			W->EndPoint = Node.P2;
			ApplyStrokeOnlyStyle(W, Node.Style);
			const FVector2D Min = FVector2D(FMath::Min(Node.P1.X, Node.P2.X), FMath::Min(Node.P1.Y, Node.P2.Y));
			const FVector2D Max = FVector2D(FMath::Max(Node.P1.X, Node.P2.X), FMath::Max(Node.P1.Y, Node.P2.Y));
			AddToCanvas(ParentCanvas, W, Min, FVector2D(FMath::Max(Max.X - Min.X, 1.0), FMath::Max(Max.Y - Min.Y, 1.0)));
			break;
		}
		case ESCADASvgNodeKind::Polyline:
		{
			if (Node.Points.Num() < 2)
			{
				UE_LOG(LogSCADASvgImport, Warning, TEXT("Polyline '%s' 顶点不足 2 个，已跳过"), *Node.Id);
				return;
			}
			if (Node.Style.Fill.IsSet())
			{
				UE_LOG(LogSCADASvgImport, Warning, TEXT("Polyline '%s' 的 fill 被忽略（折线无填充概念）"), *Node.Id);
			}
			USCADAPolylineWidget* W = ConstructNonVariable<USCADAPolylineWidget>(WidgetTree,
				USCADAPolylineWidget::StaticClass(), MakeWidgetName(Node.Id, TEXT("Polyline"), UsedNames));
			W->Points = Node.Points;
			if (Node.bSubPixelSampling) { W->bSnapToPixel = false; }
			ApplyStrokeOnlyStyle(W, Node.Style);
			FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
			FVector2D Max(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
			for (const FVector2D& P : Node.Points)
			{
				Min = FVector2D(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y));
				Max = FVector2D(FMath::Max(Max.X, P.X), FMath::Max(Max.Y, P.Y));
			}
			AddToCanvas(ParentCanvas, W, Min, FVector2D(FMath::Max(Max.X - Min.X, 1.0), FMath::Max(Max.Y - Min.Y, 1.0)));
			break;
		}
		case ESCADASvgNodeKind::Polygon:
		{
			if (Node.Points.Num() < 3)
			{
				UE_LOG(LogSCADASvgImport, Warning, TEXT("Polygon '%s' 顶点不足 3 个，已跳过"), *Node.Id);
				return;
			}
			USCADAPolygonWidget* W = ConstructNonVariable<USCADAPolygonWidget>(WidgetTree,
				USCADAPolygonWidget::StaticClass(), MakeWidgetName(Node.Id, TEXT("Polygon"), UsedNames));
			W->Points = Node.Points;
			if (Node.bSubPixelSampling) { W->bSnapToPixel = false; }
			ApplyFillBorderStyle(W, Node.Style);
			FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
			FVector2D Max(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
			for (const FVector2D& P : Node.Points)
			{
				Min = FVector2D(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y));
				Max = FVector2D(FMath::Max(Max.X, P.X), FMath::Max(Max.Y, P.Y));
			}
			AddToCanvas(ParentCanvas, W, Min, FVector2D(FMath::Max(Max.X - Min.X, 1.0), FMath::Max(Max.Y - Min.Y, 1.0)));
			break;
		}
		}
	}

	/** 文档 → WidgetTree：根 = SCADA 画布（CanvasSize = viewBox 尺寸——"SVG 原始大小"的记录，
	 *  也是手动开启 Fit/Stretch 时的缩放基准；ScaleMode 默认 None：内容按 viewBox 坐标 1:1
	 *  画在槽左上角，符合元件"按原始尺寸放置、放大由使用者显式控制"的直觉），
	 *  SVG 文档顺序 = 添加顺序（后者在上层） */
	static void BuildWidgetTree(UWidgetTree* WidgetTree, const FSCADASvgDocument& Doc)
	{
		USCADACanvasWidget* RootCanvas = WidgetTree->ConstructWidget<USCADACanvasWidget>(
			USCADACanvasWidget::StaticClass(), TEXT("SCADACanvas"));
		RootCanvas->CanvasSize = Doc.ViewBoxSize;
		RootCanvas->ScaleMode = ESCADACanvasScaleMode::None;
		RootCanvas->bShowGrid = false;
		WidgetTree->RootWidget = RootCanvas;

		TSet<FString> UsedNames;
		UsedNames.Add(TEXT("SCADACanvas"));
		for (const FSCADASvgNode& Node : Doc.Roots)
		{
			BuildNode(WidgetTree, RootCanvas, Doc.GroupContainerSize, Node, UsedNames);
		}
	}
}

USCADASvgImportFactory::USCADASvgImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bText = true;
	bEditorImport = true;
	SupportedClass = UWidgetBlueprint::StaticClass();
	Formats.Add(TEXT("svg;SVG File (SCADA Widget)"));
}

UObject* USCADASvgImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	FString SvgText;
	if (!FFileHelper::LoadFileToString(SvgText, *Filename))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("SVG 导入失败：无法读取文件 %s"), *Filename);
		return nullptr;
	}

	FSCADASvgDocument Doc;
	if (!FSCADASvgDocument::Parse(SvgText, Doc))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("SVG 导入失败：XML 无法解析（详见输出日志 LogSCADASvg）"));
		return nullptr;
	}

	// 按内容包围盒裁剪 viewBox（Project Settings → SCADA SVG Import；消除图形四周留白）
	if (const USCADASvgImportSettings* Settings = GetDefault<USCADASvgImportSettings>())
	{
		if (Settings->bCropViewBoxToContent)
		{
			Doc.CropToContent(Settings->CropPadding);
		}
	}

	// 与 UWidgetBlueprintFactory 同路径创建 Widget Blueprint（WidgetTree 由 CreateBlueprint 内部建立）
	UWidgetBlueprint* NewBP = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), InParent, InName, BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));

	SCADASvgImport::BuildWidgetTree(NewBP->WidgetTree, Doc);

	UE_LOG(LogSCADASvgImport, Log, TEXT("SVG 导入完成：%s → %s"), *Filename, *NewBP->GetName());
	return NewBP;
}
