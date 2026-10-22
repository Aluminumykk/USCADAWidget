// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCADAWidgetTypes.generated.h"

/** 线条样式（与 WinCC 线型下拉框对应） */
UENUM(BlueprintType)
enum class ESCADALineStyle : uint8
{
	Solid      UMETA(DisplayName = "实线 Solid"),
	Dashed     UMETA(DisplayName = "虚线 Dashed"),
	Dotted     UMETA(DisplayName = "点 Dotted"),
	DashDot    UMETA(DisplayName = "点划线 Dash-Dot"),
	DashDotDot UMETA(DisplayName = "双点划线 Dash-Dot-Dot")
};

/** 线端装饰样式（起点、终点独立设置，对应 WinCC 的“线起始端样式 / 线端样式”） */
UENUM(BlueprintType)
enum class ESCADALineEndStyle : uint8
{
	/** 默认 = 无装饰 */
	None  UMETA(DisplayName = "默认 None"),
	/** 实心填充箭头 */
	Arrow UMETA(DisplayName = "箭头 Arrow")
};

/** 线端形状 */
UENUM(BlueprintType)
enum class ESCADALineCap : uint8
{
	/** 无：端点平切 */
	None  UMETA(DisplayName = "无 None"),
	/** 圆形：端点处画一个直径等于线宽、向外凸出的实心圆盘 */
	Round UMETA(DisplayName = "圆形 Round")
};

/** 多边形填充图案（对应 WinCC 的“背景填充图案”） */
UENUM(BlueprintType)
enum class ESCADAFillPattern : uint8
{
	/** 实心填充 */
	Solid UMETA(DisplayName = "实心 Solid"),
	/** 透明（不填充，只画边框） */
	Transparent UMETA(DisplayName = "透明 Transparent")
};

/** 线性渐变的一个停靠点 */
USTRUCT(BlueprintType)
struct FSCADAGradientStop
{
	GENERATED_BODY()

	/** 位置（0-1，沿渐变轴） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Offset = 0.0f;

	/** 颜色（含透明度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor Color = FLinearColor::White;
};

/**
 * 线性渐变填充（填充类图元：多边形/椭圆/圆/矩形）。
 * 渐变轴用相对图形包围盒的 0-1 归一化坐标（SVG objectBoundingBox 语义），图形移动/缩放渐变随之。
 * 绘制 = 填充三角扇逐顶点算色（顶点投影到渐变轴 → 采样 stops）：线性函数经三角形
 * 线性插值是数学精确的，无需加密网格。启用时自动跳过 RoundedBox SDF 快速路径
 * （MakeBox 无逐顶点色概念，回退采样管线，观感不变）。
 */
USTRUCT(BlueprintType)
struct FSCADALinearGradient
{
	GENERATED_BODY()

	/** 启用后填充色改用渐变（FillColor 仅作轮廓 AA 覆层等处的代表色，建议保持为渐变平均色） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	/** 渐变轴起点（相对包围盒，0-1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	FVector2D AxisStart = FVector2D(0.0, 0.0);

	/** 渐变轴终点（相对包围盒，0-1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	FVector2D AxisEnd = FVector2D(1.0, 0.0);

	/** 停靠点（按 Offset 升序；少于 2 个时渐变不生效） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnabled"))
	TArray<FSCADAGradientStop> Stops;

	/** 采样 T（0-1，自动 clamp）处的颜色：相邻 stops 线性插值，两端按端点色平铺（pad） */
	FLinearColor Sample(float T) const
	{
		if (Stops.Num() == 0)
		{
			return FLinearColor::White;
		}
		if (Stops.Num() == 1 || T <= Stops[0].Offset)
		{
			return Stops[0].Color;
		}
		for (int32 i = 1; i < Stops.Num(); ++i)
		{
			if (T <= Stops[i].Offset)
			{
				const float Span = Stops[i].Offset - Stops[i - 1].Offset;
				const float LocalT = Span > KINDA_SMALL_NUMBER ? (T - Stops[i - 1].Offset) / Span : 0.0f;
				// SVG 渐变 = 逐分量直线插值（不用 HSV 插值，避免色相绕路）
				return Stops[i - 1].Color + (Stops[i].Color - Stops[i - 1].Color) * FMath::Clamp(LocalT, 0.0f, 1.0f);
			}
		}
		return Stops.Last().Color;
	}

	/** 是否生效（启用且停靠点足够） */
	bool IsActive() const { return bEnabled && Stops.Num() >= 2; }
};

/** 画布缩放模式（分辨率自适应）：以 CanvasSize 为设计基准，按实际分配尺寸自动算缩放系数 */
UENUM(BlueprintType)
enum class ESCADACanvasScaleMode : uint8
{
	/** 不自动缩放：使用手动 Zoom 系数（默认，旧行为） */
	None    UMETA(DisplayName = "无（手动 Zoom） None"),
	/** 等比缩放，设计区域完整显示在可视区域内（可能留白边） */
	Fit     UMETA(DisplayName = "等比适应 Fit"),
	/** 等比缩放填满可视区域（超出部分被裁切） */
	Fill    UMETA(DisplayName = "等比填满 Fill"),
	/** 非等比拉伸铺满可视区域：0,0→CanvasSize 恒等于可视区域四角（画面比例可能变形） */
	Stretch UMETA(DisplayName = "拉伸铺满 Stretch")
};
