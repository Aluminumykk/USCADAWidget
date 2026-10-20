// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "SCADAWidgetTypes.h"
#include "Widgets/SCADAPrimitiveWidgetBase.h"
#include "SCADAEllipseWidget.generated.h"

class SSCADAEllipse;

/**
 * SCADA 椭圆控件（对应 WinCC 的“椭圆”对象）。
 * Center/RadiusX/RadiusY 使用**画布设计坐标**（父容器为 SCADA Canvas 时即以画布左上角为原点），
 * 由 SSCADAEllipse（继承 SSCADAPolygon）Slate 层绘制：
 * 实心/透明填充 + 闭合边框（实线/虚线/点线等线型，采样周点列复用多边形全套管线）。无箭头/端点样式。
 * 控件的槽矩形（X/Y/宽/高）始终等于椭圆包围盒 (Center - R, 2R)，双向同步：
 * 修改中心/半径 → 槽矩形自动更新；在设计器中拖动/拉伸控件 → 中心/半径自动平移/缩放。
 * 椭圆在自身包围盒内镜像 = 自身，拖过对边翻转天然无感（no-op）。
 * 输入时中心/半径/线宽取整到整数像素。
 * 槽同步状态机骨架在基类 USCADAPrimitiveWidgetBase，本类只留映射核心（中心/半径增量缩放）。
 */
UCLASS(meta = (DisplayName = "SCADA Ellipse", Category = "SCADA"))
class USCADAWIDGET_API USCADAEllipseWidget : public USCADAPrimitiveWidgetBase
{
	GENERATED_BODY()

public:
	/** 圆心（画布设计坐标） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse", meta = (Delta = "1.0", LinearDeltaSensitivity = "1"))
	FVector2D Center = FVector2D(50.0, 50.0);

	/** X 半径（像素，≥1） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse", meta = (ClampMin = "1.0", Delta = "1.0", LinearDeltaSensitivity = "1"))
	float RadiusX = 50.0f;

	/** Y 半径（像素，≥1） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse", meta = (ClampMin = "1.0", Delta = "1.0", LinearDeltaSensitivity = "1"))
	float RadiusY = 30.0f;

	/** 边框颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse")
	FLinearColor BorderColor = FLinearColor(156.0f / 255.0f, 154.0f / 255.0f, 165.0f / 255.0f, 1.0f);

	/** 边框宽度（0 = 不画边框；边框向图形内部绘制，外轮廓尺寸不随边宽变化） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse", meta = (ClampMin = "0.0"))
	float Thickness = 1.0f;

	/** 边框线型（实线/虚线/点/点划线/双点划线） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse")
	ESCADALineStyle LineStyle = ESCADALineStyle::Solid;

	/** 填充颜色（背景色） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse")
	FLinearColor FillColor = FLinearColor(241.0f / 255.0f, 241.0f / 255.0f, 242.0f / 255.0f, 1.0f);

	/** 填充图案：实心 / 透明（不画填充） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse")
	ESCADAFillPattern FillPattern = ESCADAFillPattern::Solid;

	/** 闪烁：开启后每 1 秒在正常色与闪烁色之间切换（全局所有图元同步）。
	 *  只切填充到 FlashColor，边框保持 BorderColor（WinCC 语义：黑边白圆红闪 = 黑边红闪）；
	 *  FlashColor 透明度设为 0 即为“时隐时现”。FillPattern=透明时没有填充可闪（无可见闪烁）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash")
	bool bFlashEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	FLinearColor FlashColor = FLinearColor::Red;

	/** 相位翻转（翻转组）：开启后与全局定时器取色相反——正常相位显示闪烁色、
	 *  闪烁相位显示正常色。两组图元由同一时刻换相、显示色互反（一组亮时另一组灭），
	 *  用于状态互闪。本控件默认属于正常组。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	bool bFlashInverted = false;

	UFUNCTION(BlueprintCallable, Category = "SCADA|Ellipse")
	void SetCenter(FVector2D InCenter);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Ellipse")
	void SetRadiusX(float InRadiusX);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Ellipse")
	void SetRadiusY(float InRadiusY);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Ellipse")
	void SetBorderColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Ellipse")
	void SetThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Ellipse")
	void SetLineStyle(ESCADALineStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Ellipse")
	void SetFillColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Ellipse")
	void SetFillPattern(ESCADAFillPattern InPattern);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashInverted(bool bInInverted);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	/** 数据层像素对齐：中心/半径/线宽取整到就近整数（半径/线宽最小 1） */
	virtual void SnapPropertiesToPixel() override;

	/** 椭圆包围盒 Min/Max = (Center - R, Center + R) */
	virtual void GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const override;

	/** 几何数据整体平移（初始放置认领用） */
	virtual void TranslateGeometryBy(const FVector2D& Delta) override;

	/** 把半径同步给 Slate 层（中心由槽位置决定，本地中心恒为 (RadiusX, RadiusY)） */
	virtual void PushGeometryToSlate() override;

	/** 映射核心：中心/半径规范空间增量缩放；椭圆镜像 = 自身，翻转沿只做符号基线跟踪 */
	virtual void MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart) override;

protected:
	TSharedPtr<SSCADAEllipse> MyEllipse;
};
