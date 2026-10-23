// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "SCADAWidgetTypes.h"
#include "Binding/SCADABindingTypes.h"
#include "Widgets/SCADAPrimitiveWidgetBase.h"
#include "Containers/Ticker.h"
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
 *
 * UMG 属性绑定（细节面板 Bind 下拉）：可绑定属性旁各有隐藏的伴随委托属性
 * （命名 = 属性 C++ 名 + "Delegate"，UMGEditor FindDelegateProperty 的硬条件）；
 * 委托是“拉”模型而本控件走“推”模型，运行时由属性绑定轮询 Ticker 每帧 Execute()
 * 取值、按数据层规则取整后推送（见 PollPropertyBindings）。手动调用 Set* 会解除绑定。
 * 几何族（Center/RadiusX/RadiusY）经数据层取整 + 槽正向对齐后推送（与 Set* 同一数据流向）。
 */
UCLASS(meta = (DisplayName = "SCADA Ellipse", Category = "SCADA"))
class USCADAWIDGET_API USCADAEllipseWidget : public USCADAPrimitiveWidgetBase
{
	GENERATED_BODY()

public:
	/** 圆心（画布设计坐标） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse", meta = (Delta = "1.0", LinearDeltaSensitivity = "1"))
	FVector2D Center = FVector2D(50.0, 50.0);

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADAVector2D CenterDelegate;

	/** X 半径（像素，≥1） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse", meta = (ClampMin = "1.0", Delta = "1.0", LinearDeltaSensitivity = "1"))
	float RadiusX = 50.0f;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADAFloat RadiusXDelegate;

	/** Y 半径（像素，≥1） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse", meta = (ClampMin = "1.0", Delta = "1.0", LinearDeltaSensitivity = "1"))
	float RadiusY = 30.0f;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADAFloat RadiusYDelegate;

	/** 边框颜色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse")
	FLinearColor BorderColor = FLinearColor(156.0f / 255.0f, 154.0f / 255.0f, 165.0f / 255.0f, 1.0f);

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADALinearColor BorderColorDelegate;

	/** 边框宽度（0 = 不画边框；边框向图形内部绘制，外轮廓尺寸不随边宽变化） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse", meta = (ClampMin = "0.0"))
	float Thickness = 1.0f;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADAFloat ThicknessDelegate;

	/** 边框线型（实线/虚线/点/点划线/双点划线） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse")
	ESCADALineStyle LineStyle = ESCADALineStyle::Solid;

	/** 填充颜色（背景色） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse")
	FLinearColor FillColor = FLinearColor(241.0f / 255.0f, 241.0f / 255.0f, 242.0f / 255.0f, 1.0f);

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate） */
	UPROPERTY()
	FGetSCADALinearColor FillColorDelegate;

	/** 填充图案：实心 / 透明（不画填充） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse")
	ESCADAFillPattern FillPattern = ESCADAFillPattern::Solid;

	/** 线性渐变填充：启用后填充色改用渐变（SVG 导入的渐变素材自动写入；FillColor 保持为
	 *  渐变平均色，作轮廓 AA 覆层代表色与渐变停用时的回退色） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Ellipse")
	FSCADALinearGradient FillGradient;

	/** 闪烁：开启后每 1 秒在正常色与闪烁色之间切换（全局所有图元同步）。
	 *  只切填充到 FlashColor，边框保持 BorderColor（WinCC 语义：黑边白圆红闪 = 黑边红闪）；
	 *  FlashColor 透明度设为 0 即为“时隐时现”。FillPattern=透明时没有填充可闪（无可见闪烁）。 */
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
	 *  用于状态互闪。本控件默认属于正常组。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SCADA|Flash", meta = (EditCondition = "bFlashEnabled"))
	bool bFlashInverted = false;

	/** Bind 伴随委托：使本属性出现在 UMG 设计器 Bind 下拉中（命名必须 = 属性名+Delegate，bool 含 b 前缀） */
	UPROPERTY()
	FGetSCADABool bFlashInvertedDelegate;

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

	virtual void BeginDestroy() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	/** 绑定生效/变更（引擎在 AddBinding 成功后回调，仅 Slate 已创建时）：按需注册属性绑定轮询 Ticker */
	virtual void OnBindingChanged(const FName& Property) override;

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

	/** Center*k+off、RadiusX/Y*k；bScaleThickness 时边框 Thickness 一并乘 k */
	virtual void ScaleGeometryBy(float Scale, const FVector2D& PostOffset, bool bScaleThickness) override;

	/** 把半径同步给 Slate 层（中心由槽位置决定，本地中心恒为 (RadiusX, RadiusY)） */
	virtual void PushGeometryToSlate() override;

	/** 映射核心：中心/半径规范空间增量缩放；椭圆镜像 = 自身，翻转沿只做符号基线跟踪 */
	virtual void MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart) override;

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

protected:
	TSharedPtr<SSCADAEllipse> MyEllipse;
};
