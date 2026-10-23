// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCADAWidgetTypes.h"
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
	 *  跨平台/分辨率适配只调这里，图元内部坐标不变；线宽已按缩放自动补偿。
	 *  注意：ScaleMode ≠ None 时本属性被忽略（缩放由 CanvasSize × 实际尺寸自动计算）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Canvas", meta = (ClampMin = "0.1"))
	float Zoom = 1.0f;

	/** 画布设计尺寸：ScaleMode ≠ None 时的缩放基准。设计时按此坐标系画图（如 1920×1080），
	 *  运行时无论窗口实际多大，0,0→CanvasSize 都映射到可视区域（Fit 等比留边 / Stretch 拉伸铺满）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Canvas", meta = (ClampMin = "1.0", Delta = "1.0", LinearDeltaSensitivity = "1"))
	FVector2D CanvasSize = FVector2D(1920.0, 1080.0);

	/** 缩放模式（分辨率自适应）：None = 手动 Zoom（默认，旧行为）；Fit = 等比完整显示（可能留边）；
	 *  Fill = 等比填满（可能裁切）；Stretch = 非等比铺满（设计区域恒等于可视区域，比例可能变形）。
	 *  非 None 时忽略 Zoom。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Canvas")
	ESCADACanvasScaleMode ScaleMode = ESCADACanvasScaleMode::None;

	// AddChildToCanvas 直接用基类版本：内部走虚函数 GetSlotClass() → USCADACanvasSlot，行为一致

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetShowGrid(bool bInShowGrid);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetGridSize(float InGridSize);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetGridColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetZoom(float InZoom);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetCanvasSize(FVector2D InSize);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Canvas")
	void SetScaleMode(ESCADACanvasScaleMode InMode);

	/** 当前实际生效的缩放系数（逐轴）：None = Zoom；Fit/Fill/Stretch = 按 CanvasSize ×
	 *  实际分配尺寸自动计算。未排布过时返回 Zoom。坐标换算（鼠标→设计坐标）应使用本值。 */
	UFUNCTION(BlueprintPure, Category = "SCADA|Canvas")
	FVector2D GetEffectiveCanvasScale() const;

	/** 适配内容大小：把 CanvasSize 收缩到直接子控件内容并集包围盒的尺寸（四周接壤、无留白），
	 *  并把全部子控件整体平移使内容包围盒左上角落到 (0,0)。无子控件时什么都不做。
	 *  包围盒为**视觉**包围盒：计入各子控件 RenderTransform（Translation/Angle/Scale/Shear +
	 *  RenderTransformPivot）——恒等变换直接用基准矩形，否则四角绕 Pivot 变换取 AABB；
	 *  平移归零只动几何数据/槽位置，RenderTransform 相对控件自身、数据平移后视觉自然跟随。
	 *  图元子控件经 TranslateGeometryAndSync 正向通道平移（几何+槽+缓存一起动），
	 *  普通控件（Text、嵌套画布等）直接写槽位置（无槽同步状态机，安全）。
	 *  限定"根画布"场景：嵌套画布拟合后内容会归到该画布自己的左上角，
	 *  但它在外层画布里的槽矩形不会自动补偿，外层布局需自行调整。
	 *  非反射方法：由编辑器设计器扩展 FSCADACanvasFitExtension 的"适配内容"按钮调用。 */
	void FitCanvasToContent();

	/** 内容适配画布（"适配内容"的反向操作）：CanvasSize 保持不变，把画布内全部直接子控件
	 *  整体缩放+平移，使内容（视觉）包围盒**等比完整显示**在画布内并居中——
	 *  k = min(CanvasSize.X/内容宽, CanvasSize.Y/内容高)，留边 = (CanvasSize - k*内容尺寸)/2。
	 *  无子控件、或内容两轴均退化（宽和高都 <1）时什么都不做；单轴退化（纯水平/垂直线）
	 *  时该轴不参与 k 计算、内容退化轴居中。
	 *  图元经 ScaleGeometryAndSync 缩放（几何数据 + 线宽/圆角等厚度类一起乘 k，视觉是原内容
	 *  的等比放大版）；普通控件槽位置/尺寸缩放；带 RenderTransform 的子控件其
	 *  Translation（父坐标系量）同步乘 k，Angle/Scale/Pivot 不动；嵌套画布 ScaleMode=None 时
	 *  Zoom×=k（≠None 时内容随槽自适应无需处理）；Text 控件字号 Font.Size 同步缩放（取整 ≥1）。
	 *  同样限定根画布场景（嵌套画布外层槽位不补偿）。非反射方法：由 FSCADACanvasFitExtension
	 *  的"内容适配画布"按钮调用。 */
	void FitContentToCanvas();

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

	/** "适配内容/内容适配画布"共用的内容（视觉）包围盒汇总：图元取几何数据包围盒、
	 *  普通控件取槽矩形，再按各自 RenderTransform 折算为视觉 AABB（恒等快路径）。
	 *  返回 false = 无子控件或全部槽不可得，调用方应直接什么都不做 */
	bool ComputeVisualContentBounds(FVector2D& OutMin, FVector2D& OutMax) const;
};
