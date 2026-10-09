// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "SCADATextWidget.generated.h"

/**
 * SCADA 文字控件：直接继承 UE 内置 UTextBlock（字体/字号/对齐/换行等全部沿用），
 * 只增加闪烁功能（对照 WinCC 文本对象的"闪烁"属性）。
 * 全局统一 1 秒换相（FPlatformTime::Seconds() 墙钟驱动，所有图元天然同步）：
 * 闪烁相位时文字颜色切到 FlashColor；FlashColor 透明度 0 = 闪烁相位隐藏。不做选中态。
 */
UCLASS(meta = (DisplayName = "SCADA Text", Category = "SCADA"))
class USCADAWIDGET_API USCADATextWidget : public UTextBlock
{
	GENERATED_BODY()

public:
	/** 闪烁开关：开启后文字颜色按全局 1 秒相位在正常颜色与闪烁色之间切换 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash")
	bool bFlashEnabled = false;

	/** 闪烁色：闪烁相位时文字显示为该颜色；透明度 0 = 闪烁相位隐藏 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	FLinearColor FlashColor = FLinearColor::Red;

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashColor(FLinearColor InColor);

	virtual void BeginDestroy() override;

protected:
	virtual void SynchronizeProperties() override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

private:
	/** 闪烁驱动：UTextBlock 无 Tick，用核心 Ticker 每帧检查全局相位（惰性注册，BeginDestroy 注销） */
	void EnsureFlashTicker();
	void TickFlash();

	FTSTicker::FDelegateHandle FlashTickerHandle;
	/** 上次实际推送给 Slate 的颜色（只有变化才 SetColorAndOpacity，避免每帧触发文字重排） */
	FLinearColor LastAppliedColor = FLinearColor(ForceInit);
	bool bHasAppliedColor = false;
};
