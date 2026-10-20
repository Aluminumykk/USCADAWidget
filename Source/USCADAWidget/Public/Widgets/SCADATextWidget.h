// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Binding/SCADABindingTypes.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "SCADATextWidget.generated.h"

/**
 * SCADA 文字控件：直接继承 UE 内置 UTextBlock（字体/字号/对齐/换行等全部沿用），
 * 只增加闪烁功能（对照 WinCC 文本对象的"闪烁"属性）。
 * 全局统一 1 秒换相（FPlatformTime::Seconds() 墙钟驱动，所有图元天然同步）：
 * 闪烁相位时文字颜色切到 FlashColor；FlashColor 透明度 0 = 闪烁相位隐藏。不做选中态。
 *
 * UMG 属性绑定（细节面板 Bind 下拉）：自有 3 个闪烁属性按图元控件同一模板开放
 * （伴随委托 = 属性 C++ 名 + "Delegate"，Set* 开头 Unbind，独立属性绑定轮询 Ticker
 * 每帧 Execute() 取值写成员；见 PollPropertyBindings）。Text/ColorAndOpacity/Visibility
 * 等继承自 UTextBlock/UWidget 的属性引擎自带绑定，无需处理。
 * 绑定轮询 Ticker 与闪烁 Ticker 是两个独立句柄，各自按需注册/注销，互不混用。
 */
UCLASS(meta = (DisplayName = "SCADA Text", Category = "SCADA"))
class USCADAWIDGET_API USCADATextWidget : public UTextBlock
{
	GENERATED_BODY()

public:
	/** 闪烁开关：开启后文字颜色按全局 1 秒相位在正常颜色与闪烁色之间切换 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash")
	bool bFlashEnabled = false;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate，bool 含 b 前缀） */
	UPROPERTY()
	FGetSCADABool bFlashEnabledDelegate;

	/** 闪烁色：闪烁相位时文字显示为该颜色；透明度 0 = 闪烁相位隐藏 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	FLinearColor FlashColor = FLinearColor::Red;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADALinearColor FlashColorDelegate;

	/** 相位翻转（翻转组）：开启后与全局定时器取色相反——正常相位显示闪烁色、
	 *  闪烁相位显示正常色。两组图元由同一时刻换相、显示色互反（一组亮时另一组灭），
	 *  用于状态互闪。本控件默认属于正常组。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	bool bFlashInverted = false;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate，bool 含 b 前缀） */
	UPROPERTY()
	FGetSCADABool bFlashInvertedDelegate;

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashInverted(bool bInInverted);

	virtual void BeginDestroy() override;

protected:
	virtual void SynchronizeProperties() override;

	/** 绑定生效/变更（引擎在 AddBinding 成功后回调，仅 Slate 已创建时）：按需注册属性绑定轮询 Ticker */
	virtual void OnBindingChanged(const FName& Property) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

private:
	/** 闪烁驱动：UTextBlock 无 Tick，用核心 Ticker 每帧检查全局相位。
	 *  按需注册：仅 bFlashEnabled 为 true 时注册，关闭即注销（BeginDestroy 注销作兜底）。 */
	void EnsureFlashTicker();
	void RemoveFlashTicker();
	void TickFlash();

	FTSTicker::FDelegateHandle FlashTickerHandle;
	/** 上次实际推送给 Slate 的颜色（只有变化才 SetColorAndOpacity，避免每帧触发文字重排） */
	FLinearColor LastAppliedColor = FLinearColor(ForceInit);
	bool bHasAppliedColor = false;

	// —— UMG 属性绑定轮询（“拉”→“推”桥，图元控件同模板）——
	// 与闪烁 Ticker 是独立句柄：本 Ticker 只在任一 Bind 伴随委托 IsBound 时注册，
	// 每帧 Execute() 写成员（颜色/相位成员由 TickFlash 每帧读取生效；开关变化联动
	// 闪烁 Ticker 按需注册/注销）。设计器实例委托天然未绑定，不会注册；
	// BeginDestroy 兜底注销。
	bool HasBoundPropertyDelegates() const;
	void EnsurePropertyBindingTicker();
	void RemovePropertyBindingTicker();
	void PollPropertyBindings();

	FTSTicker::FDelegateHandle PropertyBindingTickerHandle;
};
