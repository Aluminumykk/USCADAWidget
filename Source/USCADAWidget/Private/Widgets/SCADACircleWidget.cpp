// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADACircleWidget.h"

#include "Slate/SSCADAEllipse.h"
#include "Components/CanvasPanelSlot.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADACircleWidget::RebuildWidget()
{
	MyCircle = SNew(SSCADAEllipse);

	// 挂接 Slate Tick 回调 + 注册核心 Ticker 兜底（编辑器/非游戏世界守卫在基类内部）：
	// 必须让所有实例都能 Tick——UMG 设计器里同一控件存在 WidgetTree(archetype，无 Slate)
	// 与 WidgetTree_0(预览，有 Slate) 两个实例，设计器拖动会把槽矩形镜像写进两棵树，
	// 两边各自映射；没有 Slate 的 archetype 若只能靠 Slate 层回调 Tick，就永远走不到
	// 收尾（取整/复位/槽回正），两边状态（缓存/拖动起点/锁定模式）逐渐分叉，
	// 编译时用 archetype 数据重建预览 → 显示回退/跳变。
	SetupSlotSyncCallbacks(MyCircle.Get());

	return MyCircle.ToSharedRef();
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
		MyCircle->SetFillGradient(FillGradient);
		MyCircle->SetFlashEnabled(bFlashEnabled);
		MyCircle->SetFlashColor(FlashColor);
		MyCircle->SetFlashInverted(bFlashInverted);
	}

	// 属性同步后补一次绑定检查：绑定先于本次同步应用（初始化时序）时在此完成注册；
	// 无任何委托绑定（设计器实例/未绑定运行实例）时是零成本 no-op。
	EnsurePropertyBindingTicker();
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
		// 设计器拖动中槽不是正方形，圆心不一定在槽内 (R,R) 处：
		// 把画布坐标圆心换算成槽内局部坐标显式传入，绘制位置与数据严格一致（松手槽回正时无跳变）
		FVector2D SlotPos = FVector2D::ZeroVector;
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			SlotPos = CanvasSlot->GetPosition();
		}
		// 批量推送：半径 + 显式本地中心一次写入，Slate 层只重采样一次
		MyCircle->SetGeometry(Radius, Radius, Center - SlotPos);
	}
}

void USCADACircleWidget::SnapPropertiesToPixel()
{
	Center.X = FMath::RoundToFloat(Center.X);
	Center.Y = FMath::RoundToFloat(Center.Y);
	Radius = FMath::Max(FMath::RoundToFloat(Radius), 1.0f);
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 0.0f);
}

void USCADACircleWidget::GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	OutMin = Center - FVector2D(Radius, Radius);
	OutMax = Center + FVector2D(Radius, Radius);
}

void USCADACircleWidget::TranslateGeometryBy(const FVector2D& Delta)
{
	Center += Delta;
}

void USCADACircleWidget::ScaleGeometryBy(float Scale, const FVector2D& PostOffset, bool bScaleThickness)
{
	Center = Center * Scale + PostOffset;
	Radius *= Scale;
	if (bScaleThickness)
	{
		Thickness *= Scale;
	}
	// 正多边形子类继承本实现即正确：数据模型同为 Center+Radius，Sides/StartAngle 不受缩放影响
}

void USCADACircleWidget::ResetInteractionState()
{
	bDragStartValid = false;
	bDragModeLocked = false;
	bCornerDrag = false;
	bLastAnchorValidX = false;
	bLastAnchorValidY = false;
}

void USCADACircleWidget::OnSlotCacheAdopted()
{
	PushGeometryToSlate();
}

