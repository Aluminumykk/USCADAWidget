// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "SCADAWidgetTypes.h"
#include "Binding/SCADABindingTypes.h"
#include "Widgets/SCADAPrimitiveWidgetBase.h"
#include "Containers/Ticker.h"
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
 * 槽同步状态机骨架在基类 USCADAPrimitiveWidgetBase，本类只留映射核心（增量缩放+翻转沿镜像）。
 *
 * UMG 属性绑定（细节面板 Bind 下拉）：可绑定属性旁各有隐藏的伴随委托属性
 * （命名 = 属性 C++ 名 + "Delegate"，UMGEditor FindDelegateProperty 的硬条件）；
 * 委托是“拉”模型而本控件走“推”模型，运行时由属性绑定轮询 Ticker 每帧 Execute()
 * 取值、按数据层规则取整后推送（见 PollPropertyBindings）。手动调用 Set* 会解除绑定。
 * Points 为数组类型，本轮不提供绑定（引擎无 TArray binder）。
 */
UCLASS(meta = (DisplayName = "SCADA Polyline", Category = "SCADA"))
class USCADAWIDGET_API USCADAPolylineWidget : public USCADAPrimitiveWidgetBase
{
	GENERATED_BODY()

public:
	/** 折线点位（画布设计坐标），按数组顺序依次连接。
	 *  数组增点（+ 追加 = 终点增；元素右键 Insert 到 0 = 起点增）时新点自动
	 *  沿就近线段延长 100px（见 PostEditChangeProperty），不再落在原点。
	 *  Delta/LinearDeltaSensitivity：细节面板数值拖动恒为 1 单位/像素
	 *  （默认拖动速度与数值大小成正比，坐标上百时过快）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline", meta = (Delta = "1.0", LinearDeltaSensitivity = "1"))
	TArray<FVector2D> Points = { FVector2D(0.0, 100.0), FVector2D(50.0, 0.0), FVector2D(100.0, 100.0) };

	/** 像素对齐：开启（默认）时点位在数据层/绘制层取整到整数像素（直线类图形边缘更锐利）；
	 *  关闭后保留亚像素坐标——SVG path 采样导入的小尺寸图形需要（密集采样顶点取整会塌陷变形），
	 *  导入工厂会自动为 path 生成的控件关闭本项。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline")
	bool bSnapToPixel = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline")
	FLinearColor Color = FLinearColor::White;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADALinearColor ColorDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Polyline", meta = (ClampMin = "1.0"))
	float Thickness = 2.0f;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADAFloat ThicknessDelegate;

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

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate，bool 含 b 前缀） */
	UPROPERTY()
	FGetSCADABool bFlashEnabledDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	FLinearColor FlashColor = FLinearColor::Red;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADALinearColor FlashColorDelegate;

	/** 相位翻转（翻转组）：开启后与全局定时器取色相反——正常相位显示闪烁色、
	 *  闪烁相位显示正常色。两组图元由同一时刻换相、显示色互反（一组亮时另一组灭），
	 *  用于状态互闪。本控件默认属于翻转组。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	bool bFlashInverted = true;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate，bool 含 b 前缀） */
	UPROPERTY()
	FGetSCADABool bFlashInvertedDelegate;

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

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashInverted(bool bInInverted);

	/** 在末尾追加一个点位 */
	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void AddPoint(FVector2D InPoint);

	/** 移除指定索引的点位（索引非法时安全忽略） */
	UFUNCTION(BlueprintCallable, Category = "SCADA|Polyline")
	void RemovePointAtIndex(int32 Index);

	virtual void BeginDestroy() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	/** 绑定生效/变更（引擎在 AddBinding 成功后回调，仅 Slate 已创建时）：按需注册属性绑定轮询 Ticker */
	virtual void OnBindingChanged(const FName& Property) override;

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	/** 数据层像素对齐：所有点位/线宽取整到就近整数（线宽最小 1） */
	virtual void SnapPropertiesToPixel() override;

	/** 点位包围盒 Min/Max（点数为 0 时给合理默认） */
	virtual void GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const override;

