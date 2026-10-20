// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADALineWidget.h"

#include "Slate/SSCADALine.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADALineWidget::RebuildWidget()
{
	MyLine = SNew(SSCADALine);

	// 挂接 Slate Tick 回调 + 注册核心 Ticker 兜底（编辑器/非游戏世界守卫在基类内部）
	SetupSlotSyncCallbacks(MyLine.Get());

	return MyLine.ToSharedRef();
}

void USCADALineWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 以点位为基准：槽矩形始终跟随点位包围盒。
	// 反方向（设计器拖动/拉伸槽 → 回写点位）由 Tick 轮询 CheckSlotSync 闭环，
	// 因此这里无条件把槽对齐到点位，保证编译/重建后点位编辑不丢失。
	SyncSlotFromGeometry();

	if (MyLine.IsValid())
	{
		PushGeometryToSlate();
		MyLine->SetColor(Color);
		MyLine->SetThickness(Thickness);
		MyLine->SetLineStyle(LineStyle);
		MyLine->SetLineStartStyle(LineStartStyle);
		MyLine->SetLineEndStyle(LineEndStyle);
		MyLine->SetLineCap(LineCap);
		MyLine->SetFlashEnabled(bFlashEnabled);
		MyLine->SetFlashColor(FlashColor);
		MyLine->SetFlashInverted(bFlashInverted);
	}
}

void USCADALineWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyLine.Reset();
}

void USCADALineWidget::PushGeometryToSlate()
{
	if (MyLine.IsValid())
	{
		// 直线 = 两点折线；Slate 层保持折线能力，供后续折线控件复用。
		// Slate 拿到的是槽内局部坐标：画布设计坐标 - 槽位置。
		FVector2D SlotPos = FVector2D::ZeroVector;
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			SlotPos = CanvasSlot->GetPosition();
		}

		TArray<FVector2D> TwoPoints = { StartPoint - SlotPos, EndPoint - SlotPos };
		MyLine->SetPoints(TwoPoints);
	}
}

void USCADALineWidget::SnapPropertiesToPixel()
{
	StartPoint.X = FMath::RoundToFloat(StartPoint.X);
	StartPoint.Y = FMath::RoundToFloat(StartPoint.Y);
	EndPoint.X = FMath::RoundToFloat(EndPoint.X);
	EndPoint.Y = FMath::RoundToFloat(EndPoint.Y);
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 1.0f);
}

void USCADALineWidget::GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	OutMin = FVector2D(FMath::Min(StartPoint.X, EndPoint.X), FMath::Min(StartPoint.Y, EndPoint.Y));
	OutMax = FVector2D(FMath::Max(StartPoint.X, EndPoint.X), FMath::Max(StartPoint.Y, EndPoint.Y));
}

void USCADALineWidget::TranslateGeometryBy(const FVector2D& Delta)
{
	StartPoint += Delta;
	EndPoint += Delta;
}

void USCADALineWidget::ResetInteractionState()
{
	bDegenerateDrag = false;
	DegenerateTarget = -1;
}