void USCADACircleWidget::MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart)
{
	// 无状态绝对映射（圆专用方案，不用增量缩放——统一标量系数作用在从槽左上角起的偏移上，
	// 拖单边时另一轴偏移也被缩放，圆心会沿垂直方向漂移；增量累积还会放大浮点误差）。
	// 边长按拖动类型拆分（随锚点一次锁定，无逐帧切换跳变）——
	// 拖角 = min(宽, 高)（小边，圆恒内切设计器矩形，槽不扩展）；拖边 = 被拖轴尺寸（槽未拖轴绕圆心扩展到直径）；平移 = 小边；
	// 圆心逐轴锚定（拖边 = 对边中心固定，拖角 = 对角固定，整体拖 = 随矩形平移），
	// 锚点在**一次拖动内锁定**（首个移动帧记录固定边/角的物理坐标），中途换向/翻过锚点自动镜像不丢锚；
	// 槽写回时任一轴小于圆直径则绕圆心扩展该轴（圆恒在插槽内，拖边外拉时两边一起变）；
	// 翻转沿天然无感（no-op）。半径不下钳：尺寸穿过 ~0 的翻转过程中绝对映射天然恢复，
	// 收尾 SnapPropertiesToPixel 才钳 ≥1。

	// 规范化：min 位置 + 正尺寸；记录原始符号（圆镜像 = 自身，符号沿只做基线跟踪）
	FVector2D CPos = RawPos;
	FVector2D CSize = RawSize;
	const bool bNegX = CSize.X < 0.0;
	const bool bNegY = CSize.Y < 0.0;
	if (bNegX) { CPos.X += CSize.X; CSize.X = -CSize.X; }
	if (bNegY) { CPos.Y += CSize.Y; CSize.Y = -CSize.Y; }
	bPrevRawNegX = bNegX;
	bPrevRawNegY = bNegY;

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
	CenterDelegate.Unbind();
	Center = InCenter;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADACircleWidget::SetRadius(float InRadius)
{
	RadiusDelegate.Unbind();
	Radius = InRadius;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADACircleWidget::SetBorderColor(FLinearColor InColor)
{
	BorderColorDelegate.Unbind();
	BorderColor = InColor;
	if (MyCircle.IsValid())
	{
		MyCircle->SetColor(BorderColor);
	}
}

void USCADACircleWidget::SetThickness(float InThickness)
{
	ThicknessDelegate.Unbind();
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
	FillColorDelegate.Unbind();
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
	bFlashEnabledDelegate.Unbind();
	bFlashEnabled = bInEnabled;
	if (MyCircle.IsValid())
	{
		MyCircle->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADACircleWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColorDelegate.Unbind();
	FlashColor = InColor;
	if (MyCircle.IsValid())
	{
		MyCircle->SetFlashColor(FlashColor);
	}
}

void USCADACircleWidget::SetFlashInverted(bool bInInverted)
{
	bFlashInvertedDelegate.Unbind();
	bFlashInverted = bInInverted;
	if (MyCircle.IsValid())
	{
		MyCircle->SetFlashInverted(bFlashInverted);
	}
}

// ———— UMG 属性绑定轮询（“拉”→“推”桥）————
// 引擎语义：手动 Set* 解除绑定（各 Set* 开头的 *Delegate.Unbind()，照 UTextBlock::SetText 先例）；
// 委托取值走每帧轮询而非引擎的一次性 Execute：绑定委托是“拉”模型、本控件数据是“推”模型，
// 中间桥 = 下面的核心 Ticker（注册时机见 EnsurePropertyBindingTicker；设计器实例委托
// 天然未绑定不会注册，运行时 PIE/打包正常工作，无 GIsEditor 守卫）。
// 绑定轮询对几何族走正向对齐（外接正方形），与 Set* 语义一致；设计器交互态的
// 特殊槽形态由 MapSlotToGeometry 独占负责，两条路径互不干扰（bSyncingGeometry 防互触发）。
// 边框色在 Slate 层的接口名是 SetColor（数据层属性名是 BorderColor，对照多边形族的既有映射）。

bool USCADACircleWidget::HasBoundPropertyDelegates() const
{
	return CenterDelegate.IsBound() || RadiusDelegate.IsBound()
		|| BorderColorDelegate.IsBound() || ThicknessDelegate.IsBound()
		|| FillColorDelegate.IsBound()
		|| bFlashEnabledDelegate.IsBound() || FlashColorDelegate.IsBound()
		|| bFlashInvertedDelegate.IsBound();
}

void USCADACircleWidget::EnsurePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid() || !HasBoundPropertyDelegates())
	{
		return;
	}
	TWeakObjectPtr<USCADACircleWidget> WeakThis(this);
	PropertyBindingTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->PollPropertyBindings();
			return true;
		}
		return false;
	}));
}

void USCADACircleWidget::RemovePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PropertyBindingTickerHandle);
		PropertyBindingTickerHandle.Reset();
	}
}

