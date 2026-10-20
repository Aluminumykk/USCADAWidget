// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADATextWidget.h"

#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

void USCADATextWidget::BeginDestroy()
{
	RemoveFlashTicker();
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
	FlashColor = InColor;
}

void USCADATextWidget::SetFlashInverted(bool bInInverted)
{
	bFlashInverted = bInInverted;
}

#if WITH_EDITOR
const FText USCADATextWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
