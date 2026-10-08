// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PanelWidget.h"
#include "SCADACanvasWidget.generated.h"

class SSCADACanvas;
class UCanvasPanelSlot;

/**
 * SCADA 画布面板：画面配置的容器基座，与 UMG Canvas Panel 同款槽（UCanvasPanelSlot），
 * 子控件以绝对设计坐标定位、可在设计器中自由拖动。
 * 支持可选背景网格与缩放系数。
 * 后续扩展：图元吸附网格、属性绑定。
 */
UCLASS(meta = (DisplayName = "SCADA Canvas", Category = "SCADA"))
class USCADAWIDGET_API USCADACanvasWidget : public UPanelWidget
{
	GENERATED_BODY()

public:
	/** 是否绘制背景网格 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Canvas")
	bool bShowGrid = true;

	/** 网格间距（未缩放时的像素） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Canvas", meta = (ClampMin = "2.0"))
	float GridSize = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Canvas")
	FLinearColor GridColor = FLinearColor(1.f, 1.f, 1.f, 0.08f);

	/** 缩放系数（预留，影响网格密度） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Canvas", meta = (ClampMin = "0.1"))
	float Zoom = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	UCanvasPanelSlot* AddChildToCanvas(UWidget* Content);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetShowGrid(bool bInShowGrid);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetGridSize(float InGridSize);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetGridColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetZoom(float InZoom);

	//~ UPanelWidget
	virtual void OnSlotAdded(UPanelSlot* InSlot) override;
	virtual void OnSlotRemoved(UPanelSlot* InSlot) override;
	//~ End of UPanelWidget

protected:
	//~ UPanelWidget
	virtual UClass* GetSlotClass() const override;
	//~ End of UPanelWidget

	//~ UWidget
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	//~ End of UWidget

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	TSharedPtr<SSCADACanvas> MyCanvas;
};
