// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADARectangleWidget.h"

#include "Slate/SSCADARectangle.h"
#include "Components/CanvasPanelSlot.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADARectangleWidget::RebuildWidget()
{
	MyRectangle = SNew(SSCADARectangle);

	// 挂接 Slate Tick 回调 + 注册核心 Ticker 兜底（编辑器/非游戏世界守卫在基类内部）
	SetupSlotSyncCallbacks(MyRectangle.Get());

	return MyRectangle.ToSharedRef();
}

void USCADARectangleWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 以位置/尺寸为基准：槽矩形始终等于矩形本身。
	// 反方向（设计器拖动/拉伸槽 → 回写位置/尺寸）由 Tick 轮询 CheckSlotSync 闭环，
	// 因此这里无条件把槽对齐到几何数据，保证编译/重建后属性编辑不丢失。
	SyncSlotFromGeometry();

	if (MyRectangle.IsValid())
	{
		PushGeometryToSlate();
		MyRectangle->SetColor(BorderColor);
		MyRectangle->SetThickness(Thickness);
		MyRectangle->SetLineStyle(LineStyle);
		MyRectangle->SetFillColor(FillColor);
		MyRectangle->SetFillPattern(FillPattern);
		MyRectangle->SetFillGradient(FillGradient);
		MyRectangle->SetFlashEnabled(bFlashEnabled);
		MyRectangle->SetFlashColor(FlashColor);
		MyRectangle->SetFlashInverted(bFlashInverted);
	}

	// 属性同步后补一次绑定检查：绑定先于本次同步应用（初始化时序）时在此完成注册；
	// 无任何委托绑定（设计器实例/未绑定运行实例）时是零成本 no-op。
	EnsurePropertyBindingTicker();
}

void USCADARectangleWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyRectangle.Reset();
}

void USCADARectangleWidget::PushGeometryToSlate()
{
	if (MyRectangle.IsValid())
	{
		// 批量推送：宽/高/圆角一次写入，Slate 层只重采样一次
		MyRectangle->SetGeometry((float)Size.X, (float)Size.Y, CornerRadiusX, CornerRadiusY);
	}
}

void USCADARectangleWidget::SnapPropertiesToPixel()
{
	Position.X = FMath::RoundToFloat(Position.X);
	Position.Y = FMath::RoundToFloat(Position.Y);
	Size.X = FMath::Max(FMath::RoundToFloat(Size.X), 1.0);
	Size.Y = FMath::Max(FMath::RoundToFloat(Size.Y), 1.0);
	const float MaxR = (float)FMath::Min(Size.X, Size.Y) * 0.5f;
	CornerRadiusX = FMath::Clamp(FMath::RoundToFloat(CornerRadiusX), 0.0f, MaxR);
	CornerRadiusY = FMath::Clamp(FMath::RoundToFloat(CornerRadiusY), 0.0f, MaxR);
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 0.0f);
}

void USCADARectangleWidget::GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	OutMin = Position;
	OutMax = Position + Size;
}

void USCADARectangleWidget::TranslateGeometryBy(const FVector2D& Delta)
{
	Position += Delta;
}

