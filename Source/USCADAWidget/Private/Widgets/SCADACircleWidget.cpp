// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADACircleWidget.h"

#include "Slate/SSCADAEllipse.h"
#include "Components/CanvasPanelSlot.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

// 临时调试日志（排查编译时槽同步路径，定位后移除）

TSharedRef<SWidget> USCADACircleWidget::RebuildWidget()
{
	MyCircle = SNew(SSCADAEllipse);

	// UWidget 没有原生 Tick，借 Slate 层的 Tick 轮询槽矩形变化
	TWeakObjectPtr<USCADACircleWidget> WeakThis(this);
	MyCircle->ExternalTickCallback = [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CheckSlotSync();
		}
	};

	// archetype 实例（无 Slate）也要能 Tick：靠核心 Ticker 兜底，保证两实例状态机同步收尾
	EnsureCoreTicker();

	return MyCircle.ToSharedRef();
}

void USCADACircleWidget::EnsureCoreTicker()
{
	if (CoreTickerHandle.IsValid())
	{
		return;
	}
	TWeakObjectPtr<USCADACircleWidget> WeakThis(this);
	CoreTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CheckSlotSync();
			return true;
		}
		return false;
	}));
}

void USCADACircleWidget::BeginDestroy()
{
	if (CoreTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CoreTickerHandle);
		CoreTickerHandle.Reset();
	}
	Super::BeginDestroy();
}

void USCADACircleWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 以圆心/半径为基准：槽矩形始终等于圆的外接正方形。
	// 反方向（设计器拖动/拉伸槽 → 回写圆心/半径）由 Tick 轮询 CheckSlotSync 闭环，
	// 因此这里无条件把槽对齐到几何数据，保证编译/重建后属性编辑不丢失。
	SyncSlotFromGeometry();

	if (MyCircle.IsValid())
	{
		PushGeometryToSlate();
		MyCircle->SetColor(BorderColor);
		MyCircle->SetThickness(Thickness);
		MyCircle->SetLineStyle(LineStyle);
		MyCircle->SetFillColor(FillColor);
		MyCircle->SetFillPattern(FillPattern);
		MyCircle->SetFlashEnabled(bFlashEnabled);
		MyCircle->SetFlashColor(FlashColor);
	}
}

void USCADACircleWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyCircle.Reset();
}

void USCADACircleWidget::PushGeometryToSlate()
{
	if (MyCircle.IsValid())
	{
		MyCircle->SetRadiusX(Radius);
		MyCircle->SetRadiusY(Radius);
		// 设计器拖动中槽不是正方形，圆心不一定在槽内 (R,R) 处：
		// 把画布坐标圆心换算成槽内局部坐标显式传入，绘制位置与数据严格一致（松手槽回正时无跳变）
		FVector2D SlotPos = FVector2D::ZeroVector;
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			SlotPos = CanvasSlot->GetPosition();
		}
		MyCircle->SetLocalCenter(Center - SlotPos);
	}
}

void USCADACircleWidget::SnapPropertiesToPixel()
{
	Center.X = FMath::RoundToFloat(Center.X);
	Center.Y = FMath::RoundToFloat(Center.Y);
	Radius = FMath::Max(FMath::RoundToFloat(Radius), 1.0f);
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 1.0f);
}

void USCADACircleWidget::SyncSlotFromGeometry()
{
	if (bSyncingGeometry)
	{
		return;
	}
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!CanvasSlot)
	{
		PushGeometryToSlate();
		return;
	}

	TGuardValue<bool> Guard(bSyncingGeometry, true);

	// 槽矩形 = 圆的外接正方形 (Center - R, 2R)
	const FVector2D Min(Center.X - Radius, Center.Y - Radius);
	const float Side = FMath::Max(Radius * 2.0f, 1.0f);

	CanvasSlot->SetAutoSize(false);
	CanvasSlot->SetPosition(Min);
	CanvasSlot->SetSize(FVector2D(Side, Side));
	CachedSlotPos = Min;
	CachedSlotSize = FVector2D(Side, Side);
	bSlotCacheValid = true;
	// 几何数据为真值：任何正向同步都复位交互簿记（符号基线/收尾标记/锚定状态）
	bPrevRawNegX = false;
	bPrevRawNegY = false;
	bPendingFinalize = false;
	bDragStartValid = false;
	bDragModeLocked = false;
	bCornerDrag = false;
	bLastAnchorValidX = false;
	bLastAnchorValidY = false;

	PushGeometryToSlate();
}

