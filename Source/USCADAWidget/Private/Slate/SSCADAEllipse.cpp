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