void USCADARectangleWidget::MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart)
{
	// 规范化：min 位置 + 正尺寸；记录原始符号（矩形镜像 = 自身，符号沿只做基线跟踪）
	FVector2D CPos = RawPos;
	FVector2D CSize = RawSize;
	const bool bNegX = CSize.X < 0.0;
	const bool bNegY = CSize.Y < 0.0;
	if (bNegX) { CPos.X += CSize.X; CSize.X = -CSize.X; }
	if (bNegY) { CPos.Y += CSize.Y; CSize.Y = -CSize.Y; }
	bPrevRawNegX = bNegX;
	bPrevRawNegY = bNegY;

	// 规范空间增量映射（正尺寸，无符号问题），除零保护钳到 1。
	// 注意：交互过程中尺寸不能钳下限——拖过对边翻转时槽尺寸会穿过 ~0，
	// 钳位会把尺寸钉死、之后增量缩放从钳位值起算，比例永久失真、无法恢复。
	// 下限钳位只在做收尾的 SnapPropertiesToPixel 里进行。
	FVector2D OldSize = CachedSlotSize;
	if (FMath::IsNearlyZero((float)OldSize.X)) { OldSize.X = 1.0; }
	if (FMath::IsNearlyZero((float)OldSize.Y)) { OldSize.Y = 1.0; }
	const FVector2D Scale(CSize.X / OldSize.X, CSize.Y / OldSize.Y);
	Position = CPos + (Position - CachedSlotPos) * Scale;
	Size = Size * Scale;

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

void USCADARectangleWidget::SetPosition(FVector2D InPosition)
{
	PositionDelegate.Unbind();
	Position = InPosition;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADARectangleWidget::SetSize(FVector2D InSize)
{
	SizeDelegate.Unbind();
	Size = InSize;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADARectangleWidget::SetCornerRadiusX(float InRadiusX)
{
	CornerRadiusXDelegate.Unbind();
	CornerRadiusX = InRadiusX;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADARectangleWidget::SetCornerRadiusY(float InRadiusY)
{
	CornerRadiusYDelegate.Unbind();
	CornerRadiusY = InRadiusY;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADARectangleWidget::SetBorderColor(FLinearColor InColor)
{
	BorderColorDelegate.Unbind();
	BorderColor = InColor;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetColor(BorderColor);
	}
}

void USCADARectangleWidget::SetThickness(float InThickness)
{
	ThicknessDelegate.Unbind();
	Thickness = InThickness;
	SnapPropertiesToPixel();
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetThickness(Thickness);
	}
}

void USCADARectangleWidget::SetLineStyle(ESCADALineStyle InStyle)
{
	LineStyle = InStyle;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetLineStyle(LineStyle);
	}
}

void USCADARectangleWidget::SetFillColor(FLinearColor InColor)
{
	FillColorDelegate.Unbind();
	FillColor = InColor;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFillColor(FillColor);
	}
}

void USCADARectangleWidget::SetFillPattern(ESCADAFillPattern InPattern)
{
	FillPattern = InPattern;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFillPattern(FillPattern);
	}
}

void USCADARectangleWidget::SetFlashEnabled(bool bInEnabled)
{
	bFlashEnabledDelegate.Unbind();
	bFlashEnabled = bInEnabled;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADARectangleWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColorDelegate.Unbind();
	FlashColor = InColor;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFlashColor(FlashColor);
	}
}

void USCADARectangleWidget::SetFlashInverted(bool bInInverted)
{
	bFlashInvertedDelegate.Unbind();
	bFlashInverted = bInInverted;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFlashInverted(bFlashInverted);
	}
}

// ———— UMG 属性绑定轮询（“拉”→“推”桥）————
// 引擎语义：手动 Set* 解除绑定（各 Set* 开头的 *Delegate.Unbind()，照 UTextBlock::SetText 先例）；
// 委托取值走每帧轮询而非引擎的一次性 Execute：绑定委托是“拉”模型、本控件数据是“推”模型，
// 中间桥 = 下面的核心 Ticker（注册时机见 EnsurePropertyBindingTicker；设计器实例委托
// 天然未绑定不会注册，运行时 PIE/打包正常工作，无 GIsEditor 守卫）。
// 边框色在 Slate 层的接口名是 SetColor（数据层属性名是 BorderColor，对照多边形族的既有映射）。

bool USCADARectangleWidget::HasBoundPropertyDelegates() const
{
	return PositionDelegate.IsBound() || SizeDelegate.IsBound()
		|| CornerRadiusXDelegate.IsBound() || CornerRadiusYDelegate.IsBound()
		|| BorderColorDelegate.IsBound() || ThicknessDelegate.IsBound()
		|| FillColorDelegate.IsBound()
		|| bFlashEnabledDelegate.IsBound() || FlashColorDelegate.IsBound()
		|| bFlashInvertedDelegate.IsBound();
}

void USCADARectangleWidget::EnsurePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid() || !HasBoundPropertyDelegates())
	{
		return;
	}
	TWeakObjectPtr<USCADARectangleWidget> WeakThis(this);
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

void USCADARectangleWidget::RemovePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PropertyBindingTickerHandle);
		PropertyBindingTickerHandle.Reset();
	}
}

