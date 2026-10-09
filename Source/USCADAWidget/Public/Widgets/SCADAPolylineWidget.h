// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "SCADATypes.h"
#include "SCADAPolylineWidget.generated.h"

class SSCADALine;

/**
 * SCADA 折线控件：任意多个折点（对应 WinCC 的“折线”对象）。
 * Points 使用**画布设计坐标**（父容器为 SCADA Canvas 时即以画布左上角为原点），
 * 复用 SSCADALine Slate 层绘制（Slate 层本身就是折线控件，支持虚线/箭头/圆顶/闪烁，
 * 拐点固定按 线宽/2 做圆连接，消除粗线接缝）。
 * 控件的槽矩形（X/Y/宽/高）始终等于折线包围盒，双向同步：
 * 修改点位 → 槽矩形自动更新；在设计器中拖动/拉伸控件 → 点位坐标自动平移/缩放。
 * 输入时坐标/线宽取整到整数像素。
 */
UCLASS(meta = (DisplayName = "SCADA Polyline", Category = "SCADA"))
class USCADAWIDGET_API USCADAPolylineWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** 折线点位（画布设计坐标），按数组顺序依次连接 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline")
	TArray<FVector2D> Points = { FVector2D(0.0, 100.0), FVector2D(50.0, 0.0), FVector2D(100.0, 100.0) };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline", meta = (ClampMin = "1.0"))
	float Thickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline")
	ESCADALineStyle LineStyle = ESCADALineStyle::Solid;

	/** 起始端样式（默认 / 实心箭头） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline")
	ESCADALineEndStyle LineStartStyle = ESCADALineEndStyle::None;

	/** 末端样式（默认 / 实心箭头） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline")
	ESCADALineEndStyle LineEndStyle = ESCADALineEndStyle::None;

	/** 线端形状：无 / 圆形（两端同时生效，端点凸出实心半圆） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline")
	ESCADALineCap LineCap = ESCADALineCap::None;

	/** 闪烁：开启后每 1 秒在正常色与闪烁色之间切换（全局所有图元同步）。
	 *  FlashColor 透明度设为 0 即为“时隐时现”。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash")
	bool bFlashEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	FLinearColor FlashColor = FLinearColor::Red;

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void SetPoints(const TArray<FVector2D>& InPoints);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void SetColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void SetThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void SetLineStyle(ESCADALineStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void SetLineStartStyle(ESCADALineEndStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void SetLineEndStyle(ESCADALineEndStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void SetLineCap(ESCADALineCap InCap);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashColor(FLinearColor InColor);

	/** 在末尾追加一个点位 */
	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void AddPoint(FVector2D InPoint);

	/** 移除指定索引的点位（索引非法时安全忽略） */
	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void RemovePointAtIndex(int32 Index);

	/** 槽矩形被外部修改（设计器拖动/编辑槽属性）后由槽调用：以槽为准回写点位坐标并标脏资产 */
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
	/** 把 Points 各项转成槽内局部坐标（画布坐标 - 槽位置）同步给 Slate 层 */
	void PushPointsToSlate();

	/** 数据层像素对齐：所有点位/线宽取整到就近整数（线宽最小 1） */
	void SnapPropertiesToPixel();

	/** 点位包围盒 → 槽矩形（编辑点位坐标后调用） */
	void SyncSlotFromPoints();

	/** 点位包围盒 Min/Max（点数为 0 时给合理默认） */
	void GetPointsBounds(FVector2D& OutMin, FVector2D& OutMax) const;

	/** 几何数据包围盒左上角（初始放置认领用） */
	FVector2D GetGeometryMin() const;

	/** 几何数据整体平移（初始放置认领用） */
	void TranslateGeometryBy(const FVector2D& Delta);

	/** 由 Slate 层每帧回调：检测设计器拖动/拉伸。槽永远保持正向规范矩形；
	 *  映射在规范空间增量进行；翻转 = 原始尺寸符号变化沿 → 矩形内镜像一次；
	 *  交互结束（槽稳定且鼠标松开）才取整/标脏/复位符号基线 */
	void CheckSlotSync();

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
	/** 核心 Ticker 句柄（EnsureCoreTicker 注册，BeginDestroy 注销） */
	FTSTicker::FDelegateHandle CoreTickerHandle;

protected:
	TSharedPtr<SSCADALine> MyPolyline;
};
