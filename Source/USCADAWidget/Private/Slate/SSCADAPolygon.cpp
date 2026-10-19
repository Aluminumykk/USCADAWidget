// Copyright Pr_UEDraw. All Rights Reserved.

#include "Slate/SSCADAPolygon.h"

#include "Styling/SlateBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateResourceHandle.h"

/** 像素对齐：四舍五入到就近整数坐标（与线同一规则） */
static FVector2D SnapToPixel(const FVector2D& P)
{
	return FVector2D(FMath::RoundToFloat(P.X), FMath::RoundToFloat(P.Y));
}

/** 无贴图纯色笔刷（画点样式的实心方块用） */
static const FSlateBrush& GetSolidBrush()
{
	static FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::Box;
	return Brush;
}

/** 纯色填充用的白色资源句柄：GetSolidBrush() 无贴图，渲染器回退 GWhiteTexture（与 MakeBox tint 填充同一条路径） */
static const FSlateResourceHandle& GetWhiteFillHandle()
{
	static FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(GetSolidBrush());
	return Handle;
}

/**
 * 耳切三角化（Ear Clipping）：把可能凹的简单多边形（顶点按序，顺时针或逆时针均可）
 * 拆成三角形组（每个三角形 3 个原始顶点索引）。
 * 失败（完全共线/自交/病态退化）返回 false，调用方静默跳过，只画边框。
 * O(n^3)，SCADA 多边形点数量级下足够。
 */