void USCADACircleWidget::ForceSyncFromSlotRect()
{
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		// archetype 实例（无 Slate）从这里首次触达，也要注册核心 Ticker
		EnsureCoreTicker();
		// 属性面板编辑槽的合法输入通道：授权一次映射（编译/后台重写不会走这里）
		bSlotRemapAuthorized = true;
		CheckSlotSync();
	}
}

void USCADACircleWidget::CheckSlotSync()
{
	if (bSyncingGeometry)
	{
		return;
	}

	// 设计器拖动/拉伸槽的处理（显示与存储分离：圆心/半径是唯一真值，槽只是操作手柄）。
	// 槽永远保持正向规范矩形（min 位置 + 正尺寸），布局不受负尺寸影响，绘制不跳。
	// 无状态绝对映射：边长按拖动类型拆分（随锚点一次锁定，无逐帧切换跳变）——
	// 拖角 = min(宽, 高)（小边，圆恒内切设计器矩形，槽不扩展）；拖边 = 被拖轴尺寸（槽未拖轴绕圆心扩展到直径）；平移 = 小边；
	// 圆心逐轴锚定（拖边 = 对边中心固定，拖角 = 对角固定，整体拖 = 随矩形平移），
	// 锚点在**一次拖动内锁定**（首个移动帧记录固定边/角的物理坐标），中途换向/翻过锚点自动镜像不丢锚；
	// 槽写回时任一轴小于圆直径则绕圆心扩展该轴（圆恒在插槽内，拖边外拉时两边一起变）；
	// 收尾才把槽对齐回外接正方形；翻转沿天然无感（no-op）。
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!CanvasSlot)
	{
		return;
	}

	const FVector2D RawPos = CanvasSlot->GetPosition();
	const FVector2D RawSize = CanvasSlot->GetSize();

	// 缓存未初始化（新实例首次 Tick）：只采纳当前槽矩形为基准，不做映射，
	// 否则默认 1x1 缓存会把健康槽矩形误判成外部缩放输入，把半径放大到爆炸。
	if (!bSlotCacheValid)
	{
		bSlotCacheValid = true;
		CachedSlotPos = RawPos;
		CachedSlotSize = RawSize;
		bPrevRawNegX = RawSize.X < 0.0;
		bPrevRawNegY = RawSize.Y < 0.0;
		// 编译/重建可能把 SynchronizeProperties 跑在槽挂上或槽位置恢复之前，
		// 那时算出的 LocalCenter 是错的并会一直留在 Slate 层（半径不受影响 → 大小对、位置突变）。
		// 槽就位后的首个 Tick 这里补一次推送，保证 LocalCenter 按真实槽位置换算。
		PushGeometryToSlate();
		return;
	}

	if (RawPos.Equals(CachedSlotPos) && RawSize.Equals(CachedSlotSize))
	{
		// 设计器无新输入。鼠标左键仍按下 = 拖动中停顿，不收尾（保留符号沿状态）；
		// 已松开 = 交互结束，收尾一次：取整、标脏、符号基线复位。
		if (bPendingFinalize && !FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
		{
			bPendingFinalize = false;
			bInteractionModified = false;
			bPrevRawNegX = false;
			bPrevRawNegY = false;
			bDragStartValid = false;
			bDragModeLocked = false;
			bCornerDrag = false;
			bLastAnchorValidX = false;
			bLastAnchorValidY = false;
			SnapPropertiesToPixel();
			SyncSlotFromGeometry();
#if WITH_EDITOR
			if (GIsEditor && !HasAnyFlags(RF_Transient))
			{
				MarkPackageDirty();
			}
#endif
		}
		return;
	}

	// 后台重写识别：鼠标未按下且未经属性面板授权（bSlotRemapAuthorized）的槽输入，
	// 不是用户输入——是设计器后台重写（编译重建后恢复几何、松手提交拖拽矩形等）。
	// 一律拒绝映射、以数据为真值对齐槽；否则拖边后编译时设计器重写的拖拽矩形会被误判成
	// "平移"交互（锚已复位 → 圆心取矩形中心、半径取小边 = 拖动前的值），数据被改回去。
	// 已知取舍：设计器里方向键微调控件（鼠标未按下、不走面板通知）也会被拒绝。
	const bool bLeftMouseDown = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	if (!bLeftMouseDown && !bSlotRemapAuthorized)
	{
		bPendingFinalize = false;
		bInteractionModified = false;
		bDragStartValid = false;
		bDragModeLocked = false;
		bCornerDrag = false;
		bLastAnchorValidX = false;
		bLastAnchorValidY = false;
		SyncSlotFromGeometry();
#if WITH_EDITOR
		if (GIsEditor && !HasAnyFlags(RF_Transient))
		{
			MarkPackageDirty();
		}
#endif
		return;
	}
	bSlotRemapAuthorized = false;

	// 设计器写入了新矩形（不变式：读到与缓存不同的值即外部输入）
#if WITH_EDITOR
	if (!bInteractionModified)
	{
		// 每次交互只进一次撤销事务
		bInteractionModified = true;
		if (GIsEditor && !HasAnyFlags(RF_Transient))
		{
			Modify();
		}
	}
#endif
	bPendingFinalize = true;

	// 规范化：min 位置 + 正尺寸；记录原始符号（圆镜像 = 自身，符号沿只做基线跟踪）
	FVector2D CPos = RawPos;
	FVector2D CSize = RawSize;
	const bool bNegX = CSize.X < 0.0;
	const bool bNegY = CSize.Y < 0.0;
	if (bNegX) { CPos.X += CSize.X; CSize.X = -CSize.X; }
	if (bNegY) { CPos.Y += CSize.Y; CSize.Y = -CSize.Y; }
	bPrevRawNegX = bNegX;
	bPrevRawNegY = bNegY;

	// 无状态绝对映射（圆专用方案，不用增量缩放——统一标量系数作用在从槽左上角起的偏移上，
	// 拖单边时另一轴偏移也被缩放，圆心会沿垂直方向漂移；增量累积还会放大浮点误差）。
	// 半径不下钳：尺寸穿过 ~0 的翻转过程中绝对映射天然恢复，收尾 SnapPropertiesToPixel 才钳 ≥1。

	// 拖动起点 = 首个输入帧的缓存矩形（交互前的槽矩形）；新拖动同时复位锚点滞后保持
	if (!bDragStartValid)
	{
		bDragStartValid = true;
		DragStartPos = CachedSlotPos;
		DragStartSize = CachedSlotSize;
		bLastAnchorValidX = false;
		bLastAnchorValidY = false;
	}

	// 锚定（WinCC 语义：拖边 = 对边中心固定，拖角 = 对角固定，整体拖 = 随矩形平移）。
	// 每轴独立、在**原始带符号空间对拖动起点**判定：当前矩形两个物理端点（RawPos / RawPos+RawSize）
	// 对照起点矩形两个端点——只有一端还对得上起点的任一端 → 对上的那端就是锚。
	// 原始空间判定天然翻转鲁棒：翻过锚边后规范化矩形的 min/max 会换侧（规范化判定会丢锚），
	// 但原始空间里设计器计算的不动端始终恒定（位置=起点±增量，符号随翻转变化）。
	// 两端都对得上（缓慢移动/原位翻转的歧义帧）→ 沿用上一帧锚点（滞后保持，防锚点跳变）；
	// 两端都对不上 → 无锚（平移）。
	const float StartX0 = (float)DragStartPos.X;
	const float StartX1 = (float)(DragStartPos.X + DragStartSize.X);
	const float StartY0 = (float)DragStartPos.Y;
	const float StartY1 = (float)(DragStartPos.Y + DragStartSize.Y);
	const float CurXA = (float)RawPos.X;
	const float CurXB = (float)(RawPos.X + RawSize.X);
	const float CurYA = (float)RawPos.Y;
	const float CurYB = (float)(RawPos.Y + RawSize.Y);
	auto PickAnchor = [](float A, float B, float S0, float S1, float& LastAnchor, bool& bLastValid, double& OutAnchor)
	{
		const bool bMA = FMath::IsNearlyEqual(A, S0, 0.5f) || FMath::IsNearlyEqual(A, S1, 0.5f);
		const bool bMB = FMath::IsNearlyEqual(B, S0, 0.5f) || FMath::IsNearlyEqual(B, S1, 0.5f);
		bool bAnchored = false;
		float Anchor = 0.0f;
		if (bMA != bMB)
		{
			Anchor = bMA ? A : B;
			bAnchored = true;
		}
		else if (bMA && bMB && bLastValid)
		{
			if (FMath::IsNearlyEqual(LastAnchor, A, 0.5f)) { Anchor = A; bAnchored = true; }
			else if (FMath::IsNearlyEqual(LastAnchor, B, 0.5f)) { Anchor = B; bAnchored = true; }
		}
		LastAnchor = Anchor;
		bLastValid = bAnchored;
		OutAnchor = (double)Anchor;
		return bAnchored;
	};
	double AnchorX = 0.0;
	double AnchorY = 0.0;
	const bool bAnchorX = PickAnchor(CurXA, CurXB, StartX0, StartX1, LastAnchorX, bLastAnchorValidX, AnchorX);
	const bool bAnchorY = PickAnchor(CurYA, CurYB, StartY0, StartY1, LastAnchorY, bLastAnchorValidY, AnchorY);

	// 拖动类型**延迟锁定**（仅决定边长规则与槽扩展，锚判定不锁）：
	// 两轴都有锚 → 拖角；单轴有锚且该轴尺寸已变 >3px、另一轴 <1px → 拖边；平移永不锁定。
	// 不能在首个输入帧就锁：拖角起步帧可能只有一轴动了，会误锁成拖边（另一轴永远无锚、圆跟着滑）。
	if (!bDragModeLocked)
	{
		const float SizeDx = FMath::Abs((float)(CSize.X - DragStartSize.X));
		const float SizeDy = FMath::Abs((float)(CSize.Y - DragStartSize.Y));
		if (bAnchorX && bAnchorY)
		{
			bDragModeLocked = true;
			bCornerDrag = true;
		}
		else if (bAnchorX && SizeDx > 3.0f && SizeDy < 1.0f)
		{
			bDragModeLocked = true;
			bCornerDrag = false;
		}
		else if (bAnchorY && SizeDy > 3.0f && SizeDx < 1.0f)
		{
			bDragModeLocked = true;
			bCornerDrag = false;
		}
	}
	const bool bEdgeLocked = bDragModeLocked && !bCornerDrag;

	// 边长：拖边锁定 = 被拖轴尺寸（外拉放大、内推缩小都有效）；
	// 其余（拖角/平移/未锁定）= min(宽, 高)（小边：圆恒内切于设计器矩形，无需槽扩展）。
	if (bEdgeLocked)
	{
		Radius = (float)((bAnchorX ? CSize.X : CSize.Y) * 0.5);
	}
	else
	{
		Radius = (float)(FMath::Min(CSize.X, CSize.Y) * 0.5);
	}

	if (bAnchorX)
	{
		Center.X = AnchorX + ((CPos.X + CSize.X * 0.5 >= AnchorX) ? Radius : -Radius);
	}
	else
	{
		Center.X = CPos.X + CSize.X * 0.5;
	}
	if (bAnchorY)
	{
		Center.Y = AnchorY + ((CPos.Y + CSize.Y * 0.5 >= AnchorY) ? Radius : -Radius);
	}
	else
	{
		Center.Y = CPos.Y + CSize.Y * 0.5;
	}

	// 槽写回：拖边锁定后未拖动轴若小于圆直径则绕圆心扩展该轴（圆恒在插槽内，两边一起变）；
	// 拖角/平移/未锁定原样写回设计器矩形（小边规则下圆恒内切，无需扩展）。缓存记写回后的值：
	// 停顿时不误判新输入，下一帧设计器覆盖后仍按"与缓存不同 = 新输入"处理。
	FVector2D OutPos = CPos;
	FVector2D OutSize = CSize;
	if (bEdgeLocked)
	{
		const float Diameter = Radius * 2.0f;
		if (OutSize.X < Diameter) { OutPos.X = (float)(Center.X - Radius); OutSize.X = Diameter; }
		if (OutSize.Y < Diameter) { OutPos.Y = (float)(Center.Y - Radius); OutSize.Y = Diameter; }
	}
	{
		TGuardValue<bool> Guard(bSyncingGeometry, true);
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetPosition(OutPos);
		CanvasSlot->SetSize(OutSize);
	}
	CachedSlotPos = OutPos;
	CachedSlotSize = OutSize;
	PushGeometryToSlate();
}