void USCADACircleWidget::PollPropertyBindings()
{
	// 运行中委托全部解除（蓝图 Set* / 外部手动 Unbind 等）→ 自注销，
	// 等下一次绑定经 OnBindingChanged/SynchronizeProperties 路径重新注册。
	if (!HasBoundPropertyDelegates())
	{
		RemovePropertyBindingTicker();
		return;
	}

	// 几何族（Center/Radius）：写成员后统一走数据层取整/钳制（半径 ≥1 在
	// SnapPropertiesToPixel 里做）+ 槽正向对齐（SyncSlotFromGeometry 末尾含
	// PushGeometryToSlate 的 SetGeometry+LocalCenter；与 Set* 接口同一数据流向，
	// 但绝不调 Set*——那会 Unbind）。
	bool bGeometryDirty = false;
	if (CenterDelegate.IsBound())
	{
		const FVector2D NewValue = CenterDelegate.Execute();
		if (!NewValue.Equals(Center))
		{
			Center = NewValue;
			bGeometryDirty = true;
		}
	}
	if (RadiusDelegate.IsBound())
	{
		const float NewValue = RadiusDelegate.Execute();
		if (!FMath::IsNearlyEqual(NewValue, Radius))
		{
			Radius = NewValue;
			bGeometryDirty = true;
		}
	}
	if (bGeometryDirty)
	{
		// 含 Thickness 的取整（幂等）；随后正向对齐：外接正方形 → 槽 → Slate
		SnapPropertiesToPixel();
		SyncSlotFromGeometry();
	}

	// 非几何属性：变化才写成员并推对应 Slate Set（与 SynchronizeProperties 的单属性子集一致）。
	// float 先按数据层规则取整/钳制再比较（边框宽度允许 0：下限 0，与本控件
	// SnapPropertiesToPixel 规则一致）。
	if (ThicknessDelegate.IsBound())
	{
		const float NewValue = FMath::Max(FMath::RoundToFloat(ThicknessDelegate.Execute()), 0.0f);
		if (!FMath::IsNearlyEqual(NewValue, Thickness))
		{
			Thickness = NewValue;
			if (MyCircle.IsValid())
			{
				MyCircle->SetThickness(Thickness);
			}
		}
	}
	if (BorderColorDelegate.IsBound())
	{
		const FLinearColor NewValue = BorderColorDelegate.Execute();
		if (NewValue != BorderColor)
		{
			BorderColor = NewValue;
			if (MyCircle.IsValid())
			{
				MyCircle->SetColor(BorderColor);
			}
		}
	}
	if (FillColorDelegate.IsBound())
	{
		const FLinearColor NewValue = FillColorDelegate.Execute();
		if (NewValue != FillColor)
		{
			FillColor = NewValue;
			if (MyCircle.IsValid())
			{
				MyCircle->SetFillColor(FillColor);
			}
		}
	}
	if (bFlashEnabledDelegate.IsBound())
	{
		const bool bNewValue = bFlashEnabledDelegate.Execute();
		if (bNewValue != bFlashEnabled)
		{
			bFlashEnabled = bNewValue;
			if (MyCircle.IsValid())
			{
				MyCircle->SetFlashEnabled(bFlashEnabled);
			}
		}
	}
	if (FlashColorDelegate.IsBound())
	{
		const FLinearColor NewValue = FlashColorDelegate.Execute();
		if (NewValue != FlashColor)
		{
			FlashColor = NewValue;
			if (MyCircle.IsValid())
			{
				MyCircle->SetFlashColor(FlashColor);
			}
		}
	}
	if (bFlashInvertedDelegate.IsBound())
	{
		const bool bNewValue = bFlashInvertedDelegate.Execute();
		if (bNewValue != bFlashInverted)
		{
			bFlashInverted = bNewValue;
			if (MyCircle.IsValid())
			{
				MyCircle->SetFlashInverted(bFlashInverted);
			}
		}
	}
}

void USCADACircleWidget::OnBindingChanged(const FName& Property)
{
	Super::OnBindingChanged(Property);

	// 绑定生效通道：UWidget::AddBinding 成功（原生 binder/属性路径）后引擎回调此处。
	// “Create Binding”（绑定到 BP 生成函数）路径不回调此处，由其后的 SynchronizeProperties
	// 或已注册 Ticker 覆盖；此处按 IsBound 判定，未绑定即零成本 no-op。
	EnsurePropertyBindingTicker();
}

void USCADACircleWidget::BeginDestroy()
{
	// 绑定轮询 Ticker 兜底注销（正常路径：weak 失效时 Ticker 自清理）。
	// 基类 BeginDestroy 负责注销槽同步核心 Ticker。
	RemovePropertyBindingTicker();
	Super::BeginDestroy();
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
