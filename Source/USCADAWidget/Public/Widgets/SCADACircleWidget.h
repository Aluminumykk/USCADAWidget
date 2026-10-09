// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "SCADATypes.h"
#include "SCADACircleWidget.generated.h"

class SSCADAEllipse;

/**
 * SCADA 圆控件（对应 WinCC 的“圆”对象，单半径）。
 * Center/Radius 使用**画布设计坐标**（父容器为 SCADA Canvas 时即以画布左上角为原点），
 * Slate 层直接复用 SSCADAEllipse（RadiusX = RadiusY = Radius）：
 * 实心/透明填充 + 闭合边框（实线/虚线/点线等线型，采样周点列复用多边形全套管线）。无箭头/端点样式。
 * 控件的槽矩形（X/Y/宽/高）始终等于圆的外接正方形 (Center - R, 2R)，双向同步：
 * 修改圆心/半径 → 槽矩形自动更新；设计器交互用**无状态绝对映射**：
 * 边长按拖动类型拆分（类型延迟锁定：两轴都有锚才锁拖角，单轴有锚且尺寸明显变化才锁拖边）——
 * 拖角 = min(宽, 高)（小边，圆恒内切于设计器矩形，槽不扩展）；
 * 拖边 = 被拖轴尺寸（外拉放大、内推缩小都有效，槽未拖轴绕圆心扩展到直径）；
 * 平移 = 小边（纯平移）；
 * 圆心逐轴锚定（拖边 = 对边中心固定，拖角 = 对角固定，整体拖 = 随矩形平移；
 * 锚判定始终对拖动起点做、每轴独立，天然翻转/换向鲁棒；边长类型延迟锁定——
 * 拖角起步帧可能只有一轴动，首帧锁定会误判成拖边；
 * 不用增量缩放——统一标量系数会让圆心沿非拖动轴漂移）。
 * 拖动中槽保持设计器矩形，圆由 SSCADAEllipse::SetLocalCenter 按数据精确绘制；
 * 交互结束收尾时槽对齐回外接正方形（无跳变）；拖过对边翻转天然无感（no-op）。
 * 输入时圆心/半径/线宽取整到整数像素。
 */
UCLASS(meta = (DisplayName = "SCADA Circle", Category = "SCADA"))
class USCADAWIDGET_API USCADACircleWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** 圆心（画布设计坐标） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle")
	FVector2D Center = FVector2D(50.0, 50.0);

	/** 半径（像素，≥1） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle", meta = (ClampMin = "1.0"))
	float Radius = 30.0f;

	/** 边框颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle")
	FLinearColor BorderColor = FLinearColor(156.0f / 255.0f, 154.0f / 255.0f, 165.0f / 255.0f, 1.0f);

	/** 边框宽度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle", meta = (ClampMin = "1.0"))
	float Thickness = 1.0f;

	/** 边框线型（实线/虚线/点/点划线/双点划线） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle")
	ESCADALineStyle LineStyle = ESCADALineStyle::Solid;

	/** 填充颜色（背景色） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle")
	FLinearColor FillColor = FLinearColor(241.0f / 255.0f, 241.0f / 255.0f, 242.0f / 255.0f, 1.0f);

	/** 填充图案：实心 / 透明（不画填充） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle")
	ESCADAFillPattern FillPattern = ESCADAFillPattern::Solid;

	/** 闪烁：开启后每 1 秒在正常色与闪烁色之间切换（全局所有图元同步）。
	 *  闪烁时边框和填充都切到 FlashColor；FlashColor 透明度设为 0 即为“时隐时现”。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash")
	bool bFlashEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	FLinearColor FlashColor = FLinearColor::Red;

	UFUNCTION(BlueprintCallable, Category = "SCADA|Circle")
	void SetCenter(FVector2D InCenter);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Circle")
	void SetRadius(float InRadius);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Circle")
	void SetBorderColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Circle")
	void SetThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Circle")
	void SetLineStyle(ESCADALineStyle InStyle);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Circle")
	void SetFillColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Circle")
	void SetFillPattern(ESCADAFillPattern InPattern);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "SCADA|Flash")
	void SetFlashColor(FLinearColor InColor);

	/** 槽矩形被外部修改（设计器拖动/编辑槽属性）后由槽调用：以槽为准回写圆心/半径并标脏资产 */
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
	/** 把半径同步给 Slate 层（圆心由槽位置决定，本地中心恒为 (Radius, Radius)） */
	void PushGeometryToSlate();

	/** 数据层像素对齐：圆心/半径/线宽取整到就近整数（半径/线宽最小 1） */
	void SnapPropertiesToPixel();

	/** 圆外接正方形 → 槽矩形（编辑圆心/半径后调用） */
	void SyncSlotFromGeometry();

	/** 几何数据包围盒左上角（初始放置认领用） */
	FVector2D GetGeometryMin() const;

	/** 几何数据整体平移（初始放置认领用） */
	void TranslateGeometryBy(const FVector2D& Delta);

	/** 确保核心 Ticker 已注册（惰性，首次交互/重建时调用）。
	 *  必须让所有实例都能 Tick：UMG 设计器里同一控件存在 WidgetTree(archetype，无 Slate)
	 *  与 WidgetTree_0(预览，有 Slate) 两个实例，设计器拖动会把槽矩形镜像写进两棵树，
	 *  两边各自映射；没有 Slate 的 archetype 若只能靠 Slate 层回调 Tick，就永远走不到
	 *  收尾（取整/复位/槽回正），两边状态（缓存/拖动起点/锁定模式）逐渐分叉，
	 *  编译时用 archetype 数据重建预览 → 显示回退/跳变。 */
	void EnsureCoreTicker();

	/** 由 Slate 层每帧回调：检测设计器拖动/拉伸。槽永远保持正向规范矩形；
	 *  无状态绝对映射：边长按拖动类型拆分（类型延迟锁定）——拖角 = 小边（槽不扩展），
	 *  拖边 = 被拖轴（未拖轴槽绕圆心扩展到直径），平移 = 小边；
	 *  圆心逐轴锚定（拖边 = 对边中心固定，拖角 = 对角固定；锚判定始终对拖动起点做）；
	 *  翻转沿为 no-op；交互结束（槽稳定且鼠标松开）才取整/标脏/复位符号基线并把槽对齐回外接正方形 */
	void CheckSlotSync();