void USCADACircleWidget::SetCenter(FVector2D InCenter)
{
	Center = InCenter;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADACircleWidget::SetRadius(float InRadius)
{
	Radius = InRadius;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADACircleWidget::SetBorderColor(FLinearColor InColor)
{
	BorderColor = InColor;
	if (MyCircle.IsValid())
	{
		MyCircle->SetColor(BorderColor);
	}
}

void USCADACircleWidget::SetThickness(float InThickness)
{
	Thickness = InThickness;
	SnapPropertiesToPixel();
	if (MyCircle.IsValid())
	{
		MyCircle->SetThickness(Thickness);
	}
}

void USCADACircleWidget::SetLineStyle(ESCADALineStyle InStyle)
{
	LineStyle = InStyle;
	if (MyCircle.IsValid())
	{
		MyCircle->SetLineStyle(LineStyle);
	}
}

void USCADACircleWidget::SetFillColor(FLinearColor InColor)
{
	FillColor = InColor;
	if (MyCircle.IsValid())
	{
		MyCircle->SetFillColor(FillColor);
	}
}

void USCADACircleWidget::SetFillPattern(ESCADAFillPattern InPattern)
{
	FillPattern = InPattern;
	if (MyCircle.IsValid())
	{
		MyCircle->SetFillPattern(FillPattern);
	}
}

void USCADACircleWidget::SetFlashEnabled(bool bInEnabled)
{
	bFlashEnabled = bInEnabled;
	if (MyCircle.IsValid())
	{
		MyCircle->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADACircleWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColor = InColor;
	if (MyCircle.IsValid())
	{
		MyCircle->SetFlashColor(FlashColor);
	}
}

#if WITH_EDITOR
void USCADACircleWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 编辑器属性面板输入完成后立即取整，面板显示值即为最终绘制值
	SnapPropertiesToPixel();

	// 圆心/半径被编辑 → 槽矩形（X/Y/宽/高）同步更新（须在 Super 之前，
	// Super 链上的 SynchronizeProperties 以槽为准回写，槽先更新才不会把新值冲掉）。
	const FName MemberPropName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropName == GET_MEMBER_NAME_CHECKED(USCADACircleWidget, Center)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADACircleWidget, Radius))
	{
		SyncSlotFromGeometry();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

const FText USCADACircleWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
