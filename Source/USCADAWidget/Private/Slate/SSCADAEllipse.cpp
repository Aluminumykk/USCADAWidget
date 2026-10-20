// Copyright Pr_UEDraw. All Rights Reserved.

#include "Slate/SSCADAEllipse.h"

void SSCADAEllipse::Construct(const FArguments& InArgs)
{
	RadiusX = FMath::Max(InArgs._RadiusX, 1.0f);
	RadiusY = FMath::Max(InArgs._RadiusY, 1.0f);
	LocalCenter = FVector2D(RadiusX, RadiusY);
	ResamplePerimeter();

	// 闪烁需要在相位翻转时重绘（与线同一机制）
	SetCanTick(true);
}

void SSCADAEllipse::SetRadiusX(float InRadiusX)
{
	RadiusX = FMath::Max(InRadiusX, 1.0f);
	if (!bCenterExplicit)
	{
		LocalCenter = FVector2D(RadiusX, RadiusY);
	}
	ResamplePerimeter();
}

void SSCADAEllipse::SetRadiusY(float InRadiusY)
{
	RadiusY = FMath::Max(InRadiusY, 1.0f);
	if (!bCenterExplicit)
	{
		LocalCenter = FVector2D(RadiusX, RadiusY);
	}
	ResamplePerimeter();
}

void SSCADAEllipse::SetLocalCenter(FVector2D InCenter)
{
	// 显式本地中心（圆控件：槽在设计器拖动中不是正方形，圆心不一定在槽几何中心，
	// 由控件层把画布坐标圆心换算成槽内局部坐标传入，绘制位置与数据严格一致）
	bCenterExplicit = true;
	LocalCenter = InCenter;
	ResamplePerimeter();
}

void SSCADAEllipse::SetGeometry(float InRadiusX, float InRadiusY, FVector2D InLocalCenter)
{
	// 等价于 SetRadiusX + SetRadiusY + SetLocalCenter 连调后的终态：
	// 半径各自钳下限 ≥1；终态 bCenterExplicit 恒为 true（LocalCenter 显式传入），
	// 故单 Setter 里 !bCenterExplicit 的中心跟随分支对终态无贡献，无需复现；
	// 中间各次 ResamplePerimeter 写入的 Points 无人读取（下一个 Setter 立即覆盖），合并为一次。
	RadiusX = FMath::Max(InRadiusX, 1.0f);
	RadiusY = FMath::Max(InRadiusY, 1.0f);
	bCenterExplicit = true;
	LocalCenter = InLocalCenter;
	ResamplePerimeter();
}

bool SSCADAEllipse::TryGetAnalyticBox(FAnalyticBox& OutBox) const
{
	// 仅正圆走 SDF（半径数据层已取整，两半径来自同一整型值时严格相等，容差只兜历史浮点）；
	// 非等轴椭圆 SDF 无两轴半径可表达，返回 false 回退采样管线
	if (!FMath::IsNearlyEqual(RadiusX, RadiusY, 0.01f))
	{
		return false;
	}
	// 圆心半径换算成包围盒（LocalCenter 为显式指定值时同样精确——设计器拖动中槽不是正方形）
	OutBox.TopLeft = LocalCenter - FVector2D(RadiusX, RadiusX);
	OutBox.Size = FVector2D(RadiusX * 2.0, RadiusX * 2.0);
	OutBox.CornerRadius = RadiusX; // 正方形 + 半径=边长一半 = SDF 数学精确整圆
	return true;
}

void SSCADAEllipse::ResamplePerimeter()
{
	// 采样数自适应周长：约每 4px 一段，Clamp 32~256（大半径/粗线下圆仍光滑）
	const float MaxR = FMath::Max(RadiusX, RadiusY);
	const int32 Segments = FMath::Clamp(FMath::RoundToInt(2.0f * PI * MaxR / 4.0f), 32, 256);

	TArray<FVector2D> Perimeter;
	Perimeter.Reserve(Segments);
	for (int32 i = 0; i < Segments; ++i)
	{
		const float Angle = 2.0f * PI * (float)i / (float)Segments;
		Perimeter.Add(FVector2D(LocalCenter.X + RadiusX * FMath::Cos(Angle), LocalCenter.Y + RadiusY * FMath::Sin(Angle)));
	}
	Points.Set(Perimeter);
}