	/** 几何数据整体平移（初始放置认领用） */
	virtual void TranslateGeometryBy(const FVector2D& Delta) override;

	/** 把 Points 各项转成槽内局部坐标（画布坐标 - 槽位置）同步给 Slate 层 */
	virtual void PushGeometryToSlate() override;

	/** 映射核心：规范空间增量缩放 + 翻转沿镜像 */
	virtual void MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart) override;

	/** 失配自愈廉价短路：点位版本号未变则数据包围盒必与上次确认时一致，跳过 O(N) 包围盒遍历
	 *  （Points 的所有写入路径——内部方法/面板编辑/撤销重做通知——都会递增版本号） */
	virtual bool ShouldVerifyGeometryBounds() const override;

	/** 失配自愈确认一致：记录已验证版本（失配路径由 OnSlotAlignedToGeometry 记录） */
	virtual void OnGeometryBoundsVerified() override;

	/** 正向对齐完成：此刻数据包围盒 == 槽缓存，记录已验证版本 */
	virtual void OnSlotAlignedToGeometry() override;

private:
	// —— UMG 属性绑定轮询（“拉”→“推”桥）——
	// 委托绑定后引擎只在绑定生效时执行一次取值（或根本不执行），本控件走每帧轮询：
	// 任一伴随委托 IsBound 即注册核心 Ticker，每帧 Execute() → 数据层取整/钳制 →
	// 与成员现值比较，不同才写成员并推送（不调公开 Set*——那会 Unbind）。
	// 设计器实例委托天然未绑定，不会注册；BeginDestroy 兜底注销。

	/** 是否有任一 Bind 伴随委托处于绑定状态（Ticker 注册/自注销的判定） */
	bool HasBoundPropertyDelegates() const;

	/** 按需注册属性绑定轮询 Ticker（幂等；无任何委托绑定时直接返回） */
	void EnsurePropertyBindingTicker();

	/** 注销属性绑定轮询 Ticker（幂等） */
	void RemovePropertyBindingTicker();

	/** 每帧轮询：Execute() 各已绑委托 → 数据层规则取整 → 变化才写成员并推送 Slate */
	void PollPropertyBindings();

	FTSTicker::FDelegateHandle PropertyBindingTickerHandle;

private:
	/** PushGeometryToSlate 的复用缓冲：每次推送 Reset() 后填充，避免设计器拖动中每帧一次的新数组分配。
	 *  Slate 层 SetPoints 接收 TAttribute<TArray<FVector2D>> 按值拷贝存储，缓冲复用无悬垂风险 */
	TArray<FVector2D> PointsScratch;

	/** 点位数据版本号：Points 每次被写入时递增（SnapPropertiesToPixel / TranslateGeometryBy /
	 *  MapSlotToGeometry 槽映射循环；面板编辑与撤销/重做经 PostEditChangeProperty → SnapPropertiesToPixel
	 *  覆盖——引擎路径 FTransaction::Apply → PostEditUndo → PostEditChange → PostEditChangeProperty） */
	uint32 PointsVersion = 1;
	/** 已确认"数据包围盒 == 槽缓存"时的点位版本号：稳态失配自愈分支据此 O(1) 短路，
	 *  版本未变即跳过 O(N) 包围盒遍历 */
	uint32 BoundsVerifiedPointsVersion = 0;

#if WITH_EDITOR
	/** 上次已知的 Points 快照：检测数组增点（新元素插入位置 diff 用）。
	 *  PreEditChange 变更前取样（模板实例可能从不跑 SynchronizeProperties），
	 *  PostEditChangeProperty 处理完刷新 */
	TArray<FVector2D> PointsSnapshot;
	bool bPointsSnapshotValid = false;

	/** 数组增点后把新元素从默认 (0,0) 移到"沿就近线段延长 100px"处：
	 *  末尾追加 = 沿最后一段向前延长；开头插入 = 沿第一段反向延长；
	 *  中间插入 = 取两邻点中点（共线，不改变现有形状） */
	void RepositionNewlyInsertedPoint(int32 InsertIndex);
#endif

protected:
	TSharedPtr<SSCADALine> MyPolyline;
};