static bool TriangulatePolygonEarClipping(const TArray<FVector2D>& Vertices, TArray<int32>& OutTriangles)
{
	OutTriangles.Reset();
	const int32 n = Vertices.Num();
	if (n < 3)
	{
		return false;
	}

	// 去掉相邻重复点（含闭合列表首尾重复），避免退化三角形
	TArray<int32> Live;
	Live.Reserve(n);
	for (int32 i = 0; i < n; ++i)
	{
		if (Live.Num() == 0 || !Vertices[i].Equals(Vertices[Live.Last()], 0.01f))
		{
			Live.Add(i);
		}
	}
	if (Live.Num() > 1 && Vertices[Live[0]].Equals(Vertices[Live.Last()], 0.01f))
	{
		Live.RemoveAt(Live.Num() - 1);
	}
	const int32 m = Live.Num();
	if (m < 3)
	{
		return false;
	}

	// 有向面积 → 环绕方向（Area>0 = 逆时针）
	double Area = 0.0;
	for (int32 i = 0; i < m; ++i)
	{
		const FVector2D& A = Vertices[Live[i]];
		const FVector2D& B = Vertices[Live[(i + 1) % m]];
		Area += (double)A.X * (double)B.Y - (double)B.X * (double)A.Y;
	}
	if (FMath::Abs(Area) < 1e-6)
	{
		return false; // 完全共线退化
	}
	const double Sign = (Area > 0.0) ? 1.0 : -1.0;

	// 三点叉积（有向面积×2 的符号载体）
	auto CrossOf = [&](int32 ia, int32 ib, int32 ic) -> double
	{
		const FVector2D& A = Vertices[ia];
		const FVector2D& B = Vertices[ib];
		const FVector2D& C = Vertices[ic];
		return (double)(B.X - A.X) * (double)(C.Y - A.Y) - (double)(B.Y - A.Y) * (double)(C.X - A.X);
	};

	// 点在三角形内（含落在边上 → 保守地判“不是耳朵”，避免把边上的顶点挖掉）
	auto PointInTri = [&](int32 ip, int32 ia, int32 ib, int32 ic) -> bool
	{
		const FVector2D& P = Vertices[ip];
		const FVector2D& A = Vertices[ia];
		const FVector2D& B = Vertices[ib];
		const FVector2D& C = Vertices[ic];
		const double c1 = (double)(B.X - A.X) * (double)(P.Y - A.Y) - (double)(B.Y - A.Y) * (double)(P.X - A.X);
		const double c2 = (double)(C.X - B.X) * (double)(P.Y - B.Y) - (double)(C.Y - B.Y) * (double)(P.X - B.X);
		const double c3 = (double)(A.X - C.X) * (double)(P.Y - C.Y) - (double)(A.Y - C.Y) * (double)(P.X - C.X);
		// 三条有向边相对多边形同向 → P 在内部或边上
		return (c1 * Sign) >= 0.0 && (c2 * Sign) >= 0.0 && (c3 * Sign) >= 0.0;
	};

	// 迭代找耳朵：凸（叉积符号=多边形方向）且三角形内无其他顶点 → 输出并移除
	TArray<int32> LiveIdx = Live;
	while (LiveIdx.Num() > 3)
	{
		const int32 mNow = LiveIdx.Num();
		bool bFoundEar = false;
		for (int32 i = 0; i < mNow && !bFoundEar; ++i)
		{
			const int32 ia = LiveIdx[(i - 1 + mNow) % mNow];
			const int32 ib = LiveIdx[i];
			const int32 ic = LiveIdx[(i + 1) % mNow];

			// 凸耳朵判定（共线 c==0 不算耳朵，等邻居变化后再试）
			if (CrossOf(ia, ib, ic) * Sign <= 0.0)
			{
				continue;
			}

			bool bClear = true;
			for (int32 j = 0; j < mNow && bClear; ++j)
			{
				if (j == i || j == (i - 1 + mNow) % mNow || j == (i + 1) % mNow)
				{
					continue;
				}
				if (PointInTri(LiveIdx[j], ia, ib, ic))
				{
					bClear = false;
				}
			}

			if (bClear)
			{
				OutTriangles.Add(ia);
				OutTriangles.Add(ib);
				OutTriangles.Add(ic);
				LiveIdx.RemoveAt(i);
				bFoundEar = true;
			}
		}
		if (!bFoundEar)
		{
			return false; // 无可用耳朵（自交/严重退化）——静默失败
		}
	}

	// 剩余 3 个顶点构成最后一个三角形
	OutTriangles.Add(LiveIdx[0]);
	OutTriangles.Add(LiveIdx[1]);
	OutTriangles.Add(LiveIdx[2]);
	return true;
}

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

	TArray<FVector2D> Poly;
	Poly.SetNumUninitialized(RawPoints.Num());
	for (int32 i = 0; i < RawPoints.Num(); ++i)
	{
		Poly[i] = SnapToPixel(RawPoints[i]);
	}

	// 闭合点列：原始点按序 + 首点回接
	TArray<FVector2D> Closed;
	Closed.Reserve(Poly.Num() + 1);
	for (const FVector2D& P : Poly)
	{
		Closed.Add(P);
	}
	Closed.Add(Poly[0]);

	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	// 闪烁：相位为“闪烁色”时**只切填充色**到 FlashColor，边框保持本色（WinCC 语义：
	// 黑边白圆红闪 = 黑边红闪）；全局 1 秒换相，与线同一逻辑。FlashColor 透明度 0 即填充隐藏。
	// 翻转组（bFlashInverted）与正常组取色相反：正常相位显示闪烁色、闪烁相位显示正常色。
	const bool bShowFlash = bFlashEnabled.Get() && (IsFlashPhaseNormal() == bFlashInverted.Get());
	const FLinearColor BorderFinalColor = Color.Get() * InWidgetStyle.GetColorAndOpacityTint();
	const FLinearColor FillFinalColor = (bShowFlash ? FlashColor.Get() : FillColor.Get()) * InWidgetStyle.GetColorAndOpacityTint();

	const FPaintGeometry PaintGeom = AllottedGeometry.ToPaintGeometry();
	const float LineThickness = Thickness.Get();
	// MakeLines 的线宽不随几何体变换缩放（点位会），画布 Zoom（或 DPI 缩放）≠1 时手动乘累积变换缩放（与线同一规则）
	const float RenderScale = (float)(AllottedGeometry.LocalToAbsolute(FVector2D(1.0, 0.0)) - AllottedGeometry.LocalToAbsolute(FVector2D::ZeroVector)).Size();
	const float ScaledLineThickness = LineThickness * RenderScale;

	// 1) 填充（实心）：耳切三角化 + 一次 MakeCustomVerts 实心三角扇；三角化失败静默跳过（仍画边框）
	if (FillPattern.Get() == ESCADAFillPattern::Solid)
	{
		DrawPolygonFill(OutDrawElements, LayerId, AllottedGeometry, Closed, FillFinalColor, DrawEffects);
	}

	// 2) 闭合边框（**内画 + 显式更高绘制层**）：几何数据 = 外轮廓 = 视觉边界；
	// 填充画在 LayerId、边框整体抬到 LayerId+1（Slate 同层内 MakeCustomVerts 与
	// MakeLines/MakeBox 合批不保证提交顺序，显式分层是严格层序）。
	// Thickness=0 不画边框且图形不会"少一圈"。
	if (LineThickness > KINDA_SMALL_NUMBER)
	{
		++LayerId; // 边框层：严格高于填充层
		TArray<FVector2D> Ring;
		if (BuildOffsetContour(Poly, LineThickness * 0.5f, Ring))
		{
			if (LineStyle.Get() == ESCADALineStyle::Solid)
			{
				// 实心边框 = 环带四边形条带（不做耳切）：外环 = 原始轮廓（尖角精确到几何
				// 顶点），内环 = 内缩 T 的轮廓；逐边两个三角形（外边缘 i→j + 内边缘 j→i），
				// 相邻四边形共享边、零缝隙、无失败模式。注意**不能**把"外环正向 + 内环倒序"
				// 拼成单环多边形做耳切——单桥接的环带在闭合边那一侧根本没被围进多边形
				// （最后一段环带丢失）；描边方案在尖角处靠圆盘/斜接拐弯也盖不住角尖。
				TArray<FVector2D> InnerRing;
				if (BuildOffsetContour(Poly, LineThickness, InnerRing))
				{
					const int32 N = Poly.Num();
					TArray<FVector2D> BandVerts;
					BandVerts.Reserve(2 * N);
					for (int32 i = 0; i < N; ++i)
					{
						BandVerts.Add(Poly[i]);
					}
					for (int32 i = 0; i < N; ++i)
					{
						BandVerts.Add(InnerRing[i]);
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
					DrawTriangleList(OutDrawElements, LayerId, AllottedGeometry, BandVerts, BandIndices, BorderFinalColor, DrawEffects);
				}
			}
			else
			{
				// 虚线系：把尖角换成半径 线宽/2 的圆弧采样，让划线模式沿圆角流动。
				// 从首边中点 M 出发、回到 M 收尾：所有原始顶点都成为内部点，全部被圆角化
				// （BuildRoundedPolyline 不圆首/末点；M 与首边共线，无拐角）。
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
				TArray<FVector2D> DashPairs;
				TArray<bool> DashIsDot;
				BuildSegmentPoints(Smoothed, DashPairs, DashIsDot, 0.0f, 0.0f);
				for (int32 Seg = 0; 2 * Seg + 1 < DashPairs.Num(); ++Seg)
				{
					const FVector2D A = DashPairs[2 * Seg];
					const FVector2D B = DashPairs[2 * Seg + 1];
					if (DashIsDot.IsValidIndex(Seg) && DashIsDot[Seg])
					{
						// 点：严格 1:1 实心方块（边长 = 线宽），沿线段方向旋转（与线同一规则）
						const FVector2D Center = SnapToPixel((A + B) * 0.5);
						const FVector2D D = B - A;
						const float Angle = FMath::Atan2((float)D.Y, (float)D.X);
						const float S = LineThickness;
						const FGeometry DotGeom = AllottedGeometry.MakeChild(
							FVector2f(S, S),
							FSlateLayoutTransform(FVector2f((float)Center.X - S * 0.5f, (float)Center.Y - S * 0.5f)),
							FSlateRenderTransform(FQuat2f(Angle)),
							FVector2f(0.5f, 0.5f));
						FSlateDrawElement::MakeBox(OutDrawElements, LayerId, DotGeom.ToPaintGeometry(), &GetSolidBrush(), DrawEffects, BorderFinalColor);
					}
					else
					{
						// 划：独立两点线段（MakeLines 只连这两个点）
						TArray<FVector2D> Pair = { A, B };
						FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeom, Pair, DrawEffects, BorderFinalColor, /*bAntialias=*/true, ScaledLineThickness);
					}
				}
			}
		}
	}

	return LayerId;
}

