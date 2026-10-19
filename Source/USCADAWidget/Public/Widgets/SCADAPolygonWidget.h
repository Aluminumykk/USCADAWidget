// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "SCADAWidgetTypes.h"
#include "SCADAPolygonWidget.generated.h"

class SSCADAPolygon;

/**
 * SCADA 多边形控件：首尾闭合的多边形（对应 WinCC 的“多边形”对象）。
 * Points 使用**画布设计坐标**（父容器为 SCADA Canvas 时即以画布左上角为原点），
 * 由 SSCADAPolygon（继承 SSCADALine）Slate 层绘制：
 * 实心/透明填充（耳切三角化，支持凹多边形）+ 闭合边框（实线/虚线/点线等线型，
 * 拐点固定按 线宽/2 做圆连接，与折线一致）。无箭头/端点样式。
 * 控件的槽矩形（X/Y/宽/高）始终等于多边形包围盒，双向同步：
 * 修改点位 → 槽矩形自动更新；在设计器中拖动/拉伸控件 → 点位坐标自动平移/缩放。
 * 输入时坐标/线宽取整到整数像素。
 */
UCLASS(meta = (DisplayName = "SCADA Polygon", Category = "SCADA"))
class USCADAWIDGET_API USCADAPolygonWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** 多边形顶点（画布设计坐标），按数组顺序连接并首尾闭合。
	 *  数组增点（+ 追加 / 元素右键 Insert）时新点自动取两邻点中点（落在被插入的边上，
	 *  闭合环形索引），不再落在 (0,0)，见 PostEditChangeProperty。
	 *  Delta/LinearDeltaSensitivity：细节面板数值拖动恒为 1 单位/像素
	 *  （默认拖动速度与数值大小成正比，坐标上百时过快）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polygon", meta = (Delta = "1.0", LinearDeltaSensitivity = "1"))
	TArray<FVector2D> Points = { FVector2D(0.0, 100.0), FVector2D(50.0, 0.0), FVector2D(100.0, 100.0) };

	/** 边框颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polygon")
	FLinearColor BorderColor = FLinearColor(156.0f / 255.0f, 154.0f / 255.0f, 165.0f / 255.0f, 1.0f);

	/** 边框宽度（0 = 不画边框；边框向图形内部绘制，外轮廓尺寸不随边宽变化） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polygon", meta = (ClampMin = "0.0"))
	float Thickness = 1.0f;

	/** 边框线型（实线/虚线/点/点划线/双点划线） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polygon")
	ESCADALineStyle LineStyle = ESCADALineStyle::Solid;

	/** 填充颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polygon")
	FLinearColor FillColor = FLinearColor(241.0f / 255.0f, 241.0f / 255.0f, 242.0f / 255.0f, 1.0f);

	/** 填充图案：实心 / 透明（不画填充） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polygon")
	ESCADAFillPattern FillPattern = ESCADAFillPattern::Solid;

	/** 闪烁：开启后每 1 秒在正常色与闪烁色之间切换（全局所有图元同步）。
	 *  闪烁时边框和填充都切到 FlashColor；FlashColor 透明度设为 0 即为“时隐时现”。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash")
	bool bFlashEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	FLinearColor FlashColor = FLinearColor::Red;

	/** 相位翻转（翻转组）：开启后与全局定时器取色相反——正常相位显示闪烁色、
	 *  闪烁相位显示正常色。两组图元由同一时刻换相、显示色互反（一组亮时另一组灭），
	 *  用于状态互闪。本控件默认属于正常组。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	bool bFlashInverted = false;

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polygon")
	void SetPoints(const TArray<FVector2D>& InPoints);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polygon")
	void SetBorderColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polygon")
	void SetThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polygon")
	void SetLineStyle(ESCADALineStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polygon")
	void SetFillColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Polygon")
	void SetFillPattern(ESCADAFillPattern InPattern);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashInverted(bool bInInverted);

	/** 在末尾追加一个顶点 */
	UFUNCTION(BlueprintCallable, Category = "SCADA|Polygon")
	void AddPoint(FVector2D InPoint);

	/** 移除指定索引的顶点（索引非法时安全忽略） */
	UFUNCTION(BlueprintCallable, Category = "SCADA|Polygon")
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
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
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
	/** 一次性映射授权：属性面板编辑/方向键微调等合法"鼠标未按下改槽"经
	 *  NotifyContentSlotGeometryChanged → ForceSyncFromSlotRect 置位；撤销/编译等后台重写
	 *  走事务序列化（不经通知通道）无授权 → CheckSlotSync 拒绝映射、以数据为真值对齐槽 */
	bool bSlotRemapAuthorized = false;
	/** 核心 Ticker 句柄（EnsureCoreTicker 注册，BeginDestroy 注销） */
	FTSTicker::FDelegateHandle CoreTickerHandle;

#if WITH_EDITOR
	/** 上次已知的 Points 快照：检测数组增点（新元素插入位置 diff 用）。
	 *  PreEditChange 变更前取样（模板实例可能从不跑 SynchronizeProperties），
	 *  PostEditChangeProperty 处理完刷新 */
	TArray<FVector2D> PointsSnapshot;
	bool bPointsSnapshotValid = false;

	/** 数组增点后把新元素从默认 (0,0) 移到两邻点中点（闭合环形索引：新点落在
	 *  被插入的那条边上，不改变现有轮廓形状；多边形闭合无"起点/终点"之分） */
	void RepositionNewlyInsertedPoint(int32 InsertIndex);
#endif

protected:
	TSharedPtr<SSCADAPolygon> MyPolygon;
};
