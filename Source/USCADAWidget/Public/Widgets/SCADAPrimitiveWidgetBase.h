// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "SCADAPrimitiveWidgetBase.generated.h"

class SSCADALine;
class UCanvasPanelSlot;

/**
 * SCADA 图元控件公共基类：收纳 6 个图元控件（Line/Polyline/Polygon/Ellipse/Circle/Rectangle）
 * 逐字复制的槽同步状态机骨架——核心 Ticker 注册/注销（BeginDestroy）、Slate Tick 回调挂接
 * （SetupSlotSyncCallbacks）、ForceSyncFromSlotRect（属性面板/微调/拖放的授权输入通道）、
 * SyncSlotFromGeometry（几何数据 → 槽矩形的正向对齐）、CheckSlotSync 主流程
 * （缓存初始化、初始放置认领、撤销识别主判别器、授权门闸、收尾、失配自愈、Modify）。
 * 子类差异经 protected virtual 钩子表达：GetGeometryBounds / TranslateGeometryBy /
 * PushGeometryToSlate / SnapPropertiesToPixel / MapSlotToGeometry（映射核心，含规范化与翻转沿）。
 * 本类不含任何 UPROPERTY（不改变子类序列化布局，已存资产安全），状态成员全部非反射。
 */
UCLASS(Abstract)
class USCADAWIDGET_API USCADAPrimitiveWidgetBase : public UWidget
{
	GENERATED_BODY()

public:
	/** 槽矩形被外部修改（设计器拖动/编辑槽属性）后由槽调用：以槽为准回写几何数据并标脏资产 */
	void ForceSyncFromSlotRect();

	/** 供画布"适配内容"等批量操作用：读取几何数据包围盒（protected GetGeometryBounds 钩子的公开转发） */
	void GetGeometryBoundsPublic(FVector2D& OutMin, FVector2D& OutMax) const { GetGeometryBounds(OutMin, OutMax); }

	/** 几何数据整体平移 Delta 并经正向通道同步槽/Slate（刷新槽缓存，
	 *  CheckSlotSync 不会把它误判为设计器拖动输入） */
	void TranslateGeometryAndSync(const FVector2D& Delta);

	/** 供画布"内容适配画布"等批量操作用：几何数据以画布原点为基准整体缩放
	 *  （p → p*Scale + PostOffset，PostOffset 为缩放后的居中平移量，一次到位避免两次取整误差），
	 *  随后按 Set* 接口同款程序段取整（SnapPropertiesToPixel 各自规则）+ 正向同步槽/Slate。
	 *  bSnapToPixel=false 的图元（SVG path 采样导入）点位保留亚像素，不被破坏。 */
	void ScaleGeometryAndSync(float Scale, const FVector2D& PostOffset);

	virtual void BeginDestroy() override;

protected:
	/** RebuildWidget 内调用：给 Slate 层挂接 ExternalTickCallback（编辑器/非游戏世界守卫，
	 *  运行时 PIE/打包不挂接）并注册核心 Ticker 兜底（archetype 实例无 Slate Tick） */
	void SetupSlotSyncCallbacks(SSCADALine* SlateWidget);

	/** 数据层像素对齐：几何坐标/线宽取整到就近整数（子类实现各自字段）。
	 *  注意不用 C++ 纯虚（= 0）：UHT 生成代码在非 CHECK_PUREVIRTUALS 配置下会
	 *  placement-new 本类，真纯虚导致 C2259 无法编译；语义纯虚以默认实现 + 注释表达 */
	virtual void SnapPropertiesToPixel() { }

	/** 几何数据包围盒 → 槽矩形（编辑几何属性后调用；正向同步以数据为真值） */
	virtual void SyncSlotFromGeometry();

	/** 几何数据包围盒 Min/Max（原始值，调用方按需把尺寸 Clamp 到 ≥1）。
	 *  语义纯虚（见 SnapPropertiesToPixel 注释）；默认给 1x1 防退化 */
	virtual void GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const
	{
		OutMin = FVector2D::ZeroVector;
		OutMax = FVector2D(1.0, 1.0);
	}

	/** 几何数据包围盒左上角（初始放置认领用；默认 = GetGeometryBounds 的 Min） */
	virtual FVector2D GetGeometryMin() const;

