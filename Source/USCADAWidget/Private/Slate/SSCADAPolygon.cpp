// Copyright Pr_UEDraw. All Rights Reserved.

#include "Slate/SSCADAPolygon.h"

#include "Slate/SCADAStrokeGeometry.h"
#include "Styling/SlateBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateResourceHandle.h"

// 像素对齐 / 纯色笔刷 / 白色填充句柄已收进共享头 Slate/SCADAStrokeGeometry.h（SCADAStroke 命名空间）

// 耳切三角化已移至共享头 Slate/SCADAStrokeGeometry.h（Line 笔画包络与此处填充共用一份实现）

void SSCADAPolygon::Construct(const FArguments& InArgs)
{
	FillColor = InArgs._FillColor;
	FillPattern = InArgs._FillPattern;

	// 闪烁需要在相位翻转时重绘（与线同一机制）
	SetCanTick(true);
}

void SSCADAPolygon::SetFillColor(const TAttribute<FLinearColor>& InColor) { FillColor = InColor; }
void SSCADAPolygon::SetFillPattern(const TAttribute<ESCADAFillPattern>& InPattern) { FillPattern = InPattern; }

int32 SSCADAPolygon::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const TArray<FVector2D>& RawPoints = Points.Get();
	if (RawPoints.Num() < 3)
	{
		// 多边形至少需要 3 个点，否则退化不画
		return LayerId;
	}

	// 平滑曲线轮廓（椭圆 / 两轴圆角矩形的角弧）：绘制层跳过整数取整，
	// 避免密集采样曲线的每段 ~0.3px 阶梯抖动被 AA 覆层放大成可见波浪（数据真值不变）
	bSmoothContourActive = PaintsSmoothContour();

	// TAttribute 统一在本函数开头取值一次：几何缓存键与本帧绘制参数共用，避免多处重复 Get
	const float LineThickness = Thickness.Get();
	const ESCADALineStyle CurLineStyle = LineStyle.Get();
	const ESCADAFillPattern CurFillPattern = FillPattern.Get();

	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	// 闪烁：相位为“闪烁色”时**只切填充色**到 FlashColor，边框保持本色（WinCC 语义：
	// 黑边白圆红闪 = 黑边红闪）；全局 1 秒换相，与线同一逻辑。FlashColor 透明度 0 即填充隐藏。
	// 翻转组（bFlashInverted）与正常组取色相反：正常相位显示闪烁色、闪烁相位显示正常色。
	const bool bShowFlash = bFlashEnabled.Get() && (IsFlashPhaseNormal() == bFlashInverted.Get());
	const FLinearColor BorderFinalColor = Color.Get() * InWidgetStyle.GetColorAndOpacityTint();
	const FLinearColor FillFinalColor = (bShowFlash ? FlashColor.Get() : FillColor.Get()) * InWidgetStyle.GetColorAndOpacityTint();

	const FPaintGeometry PaintGeom = AllottedGeometry.ToPaintGeometry();
	// MakeLines 的线宽不随几何体变换缩放（点位会），画布 Zoom（或 DPI 缩放）≠1 时手动乘累积变换缩放（与线同一规则）
	const float RenderScale = (float)(AllottedGeometry.LocalToAbsolute(FVector2D(1.0, 0.0)) - AllottedGeometry.LocalToAbsolute(FVector2D::ZeroVector)).Size();
	const float ScaledLineThickness = LineThickness * RenderScale;

	// 0) 解析几何快速路径：正圆 / 直角或等半径圆角矩形 + 实线样式 → 一次 RoundedBox SDF
	//    （解析距离场：零折线感 + 解析抗锯齿，同时天然维持“外轮廓=几何=槽包围盒、边框内画”
	//    不变式）。圆角/边框宽度参数为局部单位，随渲染变换视觉等比，无需 RenderScale 补偿。
	//    边框宽度会使内轮廓坍塌（T ≥ 小边一半）时不走此路径——与采样管线“内缩坍塌静默跳过
	//    边框”的语义保持同侧（坍塌区间留给采样路径处理）。
	//    O(1) 常量几何，无需走几何缓存，在缓存之前短路。
	FAnalyticBox ABox;
	if (CurLineStyle == ESCADALineStyle::Solid && TryGetAnalyticBox(ABox)
		&& LineThickness < FMath::Min(ABox.Size.X, ABox.Size.Y) * 0.5f)
	{
		const bool bDrawFill = (CurFillPattern == ESCADAFillPattern::Solid);
		const bool bDrawBorder = (LineThickness > KINDA_SMALL_NUMBER);
		if (bDrawFill || bDrawBorder)
		{
			// 填充色走 MakeBox 的 Tint（含闪烁/风格 tint），brush 本体恒白（成员初始化器设定）；
			// 边框颜色/宽度/圆角是 SDF 的独立参数（OUTLINE_COLOR/OUTLINE_WEIGHT/CornerRadii），
			// 只切填充不影响边框——与采样路径的 WinCC 闪烁语义一致。
			AnalyticFillBrush.OutlineSettings.CornerRadii = FVector4(ABox.CornerRadius, ABox.CornerRadius, ABox.CornerRadius, ABox.CornerRadius);
			AnalyticFillBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			AnalyticFillBrush.OutlineSettings.Width = bDrawBorder ? LineThickness : 0.0f;
			AnalyticFillBrush.OutlineSettings.Color = BorderFinalColor;
			const FGeometry BoxGeom = AllottedGeometry.MakeChild(
				FVector2f(ABox.Size),
				FSlateLayoutTransform(FVector2f(ABox.TopLeft)));
			// NoPixelSnapping：Slate 默认对 Box 元素做设备像素顶点取整（IsPixelSnapped 默认
			// true，DrawElementTypes.h:347）。轴对齐时无害，但旋转/斜放元素的四角被取整会让
			// 长边平行摆动 ±0.5px（正是"直段发糙"的来源，DrawLine 路径无此取整故干净）。
			// 关闭后 SDF 保留亚像素边 + 1 局部单位着色器 AA，任何缩放下都平滑。
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, BoxGeom.ToPaintGeometry(), &AnalyticFillBrush,
				DrawEffects | ESlateDrawEffect::NoPixelSnapping, bDrawFill ? FillFinalColor : FLinearColor(0.f, 0.f, 0.f, 0.f));
		}
		return LayerId + 1;
	}

	// 几何脏标记缓存：键 = 影响几何的全部输入（见 FGeometryCache 注释）。命中时跳过重几何
	// （取整/闭合/三角化/内缩环带/划段拆分），直接用缓存几何重建 draw elements；
	// 未命中时重建 Poly/Closed 并作废各分支结果（分支结果在首次被走到时惰性重算）。
	FGeometryCache& C = GeoCache;
	const bool bCacheHit = C.bValid
		&& C.KeyThickness == LineThickness
		&& C.KeyLineStyle == CurLineStyle
		&& C.bKeySmoothContour == bSmoothContourActive
		&& C.KeyPoints == RawPoints; // TArray 逐元素精确比较（数据层已取整/确定性采样，无漂移）
	if (!bCacheHit)
	{
		C.KeyPoints = RawPoints;
		C.KeyThickness = LineThickness;
		C.KeyLineStyle = CurLineStyle;
		C.bKeySmoothContour = bSmoothContourActive;
		C.bValid = true;

		C.Poly.SetNumUninitialized(RawPoints.Num());
		for (int32 i = 0; i < RawPoints.Num(); ++i)
		{
			C.Poly[i] = bSmoothContourActive ? RawPoints[i] : SCADAStroke::SnapToPixel(RawPoints[i]);
		}

		// 闭合点列：原始点按序 + 首点回接（缓存结果即闭口环，覆层直接用，不再每帧拷贝）
		C.Closed = C.Poly;
		C.Closed.Add(C.Poly[0]);

		C.bFillComputed = false;
		C.bBandComputed = false;
		C.bDashComputed = false;
	}

	// 是否将按实线样式画内画边框（决定填充是否需要自带轮廓覆层：实线边框时其环带 +
	// 边框色覆层完整覆盖填充轮廓；若再画填充色覆层会在边框外缘外出现一半个像素的杂色晕）
	const bool bBorderSolidDrawn = (LineThickness > KINDA_SMALL_NUMBER) && (CurLineStyle == ESCADALineStyle::Solid);

	// 1) 填充（实心）：耳切三角化 + 一次 MakeCustomVerts 实心三角扇；三角化失败静默跳过（仍画边框）。
	//    三角化结果随缓存复用（几何不变时不再每帧耳切）。
	if (CurFillPattern == ESCADAFillPattern::Solid)
	{
		if (!C.bFillComputed)
		{
			C.bFillComputed = true;
			TArray<int32> Triangles;
			C.bFillValid = SCADAStroke::TriangulateEarClipping(C.Closed, Triangles);
			if (C.bFillValid)
			{
				SCADAStroke::ExpandTriangleList(C.Closed, Triangles, C.FillTriVerts, C.FillIdentityIndices);
			}
			else
			{
				C.FillTriVerts.Reset();
				C.FillIdentityIndices.Reset();
			}
		}
		if (C.bFillValid)
		{
			DrawTriangleList(OutDrawElements, LayerId, AllottedGeometry, C.FillTriVerts, C.FillIdentityIndices, FillFinalColor, DrawEffects);
		}

		// 填充轮廓抗锯齿覆层（CustomVerts 无抗锯齿）：沿外轮廓覆一条 3 屏幕像素宽的
		// 同色闭口 AA 线（两侧渐变 + 实心核，见 DrawContourAA 说明），把边缘外包络换成
		// 渐变边。虚线边框场景下由本覆层负责划间空隙里的填充边缘（划本身是 AA 线且在
		// 更高层，不受影响）。
		if (!bBorderSolidDrawn)
		{
			DrawContourAA(OutDrawElements, LayerId, PaintGeom, C.Closed, FillFinalColor, DrawEffects);
		}
	}

	// 2) 闭合边框（**内画 + 显式更高绘制层**）：几何数据 = 外轮廓 = 视觉边界；
	// 填充画在 LayerId、边框整体抬到 LayerId+1（Slate 同层内 MakeCustomVerts 与
	// MakeLines/MakeBox 合批不保证提交顺序，显式分层是严格层序）。
	// Thickness=0 不画边框且图形不会"少一圈"。
	if (LineThickness > KINDA_SMALL_NUMBER)
	{
		++LayerId; // 边框层：严格高于填充层
		if (CurLineStyle == ESCADALineStyle::Solid)
		{
			// 实心边框 = 环带四边形条带（不做耳切）：外环 = 原始轮廓（尖角精确到几何
			// 顶点），内环 = 内缩 T 的轮廓；逐边两个三角形（外边缘 i→j + 内边缘 j→i），
			// 相邻四边形共享边、零缝隙、无失败模式。注意**不能**把"外环正向 + 内环倒序"
			// 拼成单环多边形做耳切——单桥接的环带在闭合边那一侧根本没被围进多边形
			// （最后一段环带丢失）；描边方案在尖角处靠圆盘/斜接拐弯也盖不住角尖。
			if (!C.bBandComputed)
			{
				C.bBandComputed = true;
				C.bBandValid = BuildOffsetContour(C.Poly, LineThickness, C.InnerRing);
				if (C.bBandValid)
				{
					const int32 N = C.Poly.Num();
					TArray<FVector2D> BandVerts;
					BandVerts.Reserve(2 * N);
					for (int32 i = 0; i < N; ++i)
					{
						BandVerts.Add(C.Poly[i]);
					}
					for (int32 i = 0; i < N; ++i)
					{
						BandVerts.Add(C.InnerRing[i]);
					}

					TArray<int32> BandIndices;
					BandIndices.Reserve(6 * N);
					for (int32 i = 0; i < N; ++i)
					{
						const int32 j = (i + 1) % N;
						BandIndices.Add(i);
						BandIndices.Add(j);
						BandIndices.Add(N + j);
						BandIndices.Add(i);
						BandIndices.Add(N + j);
						BandIndices.Add(N + i);
					}
					SCADAStroke::ExpandTriangleList(BandVerts, BandIndices, C.BandTriVerts, C.BandIdentityIndices);
				}
				else
				{
					C.BandTriVerts.Reset();
					C.BandIdentityIndices.Reset();
				}
			}
			if (C.bBandValid)
			{
				DrawTriangleList(OutDrawElements, LayerId, AllottedGeometry, C.BandTriVerts, C.BandIdentityIndices, BorderFinalColor, DrawEffects);

				// 环带外缘抗锯齿覆层：沿外轮廓（原始轮廓）覆一条 3 屏幕像素宽的同色
				//（边框色）闭口 AA 线（两侧渐变 + 实心核，见 DrawContourAA 说明），
				// 补偿 CustomVerts 无抗锯齿，把外包络换成渐变边。
				DrawContourAA(OutDrawElements, LayerId, PaintGeom, C.Closed, BorderFinalColor, DrawEffects);

				// 环带内缘抗锯齿覆层（边框色，沿内缩轮廓）：内缘是边框与填充的交界，
				// 此前是唯一未处理的硬曲线——直边多边形上不可见，但椭圆/圆角矩形的
				// 弧形内缘会沿曲线显出锯齿；补齐后与 SDF 边框（内缘本有解析 AA）语义
				// 对齐，直线图元同样受益（交界 1px 渐变属正常渲染器行为）。
				DrawContourAA(OutDrawElements, LayerId, PaintGeom, C.InnerRing, BorderFinalColor, DrawEffects);
			}
		}
		else
		{
			// 虚线系：把尖角换成半径 线宽/2 的圆弧采样，让划线模式沿圆角流动。
			// 从首边中点 M 出发、回到 M 收尾：所有原始顶点都成为内部点，全部被圆角化
			// （BuildRoundedPolyline 不圆首/末点；M 与首边共线，无拐角）。内缩失败
			// （轮廓坍塌）静默跳过边框。划段拆分结果随缓存复用。
			if (!C.bDashComputed)
			{
				C.bDashComputed = true;
				TArray<FVector2D> Ring;
				C.bDashValid = BuildOffsetContour(C.Poly, LineThickness * 0.5f, Ring);
				if (C.bDashValid)
				{
					const FVector2D M = (Ring[0] + Ring[1]) * 0.5f;
					TArray<FVector2D> Loop;
					Loop.Reserve(Ring.Num() + 1);
					Loop.Add(M);
					for (int32 i = 1; i < Ring.Num(); ++i)
					{
						Loop.Add(Ring[i]);
					}
					Loop.Add(M);

					TArray<FVector2D> Smoothed;
					BuildRoundedPolyline(Loop, LineThickness * 0.5f, Smoothed);

					// 逐段拆分（Trim=0：多边形无箭头裁剪区）；“点”段画旋转实心方块
					BuildSegmentPoints(Smoothed, C.DashPairs, C.DashIsDot, 0.0f, 0.0f);
				}
				else
				{
					C.DashPairs.Reset();
					C.DashIsDot.Reset();
				}
			}
			if (C.bDashValid)
			{
				TArray<FVector2D> DashPair; // 复用缓冲（MakeLines 只接受 const TArray&）：首段分配一次、后续只覆写
				TArray<FVector2D> DotLoopBuf; // 同上：点方块轮廓覆层的 5 元素闭环缓冲
				for (int32 Seg = 0; 2 * Seg + 1 < C.DashPairs.Num(); ++Seg)
				{
					const FVector2D A = C.DashPairs[2 * Seg];
					const FVector2D B = C.DashPairs[2 * Seg + 1];
					if (C.DashIsDot.IsValidIndex(Seg) && C.DashIsDot[Seg])
					{
						// 点：严格 1:1 实心方块（边长 = 线宽），沿线段方向旋转（与线同一规则）
						const FVector2D Center = SCADAStroke::SnapToPixel((A + B) * 0.5);
						const FVector2D D = B - A;
						const float Angle = FMath::Atan2((float)D.Y, (float)D.X);
						const float S = LineThickness;
						const FGeometry DotGeom = AllottedGeometry.MakeChild(
							FVector2f(S, S),
							FSlateLayoutTransform(FVector2f((float)Center.X - S * 0.5f, (float)Center.Y - S * 0.5f)),
							FSlateRenderTransform(FQuat2f(Angle)),
							FVector2f(0.5f, 0.5f));
						// NoPixelSnapping：旋转后方刷四角 = 设备空间的斜四边形，默认取整会把旋转角度量化
						FSlateDrawElement::MakeBox(OutDrawElements, LayerId, DotGeom.ToPaintGeometry(), &SCADAStroke::GetSolidBrush(), DrawEffects | ESlateDrawEffect::NoPixelSnapping, BorderFinalColor);

						// 点方块轮廓抗锯齿覆层（MakeBox 方刷无 AA；四角 = 中心 + 旋转 ±S/2，见 DrawContourAA 说明）
						const float DotH = S * 0.5f;
						const float CosA = FMath::Cos(Angle);
						const float SinA = FMath::Sin(Angle);
						auto RotCorner = [&](float X, float Y)
						{
							return FVector2D(Center.X + X * CosA - Y * SinA, Center.Y + X * SinA + Y * CosA);
						};
						DotLoopBuf.Reset(5);
						DotLoopBuf.Add(RotCorner(-DotH, -DotH));
						DotLoopBuf.Add(RotCorner(DotH, -DotH));
						DotLoopBuf.Add(RotCorner(DotH, DotH));
						DotLoopBuf.Add(RotCorner(-DotH, DotH));
						const FVector2D DotLoopFirst = DotLoopBuf[0]; // 先拷贝再 Add，避免自引用触发 TArray 检查
						DotLoopBuf.Add(DotLoopFirst);
						DrawContourAA(OutDrawElements, LayerId, PaintGeom, DotLoopBuf, BorderFinalColor, DrawEffects);
					}
					else
					{
						// 划：独立两点线段（MakeLines 只连这两个点）
						DashPair.Reset(2);
						DashPair.Add(A);
						DashPair.Add(B);
						FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeom, DashPair, DrawEffects, BorderFinalColor, /*bAntialias=*/true, ScaledLineThickness);
					}
				}
			}
		}
	}

	return LayerId;
}

