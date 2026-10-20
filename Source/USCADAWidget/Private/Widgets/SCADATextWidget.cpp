// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADATextWidget.h"

#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

void USCADATextWidget::BeginDestroy()
{
	// 两个独立 Ticker 都兜底注销（正常路径各自 weak 失效时自清理）
	RemoveFlashTicker();
	RemovePropertyBindingTicker();
	Super::BeginDestroy();
}

void USCADATextWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 闪烁按需注册：仅开启闪烁的文本才占用每帧 Ticker；关闭时若已注册则注销。
	// 关闭分支无需立即修色：Super 刚把本色推给了 Slate。
	if (bFlashEnabled)
	{
		EnsureFlashTicker();
	}
	else
	{
		RemoveFlashTicker();
	}
	// 属性被重建/编辑后强制下一帧重推颜色（Super 刚把本色推给了 Slate）
	bHasAppliedColor = false;

	// 属性同步后补一次绑定检查：绑定先于本次同步应用（初始化时序）时在此完成注册；
	// 无任何委托绑定（设计器实例/未绑定运行实例）时是零成本 no-op。
	EnsurePropertyBindingTicker();
}

void USCADATextWidget::EnsureFlashTicker()
{
	if (FlashTickerHandle.IsValid())
	{
		return;
	}
	TWeakObjectPtr<USCADATextWidget> WeakThis(this);
	FlashTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->TickFlash();
			return true;
		}
		return false;
	}));
}

void USCADATextWidget::RemoveFlashTicker()
{
	if (FlashTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FlashTickerHandle);
		FlashTickerHandle.Reset();
	}
}

void USCADATextWidget::TickFlash()
{
	if (!MyTextBlock.IsValid())
	{
		return;
	}

	// 全局统一定时器：所有闪烁图元使用同一个墙钟相位，1 秒换相，天然同步。
	// 翻转组（bFlashInverted）与正常组取色相反：正常相位显示闪烁色、闪烁相位显示正常色。
	const bool bPhaseNormal = (FMath::FloorToInt64(FPlatformTime::Seconds()) % 2) == 0;
	const bool bShowFlash = bFlashEnabled && (bPhaseNormal == bFlashInverted);
	const FLinearColor EffectiveColor = bShowFlash ? FlashColor : GetColorAndOpacity().GetSpecifiedColor();
	if (!bHasAppliedColor || EffectiveColor != LastAppliedColor)
	{
		LastAppliedColor = EffectiveColor;
		bHasAppliedColor = true;
		MyTextBlock->SetColorAndOpacity(FSlateColor(EffectiveColor));
	}
}

void USCADATextWidget::SetFlashEnabled(bool bInEnabled)
{
	bFlashEnabledDelegate.Unbind();
	bFlashEnabled = bInEnabled;
	if (bFlashEnabled)
	{
		EnsureFlashTicker();
	}
	else
	{
		RemoveFlashTicker();
		// 关闭时立即恢复本色（否则文字会停在闪烁色上，直到下次属性同步）
		TickFlash();
	}
}

void USCADATextWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColorDelegate.Unbind();
	FlashColor = InColor;
}

void USCADATextWidget::SetFlashInverted(bool bInInverted)
{
	bFlashInvertedDelegate.Unbind();
	bFlashInverted = bInInverted;
}

// ———— UMG 属性绑定轮询（“拉”→“推”桥，图元控件同模板）————
// 引擎语义：手动 Set* 解除绑定（各 Set* 开头的 *Delegate.Unbind()，照 UTextBlock::SetText 先例）；
// 委托取值走每帧轮询（注册时机见 EnsurePropertyBindingTicker；设计器实例委托天然未绑定
// 不会注册，运行时 PIE/打包正常工作，无 GIsEditor 守卫）。
// 本控件特殊点：闪烁颜色/相位生效路径是 TickFlash 每帧读成员计算（EffectiveColor 变化才推
// Slate），所以 FlashColor/bFlashInverted 绑定只需写成员；bFlashEnabled 开关变化额外联动
// 闪烁 Ticker 按需注册/注销（与 SetFlashEnabled 同分支结构，保证绑定切开关即时生效）。
// Text/ColorAndOpacity 等引擎属性自带绑定，不经本机制。

bool USCADATextWidget::HasBoundPropertyDelegates() const
{
	return bFlashEnabledDelegate.IsBound() || FlashColorDelegate.IsBound()
		|| bFlashInvertedDelegate.IsBound();
}

void USCADATextWidget::EnsurePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid() || !HasBoundPropertyDelegates())
	{
		return;
	}
	TWeakObjectPtr<USCADATextWidget> WeakThis(this);
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

void USCADATextWidget::RemovePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PropertyBindingTickerHandle);
		PropertyBindingTickerHandle.Reset();
	}
}

void USCADATextWidget::PollPropertyBindings()
{
	// 运行中委托全部解除（蓝图 Set* / 外部手动 Unbind 等）→ 自注销，
	// 等下一次绑定经 OnBindingChanged/SynchronizeProperties 路径重新注册。
	if (!HasBoundPropertyDelegates())
	{
		RemovePropertyBindingTicker();
		return;
	}

	if (bFlashEnabledDelegate.IsBound())
	{
		const bool bNewValue = bFlashEnabledDelegate.Execute();
		if (bNewValue != bFlashEnabled)
		{
			bFlashEnabled = bNewValue;
			// 联动点：与 SetFlashEnabled 同结构——开启时按需注册闪烁 Ticker；
			// 关闭时注销并立即恢复本色（否则文字停在闪烁色上）。
			if (bFlashEnabled)
			{
				EnsureFlashTicker();
			}
			else
			{
				RemoveFlashTicker();
				TickFlash();
			}
		}
	}
	// 颜色/相位成员：只写成员即可——TickFlash 每帧读成员计算 EffectiveColor，
	// 变化才 SetColorAndOpacity（闪烁关闭时无可见色可切，无副作用）。
	if (FlashColorDelegate.IsBound())
	{
		const FLinearColor NewValue = FlashColorDelegate.Execute();
		if (NewValue != FlashColor)
		{
			FlashColor = NewValue;
		}
	}
	if (bFlashInvertedDelegate.IsBound())
	{
		const bool bNewValue = bFlashInvertedDelegate.Execute();
		if (bNewValue != bFlashInverted)
		{
			bFlashInverted = bNewValue;
		}
	}
}

void USCADATextWidget::OnBindingChanged(const FName& Property)
{
	Super::OnBindingChanged(Property);

	// 绑定生效通道：UWidget::AddBinding 成功（原生 binder/属性路径）后引擎回调此处。
	// “Create Binding”（绑定到 BP 生成函数）路径不回调此处，由其后的 SynchronizeProperties
	// 或已注册 Ticker 覆盖；此处按 IsBound 判定，未绑定即零成本 no-op。
	// 注意：继承自 UTextBlock 的属性（Text/ColorAndOpacity）的绑定也会走到这里，
	// 引擎机制自持（OnBindingChanged 内引擎已处理），本处只按需注册自有属性的轮询 Ticker。
	EnsurePropertyBindingTicker();
}

#if WITH_EDITOR
const FText USCADATextWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