	/** 几何数据整体平移（初始放置认领用）。语义纯虚（见 SnapPropertiesToPixel 注释） */
	virtual void TranslateGeometryBy(const FVector2D& Delta) { }

	/** 几何数据以 (0,0) 为基准整体缩放 p → p*Scale + PostOffset（画布"内容适配画布"用）；
	 *  bScaleThickness=true 时把视觉尺寸随比例增长的"厚度类"属性一并乘 Scale
	 *  （线宽——箭头尺寸在 Slate 层由线宽推导；矩形圆角属几何本体，无条件缩放）。
	 *  本钩子只写数据不取整（取整由调用方随后 SnapPropertiesToPixel 按各子类规则收尾）。
	 *  语义纯虚（见 SnapPropertiesToPixel 注释） */
	virtual void ScaleGeometryBy(float Scale, const FVector2D& PostOffset, bool bScaleThickness) { }

	/** 把几何数据换算成槽内局部坐标同步给 Slate 层。语义纯虚（见 SnapPropertiesToPixel 注释） */
	virtual void PushGeometryToSlate() { }

	/** 映射核心（设计器写入了新槽矩形后调用）：规范化/增量缩放/翻转沿镜像/锚定等
	 *  子类专属映射，负责更新几何数据并把槽写回正向规范矩形、刷新缓存、推送 Slate。
	 *  bInteractionStart = 本次交互首帧（此刻缓存仍是交互前的槽矩形，供交互起点簿记取样）。
	 *  语义纯虚（见 SnapPropertiesToPixel 注释） */
	virtual void MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart) { }

	/** 复位子类专属交互簿记：交互收尾/撤销识别/授权门闸拒绝/正向对齐时调用（默认空；
	 *  Line 复位退化拖动模式，Circle 复位锚定/拖动类型锁定等） */
	virtual void ResetInteractionState() { }

	/** 失配自愈分支的廉价短路：返回 false 跳过包围盒遍历
	 *  （折线/多边形按点位版本号实现 O(1) 短路；默认 true 恒检查） */
	virtual bool ShouldVerifyGeometryBounds() const { return true; }

	/** 失配自愈确认"数据包围盒 == 槽缓存"后回调（折线/多边形记录已验证版本；默认空） */
	virtual void OnGeometryBoundsVerified() { }

	/** SyncSlotFromGeometry 正向对齐完成（缓存已更新）后回调
	 *  （折线/多边形记录已验证点位版本；默认空） */
	virtual void OnSlotAlignedToGeometry() { }

	/** 首次 Tick 采纳槽基准后回调（圆在此补推一次 LocalCenter 防编译重建时序滞后；默认空） */
	virtual void OnSlotCacheAdopted() { }

private:
	/** 注册核心 Ticker（幂等）：UMG 设计器双实例（archetype Outer=WidgetTree 无 Slate /
	 *  预览 Outer=WidgetTree_0 有 Slate）都可能单独收到槽写入——尤其面板拖放的落点只静默
	 *  写在 archetype 槽上且 archetype 的 Slate 不 Tick，没有核心 Ticker 就永远认领不到落点。
	 *  SetupSlotSyncCallbacks/ForceSyncFromSlotRect 惰性注册，BeginDestroy 注销 */
	void EnsureCoreTicker();

	/** 槽同步主流程（由 Slate 层 Tick 回调与核心 Ticker 每帧驱动）：检测设计器拖动/拉伸。
	 *  槽永远保持正向规范矩形（min 位置 + 正尺寸），布局不受负尺寸影响，绘制不跳；
	 *  几何数据是唯一真值，槽只是操作手柄；交互结束（槽稳定且鼠标松开）才取整/标脏/复位符号基线 */
	void CheckSlotSync();

protected:
	/** 上次写入槽的规范矩形（min 位置 + 正尺寸；槽永远保持正向，布局不受负尺寸影响）。
	 *  不变式：我们写槽后必更新缓存，Tick 中读到与缓存不同的值即设计器新输入 */
	FVector2D CachedSlotPos = FVector2D::ZeroVector;
	FVector2D CachedSlotSize = FVector2D(1.0, 1.0);
	/** 缓存是否已初始化。未初始化时首次 Tick 只采纳当前槽矩形为基准（不映射），
	 *  否则默认 1x1 缓存会被当成一次从 1x1 开始的"拖动"，几何数据被爆炸性放大 */
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
};