void SSCADAPolygon::DrawTriangleList(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
	const TArray<FVector2D>& TriVerts, const TArray<SlateIndex>& IdentityIndices, const FLinearColor& InColor, ESlateDrawEffect DrawEffects) const
{
	// 一次 MakeCustomVerts 实心三角扇：白色资源句柄回退（与 SSCADALine::AppendArrow 同一条路径），
	// 顶点位置在“绘制空间”（累积渲染变换后做像素对齐，与线体顶点同一规则），纯色由顶点 tint 决定。
	// 平滑曲线轮廓（bSmoothContourActive，见 PaintsSmoothContour）跳过顶点取整，保留亚像素几何。
	// TriVerts 已按三角形顺序展开（几何缓存重建时一次性生成），本函数只做逐顶点 FSlateVertex
	// 变换（累积渲染变换每帧可能变，顶点元素本就不能跨帧持有），恒等索引直接复用缓存。
	const FSlateRenderTransform& RT = AllottedGeometry.GetAccumulatedRenderTransform();
	const FColor Tint = InColor.ToFColor(true);
	TArray<FSlateVertex> Verts;
	Verts.Reserve(TriVerts.Num());
	if (bSmoothContourActive)
	{
		for (int32 i = 0; i < TriVerts.Num(); ++i)
		{
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, UE::Slate::CastToVector2f(TriVerts[i]), FVector2f::ZeroVector, Tint));
		}
	}
	else
	{
		for (int32 i = 0; i < TriVerts.Num(); ++i)
		{
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Enabled>(RT, UE::Slate::CastToVector2f(TriVerts[i]), FVector2f::ZeroVector, Tint));
		}
	}
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, SCADAStroke::GetWhiteFillHandle(), Verts, IdentityIndices, nullptr, 0, 0, DrawEffects);
}