private:
	/** 上次写入槽的规范矩形（min 位置 + 正尺寸；槽永远保持正向，布局不受负尺寸影响）。
	 *  不变式：我们写槽后必更新缓存，Tick 中读到与缓存不同的值即设计器新输入 */
	FVector2D CachedSlotPos = FVector2D::ZeroVector;
	FVector2D CachedSlotSize = FVector2D(1.0, 1.0);
	/** 一次性授权：属性面板编辑槽（X/Y/宽/高）经 USCADACanvasSlot 通知 ForceSyncFromSlotRect
	 *  时置位，CheckSlotSync 消费后清除。鼠标未按下且无授权的槽输入 = 设计器后台重写
	 * （编译重建后恢复几何、松手提交等），一律拒绝映射、以数据为真值对齐——
	 * 否则拖边后编译时设计器重写的拖拽矩形会被误判成"平移"交互，把圆改回拖动前的大小/位置。 */
	bool bSlotRemapAuthorized = false;
	/** 缓存是否已初始化。未初始化时首次 Tick 只采纳当前槽矩形为基准（不映射），
	 *  否则默认 1x1 缓存会被当成一次从 1x1 开始的"拖动"，半径被爆炸性放大 */
	bool bSlotCacheValid = false;
	/** 初始放置认领标记：新实例首次发现槽位置与几何数据不一致时（设计器拖放落点），
	 *  把几何数据平移到槽位置而不是把槽拉回数据原点；认领后或首次映射时复位 */
	bool bPendingInitialPlacement = true;
	/** 上一帧设计器原始槽尺寸的符号（跟踪符号基线；交互结束时复位。圆镜像 = 自身，不产生实际映射） */
	bool bPrevRawNegX = false;
	bool bPrevRawNegY = false;
	/** 交互结束收尾：取整 + 标脏一次 */
	bool bPendingFinalize = false;
	/** 本次交互是否已 Modify（一次撤销事务） */
	bool bInteractionModified = false;
	/** 防止双向同步互相触发 */
	bool bSyncingGeometry = false;
	/** 交互期锚定状态（一次拖动内有效，交互结束/正向同步时复位）。
	 *  拖动起点矩形 = 首个输入帧的缓存矩形；锚边/锚点判定始终**对拖动起点**做（每轴独立：
	 *  该轴只有一条边相对起点动了 → 另一条边是锚的物理坐标；两边都动 = 无锚 = 平移）。
	 *  对起点判定天然翻转/停顿/换向鲁棒（翻过锚点后不动边换侧，但物理锚点相对起点不变）。
	 *  拖动类型锁定（bDragModeLocked/bCornerDrag，仅决定边长规则与槽扩展，锚判定不锁）：
	 *  两轴都有锚 → 拖角；单轴有锚且该轴尺寸已变 >3px、另一轴 <1px → 拖边；平移永不锁定。
	 *  必须延迟锁定：拖角起步帧可能只有一轴动了，首帧就锁会误锁成拖边 → 另一轴永远无锚、
	 *  圆心取矩形中心 → 圆跟着滑动。锁定前边长按小边、不扩展（起步几像素瞬态）。 */
	bool bDragStartValid = false;
	FVector2D DragStartPos = FVector2D::ZeroVector;
	FVector2D DragStartSize = FVector2D(1.0, 1.0);
	bool bDragModeLocked = false;
	bool bCornerDrag = false;
	/** 锚点滞后保持（每轴独立）：两端都对得上拖动起点（缓慢移动/原位翻转的歧义帧）时沿用上一帧锚点，
	 *  防止歧义帧锚点丢失、圆心跳到矩形中心。新拖动/收尾/正向同步时复位 */
	float LastAnchorX = 0.0f;
	float LastAnchorY = 0.0f;
	bool bLastAnchorValidX = false;
	bool bLastAnchorValidY = false;
	/** 核心 Ticker 句柄（EnsureCoreTicker 注册，BeginDestroy 注销） */
	FTSTicker::FDelegateHandle CoreTickerHandle;

protected:
	TSharedPtr<SSCADAEllipse> MyCircle;

public:
	virtual void BeginDestroy() override;
};
