// Copyright Pr_UEDraw. All Rights Reserved.

#include "Slate/SSCADARectangle.h"

void SSCADARectangle::Construct(const FArguments& InArgs)
{
	Width = FMath::Max(InArgs._Width, 1.0f);
	Height = FMath::Max(InArgs._Height, 1.0f);
	CornerRadiusX = FMath::Max(InArgs._CornerRadiusX, 0.0f);
	CornerRadiusY = FMath::Max(InArgs._CornerRadiusY, 0.0f);
	ResamplePerimeter();

	// 闪烁需要在相位翻转时重绘（与线同一机制）
	SetCanTick(true);
}

void SSCADARectangle::SetWidth(float InWidth)
{
	Width = FMath::Max(InWidth, 1.0f);
	ResamplePerimeter();
}

void SSCADARectangle::SetHeight(float InHeight)
{
	Height = FMath::Max(InHeight, 1.0f);
	ResamplePerimeter();
}

void SSCADARectangle::SetCornerRadiusX(float InRadiusX)
{
	CornerRadiusX = FMath::Max(InRadiusX, 0.0f);
	ResamplePerimeter();
}

void SSCADARectangle::SetCornerRadiusY(float InRadiusY)
{
	CornerRadiusY = FMath::Max(InRadiusY, 0.0f);
	ResamplePerimeter();
}

void SSCADARectangle::ResamplePerimeter()
{
	// 圆角半径钳到合法范围：[0, min(宽,高)/2]
	const float MaxR = FMath::Min(Width, Height) * 0.5f;
	const float RX = FMath::Clamp(CornerRadiusX, 0.0f, MaxR);
	const float RY = FMath::Clamp(CornerRadiusY, 0.0f, MaxR);

	TArray<FVector2D> Perimeter;

	if (RX <= 0.0f || RY <= 0.0f)
	{
		// 直角：4 个角点
		Perimeter.Add(FVector2D(0.0f, 0.0f));
		Perimeter.Add(FVector2D(Width, 0.0f));
		Perimeter.Add(FVector2D(Width, Height));
		Perimeter.Add(FVector2D(0.0f, Height));
	}
	else
	{
		// 4 个椭圆角弧，从右上角弧开始顺时针（屏幕坐标 Y 向下）绕行一周。
		// 每角弧采样数自适应弧长：约每 4px 一段，Clamp 8~64
		const int32 Segments = FMath::Clamp(FMath::RoundToInt((PI * 0.5f) * FMath::Max(RX, RY) / 4.0f), 8, 64);
		struct FCorner { float CX, CY, StartAngle; };
		const FCorner Corners[4] = {
			{ Width - RX, RY, -PI * 0.5f },          // 右上：-90° → 0°
			{ Width - RX, Height - RY, 0.0f },       // 右下：0° → 90°
			{ RX, Height - RY, PI * 0.5f },          // 左下：90° → 180°
			{ RX, RY, PI },                          // 左上：180° → 270°
		};
		for (const FCorner& C : Corners)
		{
			for (int32 i = 0; i <= Segments; ++i)
			{
				const float Angle = C.StartAngle + (PI * 0.5f) * (float)i / (float)Segments;
				const FVector2D P(C.CX + RX * FMath::Cos(Angle), C.CY + RY * FMath::Sin(Angle));
				// 跳过与上一采样点重复的弧端点（相邻角弧端点 = 同一条直边的两端）
				if (Perimeter.Num() > 0 && P.Equals(Perimeter.Last(), 0.01f))
				{
					continue;
				}
				Perimeter.Add(P);
			}
		}
	}

	Points.Set(Perimeter);
}
