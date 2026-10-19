// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/CanvasPanel.h"
#include "SCADACanvasWidget.generated.h"

class SSCADACanvas;

/**
 * SCADA 画布面板：画面配置的容器基座，子控件以绝对设计坐标定位、可在设计器中自由拖动。
 * 支持可选背景网格与缩放系数。
 *
 * **必须继承 UCanvasPanel（而非 UPanelWidget）**：设计器的一批机制对父面板做
 * `Cast<UCanvasPanel>(Parent)` 硬转换——`UCanvasPanelSlot::SaveBaseLayout/RebaseLayout`
 * （本体拖动 MoveWidgets 写位置全靠它，转换失败则静默 no-op = 画布内任何控件都拖不动）、
 * `GetGeometryForSlot`、锚点 gizmo（FCanvasSlotExtension）等。继承 UPanelWidget 会让这些
 * 全部失效且无报错。LockToPanelOnDrag（拖动=原位移动而非换父级）也由基类提供。
 *
 * 与基类的差异：Slate 层换成 SSCADACanvas（网格 + Zoom 整体缩放），槽类换成
 * USCADACanvasSlot（槽变更时把几何回写到图元数据）。
 */
UCLASS(meta = (DisplayName = "SCADA Canvas", Category = "SCADA"))
class USCADAWIDGET_API USCADACanvasWidget : public UCanvasPanel
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

	/** 缩放系数：以左上角为原点对画布全部子内容整体渲染缩放（含网格密度）。
	 *  跨平台/分辨率适配只调这里，图元内部坐标不变；线宽已按缩放自动补偿。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Canvas", meta = (ClampMin = "0.1"))
	float Zoom = 1.0f;

	// AddChildToCanvas 直接用基类版本：内部走虚函数 GetSlotClass() → USCADACanvasSlot，行为一致

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetShowGrid(bool bInShowGrid);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetGridSize(float InGridSize);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetGridColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetZoom(float InZoom);

protected:
	//~ UPanelWidget
	virtual UClass* GetSlotClass() const override;
	//~ End of UPanelWidget

	//~ UWidget
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	//~ End of UWidget
	// OnSlotAdded/OnSlotRemoved/ReleaseSlateResources 与基类实现一致，直接继承

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

private:
	/** 基类 MyCanvas（TSharedPtr<SConstraintCanvas>）由 RebuildWidget 创建为 SSCADACanvas，
	 *  取类型化指针（网格/Zoom 接口） */
	TSharedPtr<SSCADACanvas> GetSCADACanvas() const;
};
