// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "SCADAWidgetTypes.h"
#include "Widgets/SCADAPrimitiveWidgetBase.h"
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
 * 槽同步状态机骨架在基类 USCADAPrimitiveWidgetBase，本类只留映射核心（绝对映射+锚定）。
 */
UCLASS(meta = (DisplayName = "SCADA Circle", Category = "SCADA"))
class USCADAWIDGET_API USCADACircleWidget : public USCADAPrimitiveWidgetBase
{
	GENERATED_BODY()

public:
	/** 圆心（画布设计坐标） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle", meta = (Delta = "1.0", LinearDeltaSensitivity = "1"))
	FVector2D Center = FVector2D(50.0, 50.0);

	/** 半径（像素，≥1） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle", meta = (ClampMin = "1.0", Delta = "1.0", LinearDeltaSensitivity = "1"))
	float Radius = 30.0f;

	/** 边框颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle")
	FLinearColor BorderColor = FLinearColor(156.0f / 255.0f, 154.0f / 255.0f, 165.0f / 255.0f, 1.0f);

	/** 边框宽度（0 = 不画边框；边框向图形内部绘制，外轮廓尺寸不随边宽变化） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Circle", meta = (ClampMin = "0.0"))
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
	/** 数据层像素对齐：圆心/半径/线宽取整到就近整数（半径/线宽最小 1） */
	virtual void SnapPropertiesToPixel() override;

	/** 圆外接正方形 Min/Max = (Center - R, Center + R) */
	virtual void GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const override;

	/** 几何数据整体平移（初始放置认领用） */
	virtual void TranslateGeometryBy(const FVector2D& Delta) override;

	/** 把半径/圆心同步给 Slate 层：LocalCenter = 圆心 − 槽位置，由 SetLocalCenter 显式给出，
	 *  圆的显示位置与槽形状无关；仅在交互收尾才把槽对齐回外接正方形（见基类 CheckSlotSync 收尾分支）。 */
	virtual void PushGeometryToSlate() override;

	/** 映射核心（无状态绝对映射）：边长按拖动类型拆分（类型延迟锁定）——拖角 = 小边（槽不扩展），
	 *  拖边 = 被拖轴（未拖轴槽绕圆心扩展到直径），平移 = 小边；
	 *  圆心逐轴锚定（拖边 = 对边中心固定，拖角 = 对角固定；锚判定始终对拖动起点做）；
	 *  翻转沿为 no-op */
	virtual void MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart) override;

	/** 复位交互期锚定状态（交互结束/撤销识别/授权门闸拒绝/正向同步时由基类调用） */
	virtual void ResetInteractionState() override;

	/** 首次 Tick 采纳槽基准后补推一次 LocalCenter：编译/重建可能把 SynchronizeProperties
	 *  跑在槽挂上或槽位置恢复之前，那时算出的 LocalCenter 是错的并会一直留在 Slate 层
	 *  （半径不受影响 → 大小对、位置突变）；槽就位后的首个 Tick 这里按真实槽位置换算重推 */
	virtual void OnSlotCacheAdopted() override;

private:
	/** 交互期锚定状态（一次拖动内有效，交互结束/正向同步时经 ResetInteractionState 复位）。
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

protected:
	TSharedPtr<SSCADAEllipse> MyCircle;
};
