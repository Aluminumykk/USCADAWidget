// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "SCADAWidgetTypes.h"
#include "Widgets/SCADAPrimitiveWidgetBase.h"
#include "SCADALineWidget.generated.h"

class SSCADALine;

/**
 * SCADA 直线控件：只有起点和终点（对应 WinCC 的“线”对象）。
 * StartPoint/EndPoint 使用**画布设计坐标**（父容器为 SCADA Canvas 时即以画布左上角为原点）。
 * 控件的槽矩形（X/Y/宽/高）始终等于线的包围盒，双向同步：
 * 修改端点 → 槽矩形自动更新；在设计器中拖动/拉伸控件 → 端点坐标自动平移/缩放。
 * 输入时坐标/线宽取整到整数像素。
 * 槽同步状态机骨架在基类 USCADAPrimitiveWidgetBase，本类只留映射核心（含横平竖直退化拖动）。
 */
UCLASS(meta = (DisplayName = "SCADA Line", Category = "SCADA"))
class USCADAWIDGET_API USCADALineWidget : public USCADAPrimitiveWidgetBase
{
	GENERATED_BODY()

public:
	/** 线起始端坐标（画布本地坐标）。
	 *  Delta/LinearDeltaSensitivity：细节面板数值拖动恒为 1 单位/像素
	 *  （默认拖动速度与数值大小成正比，坐标上百时过快）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line", meta = (Delta = "1.0", LinearDeltaSensitivity = "1"))
	FVector2D StartPoint = FVector2D(0.0, 0.0);

	/** 线端（终点）坐标（画布本地坐标）。默认从左上到右下的斜线，跨度 100 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line", meta = (Delta = "1.0", LinearDeltaSensitivity = "1"))
	FVector2D EndPoint = FVector2D(100.0, 100.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line", meta = (ClampMin = "1.0"))
	float Thickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line")
	ESCADALineStyle LineStyle = ESCADALineStyle::Solid;

	/** 线起始端样式（默认 / 实心箭头） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line")
	ESCADALineEndStyle LineStartStyle = ESCADALineEndStyle::None;

	/** 线端样式（默认 / 实心箭头） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line")
	ESCADALineEndStyle LineEndStyle = ESCADALineEndStyle::None;

	/** 线端形状：无 / 圆形（两端同时生效，端点凸出实心半圆） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line")
	ESCADALineCap LineCap = ESCADALineCap::None;

	/** 闪烁：开启后每 1 秒在正常色与闪烁色之间切换（全局所有图元同步）。
	 *  FlashColor 透明度设为 0 即为“时隐时现”。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash")
	bool bFlashEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	FLinearColor FlashColor = FLinearColor::Red;

	/** 相位翻转（翻转组）：开启后与全局定时器取色相反——正常相位显示闪烁色、
	 *  闪烁相位显示正常色。两组图元由同一时刻换相、显示色互反（一组亮时另一组灭），
	 *  用于状态互闪。本控件默认属于翻转组。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	bool bFlashInverted = true;

	UFUNCTION(BlueprintCallable, Category = "SCADA|Line")
	void SetStartPoint(FVector2D InPoint);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Line")
	void SetEndPoint(FVector2D InPoint);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Line")
	void SetColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Line")
	void SetThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Line")
	void SetLineStyle(ESCADALineStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Line")
	void SetLineStartStyle(ESCADALineEndStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Line")
	void SetLineEndStyle(ESCADALineEndStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Line")
	void SetLineCap(ESCADALineCap InCap);

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
	/** 数据层像素对齐：坐标/线宽取整到就近整数（线宽最小 1） */
	virtual void SnapPropertiesToPixel() override;

	/** 端点包围盒 Min/Max */
	virtual void GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const override;

	/** 几何数据整体平移（初始放置认领用） */
	virtual void TranslateGeometryBy(const FVector2D& Delta) override;

	/** 把起点/终点转成槽内局部坐标（画布坐标 - 槽位置）同步给 Slate 层 */
	virtual void PushGeometryToSlate() override;

	/** 映射核心：规范空间增量缩放 + 翻转沿镜像；
	 *  横平竖直（单轴退化）交互走端点跟随映射（见 MapDegenerateSlotRect） */
	virtual void MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart) override;

	/** 复位子类交互簿记（退化拖动模式/目标端点锁定） */
	virtual void ResetInteractionState() override;

private:
	/** 单轴退化（水平/竖直线）槽拖动映射：整体平移 / 被抓角对应侧端点跟随，
	 *  另一端点为基准不动（缩放映射对两个等偏移端点恒映射到同一边，线永远拉不斜） */
	void MapDegenerateSlotRect(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize);

	/** 槽写回端点包围盒规范矩形 + 更新缓存 + 推送 Slate（退化映射各分支共用收尾） */
	void WriteBackBoundsSlot(UCanvasPanelSlot* CanvasSlot);

private:
	/** 交互起点簿记（横平竖直退化拖动的基准；交互首帧取样 = 设计器 StartingOffsets
	 *  快照——STransformHandle::Resize 每帧从该快照绝对重写槽矩形，非增量） */
	FVector2D InteractionStartPos = FVector2D::ZeroVector;
	FVector2D InteractionStartSize = FVector2D(1.0, 1.0);
	/** 交互起始时两端点副本（端点跟随的绝对基准，避免帧间累积误差） */
	FVector2D StartPointAtInteractionStart = FVector2D::ZeroVector;
	FVector2D EndPointAtInteractionStart = FVector2D(100.0, 100.0);
	/** 本次交互是否锁定单轴退化（水平/竖直线）端点跟随模式（交互首帧判定，交互内不变） */
	bool bDegenerateDrag = false;
	/** 退化模式轴向：true = 水平线（Y 跨度 0，按 X 侧选目标端点）；false = 竖直线 */
	bool bDegenerateDragHorizontal = false;
	/** 退化模式目标端点（0=StartPoint，1=EndPoint，-1=未锁定；
	 *  锁前不动单端点，锁定时两端必未交叉） */
	int8 DegenerateTarget = -1;

protected:
	TSharedPtr<SSCADALine> MyLine;
};