void USCADARectangleWidget::PollPropertyBindings()
{
	// 运行中委托全部解除（蓝图 Set* / 外部手动 Unbind 等）→ 自注销，
	// 等下一次绑定经 OnBindingChanged/SynchronizeProperties 路径重新注册。
	if (!HasBoundPropertyDelegates())
	{
		RemovePropertyBindingTicker();
		return;
	}

	// 几何族（Position/Size/CornerRadiusX/CornerRadiusY）：写成员后统一走数据层取整/钳制
	// （尺寸 ≥1、圆角 [0, min(宽,高)/2] 依赖尺寸，由 SnapPropertiesToPixel 统一处理）
	// + 槽正向对齐（SyncSlotFromGeometry 末尾含 PushGeometryToSlate；与 Set* 接口同一
	// 数据流向，但绝不调 Set*——那会 Unbind）。
	bool bGeometryDirty = false;
	if (PositionDelegate.IsBound())
	{
		const FVector2D NewValue = PositionDelegate.Execute();
		if (!NewValue.Equals(Position))
		{
			Position = NewValue;
			bGeometryDirty = true;
		}
	}
	if (SizeDelegate.IsBound())
	{
		const FVector2D NewValue = SizeDelegate.Execute();
		if (!NewValue.Equals(Size))
		{
			Size = NewValue;
			bGeometryDirty = true;
		}
	}
	if (CornerRadiusXDelegate.IsBound())
	{
		const float NewValue = CornerRadiusXDelegate.Execute();
		if (!FMath::IsNearlyEqual(NewValue, CornerRadiusX))
		{
			CornerRadiusX = NewValue;
			bGeometryDirty = true;
		}
	}
	if (CornerRadiusYDelegate.IsBound())
	{
		const float NewValue = CornerRadiusYDelegate.Execute();
		if (!FMath::IsNearlyEqual(NewValue, CornerRadiusY))
		{
			CornerRadiusY = NewValue;
			bGeometryDirty = true;
		}
	}
	if (bGeometryDirty)
	{
		// 含 Thickness 的取整（幂等）；随后正向对齐：矩形 → 槽 → Slate
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
			if (MyRectangle.IsValid())
			{
				MyRectangle->SetThickness(Thickness);
			}
		}
	}
	if (BorderColorDelegate.IsBound())
	{
		const FLinearColor NewValue = BorderColorDelegate.Execute();
		if (NewValue != BorderColor)
		{
			BorderColor = NewValue;
			if (MyRectangle.IsValid())
			{
				MyRectangle->SetColor(BorderColor);
			}
		}
	}
	if (FillColorDelegate.IsBound())
	{
		const FLinearColor NewValue = FillColorDelegate.Execute();
		if (NewValue != FillColor)
		{
			FillColor = NewValue;
			if (MyRectangle.IsValid())
			{
				MyRectangle->SetFillColor(FillColor);
			}
		}
	}
	if (bFlashEnabledDelegate.IsBound())
	{
		const bool bNewValue = bFlashEnabledDelegate.Execute();
		if (bNewValue != bFlashEnabled)
		{
			bFlashEnabled = bNewValue;
			if (MyRectangle.IsValid())
			{
				MyRectangle->SetFlashEnabled(bFlashEnabled);
			}
		}
	}
	if (FlashColorDelegate.IsBound())
	{
		const FLinearColor NewValue = FlashColorDelegate.Execute();
		if (NewValue != FlashColor)
		{
			FlashColor = NewValue;
			if (MyRectangle.IsValid())
			{
				MyRectangle->SetFlashColor(FlashColor);
			}
		}
	}
	if (bFlashInvertedDelegate.IsBound())
	{
		const bool bNewValue = bFlashInvertedDelegate.Execute();
		if (bNewValue != bFlashInverted)
		{
			bFlashInverted = bNewValue;
			if (MyRectangle.IsValid())
			{
				MyRectangle->SetFlashInverted(bFlashInverted);
			}
		}
	}
}

void USCADARectangleWidget::OnBindingChanged(const FName& Property)
{
	Super::OnBindingChanged(Property);

	// 绑定生效通道：UWidget::AddBinding 成功（原生 binder/属性路径）后引擎回调此处。
	// “Create Binding”（绑定到 BP 生成函数）路径不回调此处，由其后的 SynchronizeProperties
	// 或已注册 Ticker 覆盖；此处按 IsBound 判定，未绑定即零成本 no-op。
	EnsurePropertyBindingTicker();
}

void USCADARectangleWidget::BeginDestroy()
{
	// 绑定轮询 Ticker 兜底注销（正常路径：weak 失效时 Ticker 自清理）
	RemovePropertyBindingTicker();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void USCADARectangleWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 编辑器属性面板输入完成后立即取整，面板显示值即为最终绘制值
	SnapPropertiesToPixel();

	// 位置/尺寸/圆角被编辑 → 槽矩形（X/Y/宽/高）同步更新（须在 Super 之前，
	// Super 链上的 SynchronizeProperties 以槽为准回写，槽先更新才不会把新值冲掉）。
	const FName MemberPropName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropName == GET_MEMBER_NAME_CHECKED(USCADARectangleWidget, Position)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADARectangleWidget, Size)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADARectangleWidget, CornerRadiusX)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADARectangleWidget, CornerRadiusY))
	{
		SyncSlotFromGeometry();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

const FText USCADARectangleWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
