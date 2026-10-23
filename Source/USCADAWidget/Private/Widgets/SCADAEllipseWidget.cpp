// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADAEllipseWidget.h"

#include "Slate/SSCADAEllipse.h"
#include "Components/CanvasPanelSlot.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADAEllipseWidget::RebuildWidget()
{
	MyEllipse = SNew(SSCADAEllipse);

	// 挂接 Slate Tick 回调 + 注册核心 Ticker 兜底（编辑器/非游戏世界守卫在基类内部）
	SetupSlotSyncCallbacks(MyEllipse.Get());

	return MyEllipse.ToSharedRef();
}

void USCADAEllipseWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 以中心/半径为基准：槽矩形始终跟随椭圆包围盒。
	// 反方向（设计器拖动/拉伸槽 → 回写中心/半径）由 Tick 轮询 CheckSlotSync 闭环，
	// 因此这里无条件把槽对齐到几何数据，保证编译/重建后属性编辑不丢失。
	SyncSlotFromGeometry();

	if (MyEllipse.IsValid())
	{
		PushGeometryToSlate();
		MyEllipse->SetColor(BorderColor);
		MyEllipse->SetThickness(Thickness);
		MyEllipse->SetLineStyle(LineStyle);
		MyEllipse->SetFillColor(FillColor);
		MyEllipse->SetFillPattern(FillPattern);
		MyEllipse->SetFillGradient(FillGradient);
		MyEllipse->SetFlashEnabled(bFlashEnabled);
		MyEllipse->SetFlashColor(FlashColor);
		MyEllipse->SetFlashInverted(bFlashInverted);
	}

	// 属性同步后补一次绑定检查：绑定先于本次同步应用（初始化时序）时在此完成注册；
	// 无任何委托绑定（设计器实例/未绑定运行实例）时是零成本 no-op。
	EnsurePropertyBindingTicker();
}

void USCADAEllipseWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyEllipse.Reset();
}

void USCADAEllipseWidget::PushGeometryToSlate()
{
	if (MyEllipse.IsValid())
	{
		// 椭圆的槽矩形恒等于包围盒，本地中心恒为 (RadiusX, RadiusY)（即原单 Setter 链
		// !bCenterExplicit 中心跟随分支的终态值），批量接口一次写入、只重采样一次
		MyEllipse->SetGeometry(RadiusX, RadiusY, FVector2D(RadiusX, RadiusY));
	}
}

void USCADAEllipseWidget::SnapPropertiesToPixel()
{
	Center.X = FMath::RoundToFloat(Center.X);
	Center.Y = FMath::RoundToFloat(Center.Y);
	RadiusX = FMath::Max(FMath::RoundToFloat(RadiusX), 1.0f);
	RadiusY = FMath::Max(FMath::RoundToFloat(RadiusY), 1.0f);
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 0.0f);
}

void USCADAEllipseWidget::GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	OutMin = Center - FVector2D(RadiusX, RadiusY);
	OutMax = Center + FVector2D(RadiusX, RadiusY);
}

void USCADAEllipseWidget::TranslateGeometryBy(const FVector2D& Delta)
{
	Center += Delta;
}

void USCADAEllipseWidget::ScaleGeometryBy(float Scale, const FVector2D& PostOffset, bool bScaleThickness)
{
	Center = Center * Scale + PostOffset;
	RadiusX *= Scale;
	RadiusY *= Scale;
	if (bScaleThickness)
	{
		Thickness *= Scale;
	}
}