bool SSCADAPolygon::BuildOffsetContour(const TArray<FVector2D>& Poly, float OffsetDistance, TArray<FVector2D>& OutClosedRing) const
{
	const int32 N = Poly.Num();
	OutClosedRing.Reset();
	if (N < 3)
	{
		return false;
	}
	if (FMath::Abs(OffsetDistance) <= KINDA_SMALL_NUMBER)
	{
		// 无需偏移：原轮廓闭合输出
		OutClosedRing = Poly;
		OutClosedRing.Add(Poly[0]);
		return true;
	}

	// 绕向：有向面积符号决定内法线方向（屏幕坐标 Y 向下）
	double Area2 = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A = Poly[i];
		const FVector2D& B = Poly[(i + 1) % N];
		Area2 += (double)A.X * B.Y - (double)B.X * A.Y;
	}
	if (FMath::IsNearlyZero((float)Area2))
	{
		return false; // 退化（零面积/共线）
	}
	const float WindingSign = Area2 > 0.0 ? 1.0f : -1.0f;

	// 每条边（i → i+1）的单位方向；零长边沿用上一有效边方向兜底
	TArray<FVector2D> EdgeDir;
	EdgeDir.SetNumUninitialized(N);
	FVector2D LastValid(1.0, 0.0);
	bool bHasValid = false;
	for (int32 i = 0; i < N; ++i)
	{
		FVector2D D = Poly[(i + 1) % N] - Poly[i];
		if (!D.IsNearlyZero())
		{
			D.Normalize();
			LastValid = D;
			bHasValid = true;
		}
		else
		{
			D = bHasValid ? LastValid : FVector2D(1.0, 0.0);
		}
		EdgeDir[i] = D;
	}

	// 每个偏移顶点 = 相邻两边偏移线的斜接交点：沿角平分线（内法线和）移动
	// D / cos(半角)（D 为负即向外偏移），凹顶点由同一公式自然处理；限幅 4x 防尖角处斜接爆炸。
	OutClosedRing.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D D1 = EdgeDir[(i - 1 + N) % N];
		const FVector2D D2 = EdgeDir[i];
		// 内法线：Area2>0（屏幕坐标下为顺时针）时 (-dy, dx) 指向图形内部
		const FVector2D N1 = WindingSign * FVector2D(-D1.Y, D1.X);
		const FVector2D N2 = WindingSign * FVector2D(-D2.Y, D2.X);

		FVector2D Miter = N1 + N2;
		if (Miter.IsNearlyZero())
		{
			// 相邻边反向（180° 尖峰）：退化取单边法线
			Miter = N1;
		}
		Miter.Normalize();
		const float CosHalf = FMath::Max((float)FVector2D::DotProduct(Miter, N1), 0.25f);
		const float MaxLen = FMath::Abs(OffsetDistance) * 4.0f;
		const float OffsetLen = FMath::Clamp(OffsetDistance / CosHalf, -MaxLen, MaxLen);
		OutClosedRing[i] = Poly[i] + Miter * OffsetLen;
	}

	// 向内偏移后面积翻号/归零 = 图形小于偏移距离，轮廓坍塌 → 失败（调用方跳过边框）；
	// 向外偏移面积只会增大，不会坍塌，跳过该检查
	if (OffsetDistance > 0.0f)
	{
		double OffsetArea2 = 0.0;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = OutClosedRing[i];
			const FVector2D& B = OutClosedRing[(i + 1) % N];
			OffsetArea2 += (double)A.X * B.Y - (double)B.X * A.Y;
		}
		if (OffsetArea2 * Area2 <= 0.0)
		{
			OutClosedRing.Reset();
			return false;
		}
	}

	// 闭合：必须先拷贝首点——Add 直接传自身元素引用（OutClosedRing[0]）会触发
	// TArray 自引用检查（扩容重分配时引用悬垂 → 断言崩溃）
	const FVector2D FirstPoint = OutClosedRing[0];
	OutClosedRing.Add(FirstPoint);
	return true;
}
