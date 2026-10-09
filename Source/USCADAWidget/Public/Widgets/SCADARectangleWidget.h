// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "SCADATypes.h"
#include "SCADARectangleWidget.generated.h"

class SSCADARectangle;

/**
 * SCADA 矩形控件（对应 WinCC 的“矩形”对象，支持圆角）。
 * Position（左上角）/Size 使用**画布设计坐标**（父容器为 SCADA Canvas 时即以画布左上角为原点），
 * CornerRadiusX/Y 为 WinCC 的“圆角宽度/圆角高度”（椭圆角弧两轴，0 = 直角）。
 * 由 SSCADARectangle（继承 SSCADAPolygon）Slate 层绘制：
 * 实心/透明填充 + 闭合边框（实线/虚线/点线等线型，采样周点列复用多边形全套管线）。无箭头/端点样式。
 * 控件的槽矩形（X/Y/宽/高）始终等于矩形本身，双向同步：
 * 修改位置/尺寸 → 槽矩形自动更新；在设计器中拖动/拉伸控件 → 位置/尺寸自动平移/缩放。
 * 矩形在自身内镜像 = 自身，拖过对边翻转天然无感（no-op）。圆角半径不随拉伸缩放，仅属性面板编辑。
 * 输入时位置/尺寸/圆角/线宽取整到整数像素。
 */
UCLASS(meta = (DisplayName = "SCADA Rectangle", Category = "SCADA"))
class USCADAWIDGET_API USCADARectangleWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** 左上角位置（画布设计坐标） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Rectangle")
	FVector2D Position = FVector2D(0.0, 0.0);

	/** 尺寸（宽/高，像素，≥1） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Rectangle")
	FVector2D Size = FVector2D(100.0, 60.0);

	/** 圆角宽度（角弧 X 半径，像素，0 = 直角，≤ min(宽,高)/2） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Rectangle", meta = (ClampMin = "0.0"))
	float CornerRadiusX = 0.0f;

	/** 圆角高度（角弧 Y 半径，像素，0 = 直角，≤ min(宽,高)/2） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Rectangle", meta = (ClampMin = "0.0"))
	float CornerRadiusY = 0.0f;

	/** 边框颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Rectangle")
	FLinearColor BorderColor = FLinearColor(156.0f / 255.0f, 154.0f / 255.0f, 165.0f / 255.0f, 1.0f);

	/** 边框宽度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Rectangle", meta = (ClampMin = "1.0"))
	float Thickness = 1.0f;

	/** 边框线型（实线/虚线/点/点划线/双点划线） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Rectangle")
	ESCADALineStyle LineStyle = ESCADALineStyle::Solid;

	/** 填充颜色（背景色） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Rectangle")
	FLinearColor FillColor = FLinearColor(241.0f / 255.0f, 241.0f / 255.0f, 242.0f / 255.0f, 1.0f);

	/** 填充图案：实心 / 透明（不画填充） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Rectangle")
	ESCADAFillPattern FillPattern = ESCADAFillPattern::Solid;

	/** 闪烁：开启后每 1 秒在正常色与闪烁色之间切换（全局所有图元同步）。
	 *  闪烁时边框和填充都切到 FlashColor；FlashColor 透明度设为 0 即为“时隐时现”。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash")
	bool bFlashEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	FLinearColor FlashColor = FLinearColor::Red;

	UFUNCTION(BlueprintCallable, Category = "SCADA|Rectangle")
	void SetPosition(FVector2D InPosition);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Rectangle")
	void SetSize(FVector2D InSize);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Rectangle")
	void SetCornerRadiusX(float InRadiusX);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Rectangle")
	void SetCornerRadiusY(float InRadiusY);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Rectangle")
	void SetBorderColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Rectangle")
	void SetThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Rectangle")
	void SetLineStyle(ESCADALineStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Rectangle")
	void SetFillColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Rectangle")
	void SetFillPattern(ESCADAFillPattern InPattern);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashColor(FLinearColor InColor);

	/** 槽矩形被外部修改（设计器拖动/编辑槽属性）后由槽调用：以槽为准回写位置/尺寸并标脏资产 */
	void ForceSyncFromSlotRect();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual const FText GetPaletteCategory() override;
#endif

private:
	/** 把尺寸/圆角同步给 Slate 层（位置由槽位置决定，本地矩形恒为 (0,0,Size)） */
	void PushGeometryToSlate();

	/** 数据层像素对齐：位置/尺寸/圆角/线宽取整到就近整数（尺寸/线宽最小 1，圆角 [0, min(宽,高)/2]） */
	void SnapPropertiesToPixel();

	/** 矩形 → 槽矩形（编辑位置/尺寸后调用） */
	void SyncSlotFromGeometry();

	/** 由 Slate 层每帧回调：检测设计器拖动/拉伸。槽永远保持正向规范矩形；
	 *  映射在规范空间增量进行；矩形在自身内镜像 = 自身，翻转沿为 no-op；
	 *  交互结束（槽稳定且鼠标松开）才取整/标脏/复位符号基线 */
	void CheckSlotSync();

private:
	/** 上次写入槽的规范矩形（min 位置 + 正尺寸；槽永远保持正向，布局不受负尺寸影响）。
	 *  不变式：我们写槽后必更新缓存，Tick 中读到与缓存不同的值即设计器新输入 */
	FVector2D CachedSlotPos = FVector2D::ZeroVector;
	FVector2D CachedSlotSize = FVector2D(1.0, 1.0);
	/** 缓存是否已初始化。未初始化时首次 Tick 只采纳当前槽矩形为基准（不映射），
	 *  否则默认 1x1 缓存会被当成一次从 1x1 开始的"拖动"，尺寸被爆炸性放大 */
	bool bSlotCacheValid = false;
	/** 上一帧设计器原始槽尺寸的符号（跟踪符号基线；交互结束时复位。矩形镜像 = 自身，不产生实际映射） */
	bool bPrevRawNegX = false;
	bool bPrevRawNegY = false;
	/** 交互结束收尾：取整 + 标脏一次 */
	bool bPendingFinalize = false;
	/** 本次交互是否已 Modify（一次撤销事务） */
	bool bInteractionModified = false;
	/** 防止双向同步互相触发 */
	bool bSyncingGeometry = false;

protected:
	TSharedPtr<SSCADARectangle> MyRectangle;
};
