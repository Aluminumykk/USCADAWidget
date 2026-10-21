// Copyright Pr_UEDraw. All Rights Reserved.

#include "Slate/SSCADARegularPolygon.h"

void SSCADARegularPolygon::Construct(const FArguments& InArgs)
{
	Sides = FMath::Max(InArgs._Sides, 3);
	StartAngle = InArgs._StartAngle;

	// 与父类 Construct 同序（默认半径，UMG 层随后经 SetGeometry 推送真值并重采样）；
	// 不调父类 Construct——其参数类型不同且会以椭圆采样覆盖
	RadiusX = 50.0f;
	RadiusY = 50.0f;
	LocalCenter = FVector2D(RadiusX, RadiusY);
	ResamplePerimeter();

	// 闪烁需要在相位翻转时重绘（与椭圆同一机制）
	SetCanTick(true);
}

void SSCADARegularPolygon::SetSides(int32 InSides)
{
	Sides = FMath::Max(InSides, 3);
	ResamplePerimeter();
}

void SSCADARegularPolygon::SetStartAngle(float InStartAngle)
{
	StartAngle = InStartAngle;
	ResamplePerimeter();
}

void SSCADARegularPolygon::ResamplePerimeter()
{
	// 顶点 = 外接圆上 StartAngle 起均布 N 点（度→弧度；屏幕坐标 Y 向下，-90° = 顶点朝上）。
	// 半径恒用 RadiusX：控件层（继承圆控件）始终以 SetGeometry(R, R, ...) 等轴推送。
	const float R = RadiusX;
	const int32 N = FMath::Max(Sides, 3);

	TArray<FVector2D> Perimeter;
	Perimeter.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const float Angle = FMath::DegreesToRadians(StartAngle + 360.0f * (float)i / (float)N);
		Perimeter.Add(FVector2D(LocalCenter.X + R * FMath::Cos(Angle), LocalCenter.Y + R * FMath::Sin(Angle)));
	}
	Points.Set(Perimeter);
}
