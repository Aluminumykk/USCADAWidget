// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADATextWidget.h"

#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

void USCADATextWidget::BeginDestroy()
{
	if (FlashTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FlashTickerHandle);
		FlashTickerHandle.Reset();
	}
	Super::BeginDestroy();
}

void USCADATextWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	EnsureFlashTicker();
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

void USCADATextWidget::TickFlash()
{
	if (!MyTextBlock.IsValid())
	{
		return;
	}

	// 全局统一定时器：所有闪烁图元使用同一个墙钟相位，1 秒换相，天然同步
	const bool bPhaseNormal = (FMath::FloorToInt64(FPlatformTime::Seconds()) % 2) == 0;
	const FLinearColor EffectiveColor = (bFlashEnabled && !bPhaseNormal) ? FlashColor : ColorAndOpacity.GetSpecifiedColor();
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
}

void USCADATextWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColor = InColor;
}

#if WITH_EDITOR
const FText USCADATextWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
