// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "SCADATypes.h"
#include "SCADALineWidget.generated.h"

class SSCADALine;
class UCanvasPanelSlot;

/**
 * SCADA 直线控件：只有起点和终点（对应 WinCC 的“线”对象）。
 * StartPoint/EndPoint 使用**画布设计坐标**（父容器为 SCADA Canvas 时即以画布左上角为原点）。
 * 控件的槽矩形（X/Y/宽/高）始终等于线的包围盒，双向同步：
 * 修改端点 → 槽矩形自动更新；在设计器中拖动/拉伸控件 → 端点坐标自动平移/缩放。
 * 输入时坐标/线宽取整到整数像素。
 */
UCLASS(meta = (DisplayName = "SCADA Line", Category = "SCADA"))
class USCADAWIDGET_API USCADALineWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** 线起始端坐标（画布本地坐标） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line")
	FVector2D StartPoint = FVector2D(0.0, 0.0);

	/** 线端（终点）坐标（画布本地坐标）。默认从左上到右下的斜线，跨度 100 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Line")
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

	/** 槽矩形被外部修改（设计器拖动/编辑槽属性）后由槽调用：以槽为准回写端点坐标并标脏资产 */
	void ForceSyncFromSlotRect();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

public:
	virtual void BeginDestroy() override;

private:
	/** 注册核心 Ticker（幂等）：UMG 设计器双实例（archetype Outer=WidgetTree 无 Slate /
	 *  预览 Outer=WidgetTree_0 有 Slate）都可能单独收到槽写入——尤其面板拖放的落点只静默
	 *  写在 archetype 槽上且 archetype 的 Slate 不 Tick，没有核心 Ticker 就永远认领不到落点。
	 *  RebuildWidget/ForceSyncFromSlotRect 惰性注册，BeginDestroy 注销 */
	void EnsureCoreTicker();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual const FText GetPaletteCategory() override;
#endif

private:
	/** 把起点/终点转成槽内局部坐标（画布坐标 - 槽位置）同步给 Slate 层 */
	void PushPointsToSlate();

	/** 数据层像素对齐：坐标/线宽取整到就近整数（线宽最小 1） */
	void SnapPropertiesToPixel();

	/** 端点包围盒 → 槽矩形（编辑端点坐标后调用） */
	void SyncSlotFromPoints();

	/** 端点包围盒 Min/Max */
	void GetPointsBounds(FVector2D& OutMin, FVector2D& OutMax) const;

	/** 几何数据包围盒左上角（初始放置认领用） */
	FVector2D GetGeometryMin() const;

	/** 几何数据整体平移（初始放置认领用） */
	void TranslateGeometryBy(const FVector2D& Delta);

	/** 由 Slate 层每帧回调：检测设计器拖动/拉伸。槽永远保持正向规范矩形；
	 *  映射在规范空间增量进行；翻转 = 原始尺寸符号变化沿 → 矩形内镜像一次；
	 *  交互结束（槽稳定且鼠标松开）才取整/标脏/复位符号基线；
	 *  横平竖直（单轴退化）交互走端点跟随映射（见 MapDegenerateSlotRect） */
	void CheckSlotSync();

	/** 单轴退化（水平/竖直线）槽拖动映射：整体平移 / 被抓角对应侧端点跟随，
	 *  另一端点为基准不动（缩放映射对两个等偏移端点恒映射到同一边，线永远拉不斜） */
	void MapDegenerateSlotRect(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize);

	/** 槽写回端点包围盒规范矩形 + 更新缓存 + 推送 Slate（退化映射各分支共用收尾） */
	void WriteBackBoundsSlot(UCanvasPanelSlot* CanvasSlot);

private:
	/** 上次写入槽的规范矩形（min 位置 + 正尺寸；槽永远保持正向，布局不受负尺寸影响）。
	 *  不变式：我们写槽后必更新缓存，Tick 中读到与缓存不同的值即设计器新输入 */
	FVector2D CachedSlotPos = FVector2D::ZeroVector;
	FVector2D CachedSlotSize = FVector2D(1.0, 1.0);
	/** 缓存是否已初始化。未初始化时首次 Tick 只采纳当前槽矩形为基准（不映射），
	 *  否则默认 1x1 缓存会被当成一次从 1x1 开始的"拖动"，点位被爆炸性放大 */
	bool bSlotCacheValid = false;
	/** 初始放置认领标记：新实例首次发现槽位置与几何数据不一致时（设计器拖放落点），
	 *  把几何数据平移到槽位置而不是把槽拉回数据原点；认领后或首次映射时复位 */
	bool bPendingInitialPlacement = true;
	/** 上一帧设计器原始槽尺寸的符号（检测拖过对边的翻转沿；交互结束时复位） */
	bool bPrevRawNegX = false;
	bool bPrevRawNegY = false;
	/** 交互结束收尾：取整 + 标脏一次 */
	bool bPendingFinalize = false;
	/** 本次交互是否已 Modify（一次撤销事务） */
	bool bInteractionModified = false;
	/** 防止双向同步互相触发 */
	bool bSyncingGeometry = false;
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
	/** 核心 Ticker 句柄（EnsureCoreTicker 注册，BeginDestroy 注销） */
	FTSTicker::FDelegateHandle CoreTickerHandle;

protected:
	TSharedPtr<SSCADALine> MyLine;
};