void USCADAEllipseWidget::MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart)
{
	// 规范化：min 位置 + 正尺寸；记录原始符号（椭圆镜像 = 自身，符号沿只做基线跟踪）
	FVector2D CPos = RawPos;
	FVector2D CSize = RawSize;
	const bool bNegX = CSize.X < 0.0;
	const bool bNegY = CSize.Y < 0.0;
	if (bNegX) { CPos.X += CSize.X; CSize.X = -CSize.X; }
	if (bNegY) { CPos.Y += CSize.Y; CSize.Y = -CSize.Y; }
	bPrevRawNegX = bNegX;
	bPrevRawNegY = bNegY;

	// 规范空间增量映射（正尺寸，无符号问题），除零保护钳到 1。
	// 注意：交互过程中半径不能钳下限（≥1）——拖过对边翻转时槽尺寸会穿过 ~0，
	// 钳位会把半径钉死在 1，之后继续拖动时增量缩放从钳位值起算，比例永久失真、无法恢复。
	// Scale 恒为正，半径天然保持为正；下限钳位只在做收尾的 SnapPropertiesToPixel 里进行。
	FVector2D OldSize = CachedSlotSize;
	if (FMath::IsNearlyZero((float)OldSize.X)) { OldSize.X = 1.0; }
	if (FMath::IsNearlyZero((float)OldSize.Y)) { OldSize.Y = 1.0; }
	const FVector2D Scale(CSize.X / OldSize.X, CSize.Y / OldSize.Y);
	Center = CPos + (Center - CachedSlotPos) * Scale;
	RadiusX = RadiusX * (float)Scale.X;
	RadiusY = RadiusY * (float)Scale.Y;

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

void USCADAEllipseWidget::SetCenter(FVector2D InCenter)
{
	CenterDelegate.Unbind();
	Center = InCenter;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADAEllipseWidget::SetRadiusX(float InRadiusX)
{
	RadiusXDelegate.Unbind();
	RadiusX = InRadiusX;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADAEllipseWidget::SetRadiusY(float InRadiusY)
{
	RadiusYDelegate.Unbind();
	RadiusY = InRadiusY;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADAEllipseWidget::SetBorderColor(FLinearColor InColor)
{
	BorderColorDelegate.Unbind();
	BorderColor = InColor;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetColor(BorderColor);
	}
}

void USCADAEllipseWidget::SetThickness(float InThickness)
{
	ThicknessDelegate.Unbind();
	Thickness = InThickness;
	SnapPropertiesToPixel();
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetThickness(Thickness);
	}
}

void USCADAEllipseWidget::SetLineStyle(ESCADALineStyle InStyle)
{
	LineStyle = InStyle;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetLineStyle(LineStyle);
	}
}

void USCADAEllipseWidget::SetFillColor(FLinearColor InColor)
{
	FillColorDelegate.Unbind();
	FillColor = InColor;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFillColor(FillColor);
	}
}

void USCADAEllipseWidget::SetFillPattern(ESCADAFillPattern InPattern)
{
	FillPattern = InPattern;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFillPattern(FillPattern);
	}
}

void USCADAEllipseWidget::SetFlashEnabled(bool bInEnabled)
{
	bFlashEnabledDelegate.Unbind();
	bFlashEnabled = bInEnabled;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADAEllipseWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColorDelegate.Unbind();
	FlashColor = InColor;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFlashColor(FlashColor);
	}
}

void USCADAEllipseWidget::SetFlashInverted(bool bInInverted)
{
	bFlashInvertedDelegate.Unbind();
	bFlashInverted = bInInverted;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFlashInverted(bFlashInverted);
	}
}

// ———— UMG 属性绑定轮询（“拉”→“推”桥）————
// 引擎语义：手动 Set* 解除绑定（各 Set* 开头的 *Delegate.Unbind()，照 UTextBlock::SetText 先例）；
// 委托取值走每帧轮询而非引擎的一次性 Execute：绑定委托是“拉”模型、本控件数据是“推”模型，
// 中间桥 = 下面的核心 Ticker（注册时机见 EnsurePropertyBindingTicker；设计器实例委托
// 天然未绑定不会注册，运行时 PIE/打包正常工作，无 GIsEditor 守卫）。
// 边框色在 Slate 层的接口名是 SetColor（数据层属性名是 BorderColor，对照多边形族的既有映射）。

bool USCADAEllipseWidget::HasBoundPropertyDelegates() const
{
	return CenterDelegate.IsBound() || RadiusXDelegate.IsBound() || RadiusYDelegate.IsBound()
		|| BorderColorDelegate.IsBound() || ThicknessDelegate.IsBound()
		|| FillColorDelegate.IsBound()
		|| bFlashEnabledDelegate.IsBound() || FlashColorDelegate.IsBound()
		|| bFlashInvertedDelegate.IsBound();
}

void USCADAEllipseWidget::EnsurePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid() || !HasBoundPropertyDelegates())
	{
		return;
	}
	TWeakObjectPtr<USCADAEllipseWidget> WeakThis(this);
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

void USCADAEllipseWidget::RemovePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PropertyBindingTickerHandle);
		PropertyBindingTickerHandle.Reset();
	}
}