void USCADALineWidget::MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart)
{
	// 交互首帧簿记：此刻缓存仍是鼠标按下时的槽值（= 设计器 StartingOffsets 快照——
	// STransformHandle::Resize 每帧从该快照绝对重写槽矩形，非增量）。
	// 横平竖直（单轴退化）交互锁定期内模式，端点跟随映射的全部基准在此取样。
	if (bInteractionStart)
	{
		InteractionStartPos = CachedSlotPos;
		InteractionStartSize = CachedSlotSize;
		StartPointAtInteractionStart = StartPoint;
		EndPointAtInteractionStart = EndPoint;
		// 恰有一轴跨度为 0（水平：Y 相等；竖直：X 相等）才启用端点跟随；
		// 点（两轴全退化）走原缩放路径
		const bool bDegenerateY = StartPoint.Y == EndPoint.Y;
		const bool bDegenerateX = StartPoint.X == EndPoint.X;
		bDegenerateDrag = bDegenerateX != bDegenerateY;
		bDegenerateDragHorizontal = bDegenerateY;
		DegenerateTarget = -1;
	}

	// 单轴退化（水平/竖直线）把手拖动：缩放映射对两个等偏移端点恒映射到同一边，
	// 线永远拉不斜（死锁），改为"被抓角端点跟随"。仅编辑器设计器世界启用，
	// 运行时/PIE 槽输入走原缩放路径，行为不变。
	if (bDegenerateDrag && GIsEditor && (!GetWorld() || !GetWorld()->IsGameWorld()))
	{
		MapDegenerateSlotRect(CanvasSlot, RawPos, RawSize);
		return;
	}

	// 规范化：min 位置 + 正尺寸；记录原始符号
	FVector2D CPos = RawPos;
	FVector2D CSize = RawSize;
	const bool bNegX = CSize.X < 0.0;
	const bool bNegY = CSize.Y < 0.0;
	if (bNegX) { CPos.X += CSize.X; CSize.X = -CSize.X; }
	if (bNegY) { CPos.Y += CSize.Y; CSize.Y = -CSize.Y; }

	// 翻转沿检测：符号相对上一帧原始值变化 → 该轴在矩形内镜像一次
	const bool bToggleX = (bNegX != bPrevRawNegX);
	const bool bToggleY = (bNegY != bPrevRawNegY);
	bPrevRawNegX = bNegX;
	bPrevRawNegY = bNegY;

	// 规范空间增量映射（正尺寸，无符号问题），除零保护钳到 1
	FVector2D OldSize = CachedSlotSize;
	if (FMath::IsNearlyZero((float)OldSize.X)) { OldSize.X = 1.0; }
	if (FMath::IsNearlyZero((float)OldSize.Y)) { OldSize.Y = 1.0; }
	const FVector2D Scale(CSize.X / OldSize.X, CSize.Y / OldSize.Y);
	StartPoint = CPos + (StartPoint - CachedSlotPos) * Scale;
	EndPoint = CPos + (EndPoint - CachedSlotPos) * Scale;

	// 翻转沿：在新规范矩形内镜像
	if (bToggleX)
	{
		StartPoint.X = CPos.X + (CSize.X - (StartPoint.X - CPos.X));
		EndPoint.X = CPos.X + (CSize.X - (EndPoint.X - CPos.X));
	}
	if (bToggleY)
	{
		StartPoint.Y = CPos.Y + (CSize.Y - (StartPoint.Y - CPos.Y));
		EndPoint.Y = CPos.Y + (CSize.Y - (EndPoint.Y - CPos.Y));
	}

	// 槽写回正向规范矩形（设计器下一帧会覆盖；始终以缓存判定新输入）
	{
		TGuardValue<bool> Guard(bSyncingGeometry, true);
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetPosition(CPos);
		CanvasSlot->SetSize(CSize);
	}
	CachedSlotPos = CPos;
	CachedSlotSize = CSize;
	PushGeometryToSlate();
}