void SSCADAPolygon::DrawTriangleList(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
	const TArray<FVector2D>& Vertices, const TArray<int32>& Indices, const FLinearColor& InColor, ESlateDrawEffect DrawEffects) const
{
	// 一次 MakeCustomVerts 实心三角扇：白色资源句柄回退（与 SSCADALine::AppendArrow 同一条路径），
	// 顶点位置在“绘制空间”（累积渲染变换后做像素对齐，与线体顶点同一规则），纯色由顶点 tint 决定。
	const FSlateRenderTransform& RT = AllottedGeometry.GetAccumulatedRenderTransform();
	const FColor Tint = InColor.ToFColor(true);
	TArray<FSlateVertex> Verts;
	Verts.Reserve(Indices.Num());
	for (int32 i = 0; i < Indices.Num(); ++i)
	{
		Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Enabled>(RT, UE::Slate::CastToVector2f(Vertices[Indices[i]]), FVector2f::ZeroVector, Tint));
	}
	TArray<SlateIndex> Indexes;
	Indexes.Reserve(Indices.Num());
	for (int32 i = 0; i < Indices.Num(); ++i)
	{
		Indexes.Add(i);
	}
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, GetWhiteFillHandle(), Verts, Indexes, nullptr, 0, 0, DrawEffects);
}

void SSCADAPolygon::DrawPolygonFill(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
	const TArray<FVector2D>& Vertices, const FLinearColor& InColor, ESlateDrawEffect DrawEffects) const
{
	TArray<int32> Triangles;
	if (!TriangulatePolygonEarClipping(Vertices, Triangles))
	{
		return; // 三角化失败：静默跳过，只画边框
	}
	DrawTriangleList(OutDrawElements, LayerId, AllottedGeometry, Vertices, Triangles, InColor, DrawEffects);
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