void USCADAEllipseWidget::PollPropertyBindings()
{
	// 运行中委托全部解除（蓝图 Set* / 外部手动 Unbind 等）→ 自注销，
	// 等下一次绑定经 OnBindingChanged/SynchronizeProperties 路径重新注册。
	if (!HasBoundPropertyDelegates())
	{
		RemovePropertyBindingTicker();
		return;
	}

	// 几何族（Center/RadiusX/RadiusY）：写成员后统一走数据层取整/钳制（半径 ≥1 在
	// SnapPropertiesToPixel 里做）+ 槽正向对齐（SyncSlotFromGeometry 末尾含
	// PushGeometryToSlate；与 Set* 接口同一数据流向，但绝不调 Set*——那会 Unbind）。
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
	if (RadiusXDelegate.IsBound())
	{
		const float NewValue = RadiusXDelegate.Execute();
		if (!FMath::IsNearlyEqual(NewValue, RadiusX))
		{
			RadiusX = NewValue;
			bGeometryDirty = true;
		}
	}
	if (RadiusYDelegate.IsBound())
	{
		const float NewValue = RadiusYDelegate.Execute();
		if (!FMath::IsNearlyEqual(NewValue, RadiusY))
		{
			RadiusY = NewValue;
			bGeometryDirty = true;
		}
	}
	if (bGeometryDirty)
	{
		// 含 Thickness 的取整（幂等）；随后正向对齐：包围盒 → 槽 → Slate
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
			if (MyEllipse.IsValid())
			{
				MyEllipse->SetThickness(Thickness);
			}
		}
	}
	if (BorderColorDelegate.IsBound())
	{
		const FLinearColor NewValue = BorderColorDelegate.Execute();
		if (NewValue != BorderColor)
		{
			BorderColor = NewValue;
			if (MyEllipse.IsValid())
			{
				MyEllipse->SetColor(BorderColor);
			}
		}
	}
	if (FillColorDelegate.IsBound())
	{
		const FLinearColor NewValue = FillColorDelegate.Execute();
		if (NewValue != FillColor)
		{
			FillColor = NewValue;
			if (MyEllipse.IsValid())
			{
				MyEllipse->SetFillColor(FillColor);
			}
		}
	}
	if (bFlashEnabledDelegate.IsBound())
	{
		const bool bNewValue = bFlashEnabledDelegate.Execute();
		if (bNewValue != bFlashEnabled)
		{
			bFlashEnabled = bNewValue;
			if (MyEllipse.IsValid())
			{
				MyEllipse->SetFlashEnabled(bFlashEnabled);
			}
		}
	}
	if (FlashColorDelegate.IsBound())
	{
		const FLinearColor NewValue = FlashColorDelegate.Execute();
		if (NewValue != FlashColor)
		{
			FlashColor = NewValue;
			if (MyEllipse.IsValid())
			{
				MyEllipse->SetFlashColor(FlashColor);
			}
		}
	}
	if (bFlashInvertedDelegate.IsBound())
	{
		const bool bNewValue = bFlashInvertedDelegate.Execute();
		if (bNewValue != bFlashInverted)
		{
			bFlashInverted = bNewValue;
			if (MyEllipse.IsValid())
			{
				MyEllipse->SetFlashInverted(bFlashInverted);
			}
		}
	}
}

void USCADAEllipseWidget::OnBindingChanged(const FName& Property)
{
	Super::OnBindingChanged(Property);

	// 绑定生效通道：UWidget::AddBinding 成功（原生 binder/属性路径）后引擎回调此处。
	// “Create Binding”（绑定到 BP 生成函数）路径不回调此处，由其后的 SynchronizeProperties
	// 或已注册 Ticker 覆盖；此处按 IsBound 判定，未绑定即零成本 no-op。
	EnsurePropertyBindingTicker();
}

void USCADAEllipseWidget::BeginDestroy()
{
	// 绑定轮询 Ticker 兜底注销（正常路径：weak 失效时 Ticker 自清理）
	RemovePropertyBindingTicker();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void USCADAEllipseWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 编辑器属性面板输入完成后立即取整，面板显示值即为最终绘制值
	SnapPropertiesToPixel();

	// 中心/半径被编辑 → 槽矩形（X/Y/宽/高）同步更新（须在 Super 之前，
	// Super 链上的 SynchronizeProperties 以槽为准回写，槽先更新才不会把新值冲掉）。
	const FName MemberPropName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropName == GET_MEMBER_NAME_CHECKED(USCADAEllipseWidget, Center)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADAEllipseWidget, RadiusX)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADAEllipseWidget, RadiusY))
	{
		SyncSlotFromGeometry();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

const FText USCADAEllipseWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