void USCADALineWidget::MapDegenerateSlotRect(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize)
{
	const FVector2D& P0 = InteractionStartPos;
	const FVector2D& S0 = InteractionStartSize;

	// 整体平移（拖线体 / 方向键微调 / 属性面板改 X、Y）：尺寸与起点一致，仅位置变化
	if (RawSize.Equals(S0))
	{
		const FVector2D Delta = RawPos - P0;
		StartPoint = StartPointAtInteractionStart + Delta;
		EndPoint = EndPointAtInteractionStart + Delta;
		WriteBackBoundsSlot(CanvasSlot);
		return;
	}

	// 把手方向解码（对拖动起点矩形）：Position（= Offsets.Left/Top，锚点 0）精确跟随
	// 左/上方向把手（其带对称扩展），右/下方向把手只动尺寸、pos 不动 → 被抓角 = pos 或 pos + size。
	// 解码拖过对边（尺寸符号翻转）后依然稳定：左/上把手永远移动 pos，右/下把手永远不动 pos。
	auto DecodeAxis = [](float InRawPos, float InRawSize, float StartPosV, float StartSizeV) -> int8
	{
		if (!FMath::IsNearlyEqual(InRawPos, StartPosV, 0.01f)) { return -1; }
		if (!FMath::IsNearlyEqual(InRawSize, StartSizeV, 0.01f)) { return 1; }
		return 0;
	};
	const int8 HX = DecodeAxis(RawPos.X, RawSize.X, P0.X, S0.X);
	const int8 HY = DecodeAxis(RawPos.Y, RawSize.Y, P0.Y, S0.Y);

	// 退化轴（水平线 = Y，竖直线 = X）被抓角位移与选择轴（水平线 = X 选左/右端点；
	// 竖直线 = Y 选上/下端点）解码。解码不出 = 该轴无此分量。
	const int8 DegAxis = bDegenerateDragHorizontal ? HY : HX;
	const int8 Selecting = bDegenerateDragHorizontal ? HX : HY;
	if (DegAxis == 0)
	{
		// 退化轴无输入（如水平线拖左中/右中把手的纯水平段）：不动
		WriteBackBoundsSlot(CanvasSlot);
		return;
	}
	const float DegDelta = bDegenerateDragHorizontal
		? (DegAxis < 0 ? RawPos.Y - P0.Y : RawPos.Y + RawSize.Y - (P0.Y + S0.Y))
		: (DegAxis < 0 ? RawPos.X - P0.X : RawPos.X + RawSize.X - (P0.X + S0.X));

	if (Selecting == 0)
	{
		// 中点把手（水平线上下拖 / 竖直线左右拖）：两端点沿退化轴整体平移，
		// 线保持横平竖直（被抓边精确跟随鼠标）。仅鼠标按住时映射——属性面板
		// 改槽（如给水平线设高度）不产生平移，槽自动回正到包围盒。
		if (FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
		{
			const FVector2D DegOffset = bDegenerateDragHorizontal ? FVector2D(0.0, DegDelta) : FVector2D(DegDelta, 0.0);
			StartPoint = StartPointAtInteractionStart + DegOffset;
			EndPoint = EndPointAtInteractionStart + DegOffset;
		}
		WriteBackBoundsSlot(CanvasSlot);
		return;
	}

	// 锁定目标端点 = 数据中靠近被拖侧的那个端点（锁前只可能做过两端的整体平移，
	// 相对次序不变，此刻两端仍未交叉）
	if (DegenerateTarget < 0)
	{
		const bool bStartNearer = bDegenerateDragHorizontal
			? (Selecting < 0 ? StartPoint.X <= EndPoint.X : StartPoint.X >= EndPoint.X)
			: (Selecting < 0 ? StartPoint.Y <= EndPoint.Y : StartPoint.Y >= EndPoint.Y);
		DegenerateTarget = bStartNearer ? 0 : 1;
	}

	// 被抓角当前位置 G 与起始位置 G0 按同一解码规则取值（未解码轴取 pos 分量，差恒 0）；
	// 目标端点 = 其交互起始副本 + (G - G0)，另一端点为基准保持不动。
	// 绝对位移基准 ⇒ 拖过对边端点互换、快速甩动都无累积误差/双重计数；
	// 中点把手阶段的整体平移已含在副本差里，锁帧处按当帧 (dx, dy) 增量连续衔接。
	const FVector2D G(
		HX < 0 ? RawPos.X : (HX > 0 ? RawPos.X + RawSize.X : RawPos.X),
		HY < 0 ? RawPos.Y : (HY > 0 ? RawPos.Y + RawSize.Y : RawPos.Y));
	const FVector2D G0(
		HX < 0 ? P0.X : (HX > 0 ? P0.X + S0.X : P0.X),
		HY < 0 ? P0.Y : (HY > 0 ? P0.Y + S0.Y : P0.Y));

	FVector2D& Target = DegenerateTarget == 0 ? StartPoint : EndPoint;
	const FVector2D& TargetStart = DegenerateTarget == 0 ? StartPointAtInteractionStart : EndPointAtInteractionStart;
	Target = TargetStart + (G - G0);

	WriteBackBoundsSlot(CanvasSlot);
}

void USCADALineWidget::WriteBackBoundsSlot(UCanvasPanelSlot* CanvasSlot)
{
	FVector2D Min, Max;
	GetGeometryBounds(Min, Max);
	const FVector2D Size(FMath::Max(Max.X - Min.X, 1.0), FMath::Max(Max.Y - Min.Y, 1.0));
	{
		TGuardValue<bool> Guard(bSyncingGeometry, true);
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetPosition(Min);
		CanvasSlot->SetSize(Size);
	}
	CachedSlotPos = Min;
	CachedSlotSize = Size;
	PushGeometryToSlate();
}

void USCADALineWidget::SetStartPoint(FVector2D InPoint)
{
	StartPoint = InPoint;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADALineWidget::SetEndPoint(FVector2D InPoint)
{
	EndPoint = InPoint;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADALineWidget::SetColor(FLinearColor InColor)
{
	Color = InColor;
	if (MyLine.IsValid())
	{
		MyLine->SetColor(Color);
	}
}

void USCADALineWidget::SetThickness(float InThickness)
{
	Thickness = InThickness;
	SnapPropertiesToPixel();
	if (MyLine.IsValid())
	{
		MyLine->SetThickness(Thickness);
	}
}

void USCADALineWidget::SetLineStyle(ESCADALineStyle InStyle)
{
	LineStyle = InStyle;
	if (MyLine.IsValid())
	{
		MyLine->SetLineStyle(LineStyle);
	}
}

void USCADALineWidget::SetLineStartStyle(ESCADALineEndStyle InStyle)
{
	LineStartStyle = InStyle;
	if (MyLine.IsValid())
	{
		MyLine->SetLineStartStyle(LineStartStyle);
	}
}

void USCADALineWidget::SetLineEndStyle(ESCADALineEndStyle InStyle)
{
	LineEndStyle = InStyle;
	if (MyLine.IsValid())
	{
		MyLine->SetLineEndStyle(LineEndStyle);
	}
}

void USCADALineWidget::SetLineCap(ESCADALineCap InCap)
{
	LineCap = InCap;
	if (MyLine.IsValid())
	{
		MyLine->SetLineCap(LineCap);
	}
}

void USCADALineWidget::SetFlashEnabled(bool bInEnabled)
{
	bFlashEnabled = bInEnabled;
	if (MyLine.IsValid())
	{
		MyLine->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADALineWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColor = InColor;
	if (MyLine.IsValid())
	{
		MyLine->SetFlashColor(FlashColor);
	}
}

void USCADALineWidget::SetFlashInverted(bool bInInverted)
{
	bFlashInverted = bInInverted;
	if (MyLine.IsValid())
	{
		MyLine->SetFlashInverted(bFlashInverted);
	}
}

#if WITH_EDITOR
void USCADALineWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 编辑器属性面板输入完成后立即取整，面板显示值即为最终绘制值
	SnapPropertiesToPixel();

	// 端点坐标被编辑 → 槽矩形（X/Y/宽/高）同步更新（须在 Super 之前，
	// Super 链上的 SynchronizeProperties 以槽为准回写，槽先更新才不会把新值冲掉）。
	// 注意要查 MemberProperty：编辑 StartPoint.X 这类子字段时 Property 是内层的 X，
	// 只有 MemberProperty（所属成员）才是 StartPoint/EndPoint 本身。
	const FName MemberPropName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropName == GET_MEMBER_NAME_CHECKED(USCADALineWidget, StartPoint) ||
		MemberPropName == GET_MEMBER_NAME_CHECKED(USCADALineWidget, EndPoint))
	{
		SyncSlotFromGeometry();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

const FText USCADALineWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
