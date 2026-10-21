// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector2D.h"
#include "Styling/SlateBrush.h"
#include "Rendering/SlateResourceHandle.h"
#include "Rendering/RenderingCommon.h"
#include "Framework/Application/SlateApplication.h"

/**
 * 笔画几何工具（Line/Polyline/Polygon 共享，避免复制漂移）。
 *
 * 设计（2026-10，drawio 机制调研后定型）：等宽笔画的实线线体不再用"逐段矩形/线段堆叠"，
 * 而是先构造**笔画包络**（中心线两侧各偏移半线宽的封闭多边形），再一次性三角化填色 +
 * 沿包络轮廓 AA 覆层。依据：Slate 无 path-stroke API，等价做法是自己实现 stroke 语义；
 * drawio 在自身渲染器表达不了变宽笔画时（mxArrowConnector）正是退化为"自算封闭轮廓 + 填充"。
 *
 * 拐角平滑走**中心线圆角替换**（drawio rounded 语义：切点 = min(R/tan(θ/2), 邻段半长)，
 * 而非 join 圆盘——join 半径只能等于半线宽，锐角下必成球且会在内侧多填出"鼓包"；
 * 中心线圆角半径与线宽无关，管道肘部观感自然，内外侧包络是同心圆弧，天然无过填。
 */
namespace SCADAStroke
{
	/** 像素对齐：四舍五入到就近整数坐标 */
	inline FVector2D SnapToPixel(const FVector2D& P)
	{
		return FVector2D(FMath::RoundToFloat(P.X), FMath::RoundToFloat(P.Y));
	}

	/** 无贴图纯色笔刷（画点样式的实心方块用） */
	inline const FSlateBrush& GetSolidBrush()
	{
		static FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		return Brush;
	}

	/** 纯色填充用的白色资源句柄：GetSolidBrush() 无贴图，渲染器回退 GWhiteTexture（与 MakeBox tint 填充同一条路径） */
	inline const FSlateResourceHandle& GetWhiteFillHandle()
	{
		static FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(GetSolidBrush());
		return Handle;
	}

	/**
	 * 耳切三角化（Ear Clipping）：把可能凹的简单多边形（顶点按序，顺/逆时针均可）
	 * 拆成三角形索引组。相邻重复点自动去除；失败（共线退化/自交/无耳朵）返回 false，
	 * 调用方静默跳过填充。续扫实现（移除耳朵后从前一位置继续，不整圈重启），
	 * 最坏 O(n^3)（逐点包含判定），SCADA 图元顶点量级下足够。
	 */
	inline bool TriangulateEarClipping(const TArray<FVector2D>& Vertices, TArray<int32>& OutTriangles)
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

		// 点在三角形内（含落在边上 → 保守判"不是耳朵"，避免把边上的顶点挖掉）
		auto PointInTri = [&](int32 ip, int32 ia, int32 ib, int32 ic) -> bool
		{
			const FVector2D& P = Vertices[ip];
			const FVector2D& A = Vertices[ia];
			const FVector2D& B = Vertices[ib];
			const FVector2D& C = Vertices[ic];
			const double c1 = (double)(B.X - A.X) * (double)(P.Y - A.Y) - (double)(B.Y - A.Y) * (double)(P.X - A.X);
			const double c2 = (double)(C.X - B.X) * (double)(P.Y - B.Y) - (double)(C.Y - B.Y) * (double)(P.X - B.X);
			const double c3 = (double)(A.X - C.X) * (double)(P.Y - C.Y) - (double)(A.Y - C.Y) * (double)(P.X - C.X);
			return (c1 * Sign) >= 0.0 && (c2 * Sign) >= 0.0 && (c3 * Sign) >= 0.0;
		};

		// 迭代找耳朵（续扫，经典耳切优化）：凸（叉积符号=多边形方向）且三角形内无其他顶点 →
		// 输出并移除。RemoveAt(i) 只改变 i-1、i 两处的邻接关系，因此下次从被移除位置的前一个
		// 下标继续检查（新 i 处即原 i+1，与 i-1 一起重检），不整圈重启；其余顶点本轮仍会被扫到
		//（它们的耳朵性只会因某次移除由假变真，延迟到扫回时检出，不会漏判）。连续检查满一圈
		//（NoEarCount 达到当前存活数）才判定无耳朵失败，终止性由计数器保证。
		TArray<int32> LiveIdx = Live;
		int32 ScanIdx = 0;
		int32 NoEarCount = 0;
		while (LiveIdx.Num() > 3)
		{
			const int32 mNow = LiveIdx.Num();
			const int32 i = ScanIdx;
			const int32 iPrev = (i - 1 + mNow) % mNow;
			const int32 iNext = (i + 1) % mNow;
			const int32 ia = LiveIdx[iPrev];
			const int32 ib = LiveIdx[i];
			const int32 ic = LiveIdx[iNext];

			bool bIsEar = CrossOf(ia, ib, ic) * Sign > 0.0;
			if (bIsEar)
			{
				for (int32 j = 0; j < mNow && bIsEar; ++j)
				{
					if (j == i || j == iPrev || j == iNext)
					{
						continue;
					}
					if (PointInTri(LiveIdx[j], ia, ib, ic))
					{
						bIsEar = false;
					}
				}
			}

			if (bIsEar)
			{
				OutTriangles.Add(ia);
				OutTriangles.Add(ib);
				OutTriangles.Add(ic);
				LiveIdx.RemoveAt(i);
				// 从被移除位置的前一个下标继续（环形取模；Num 变 3 时取模仍合法，循环随即退出）
				ScanIdx = (i - 1 + LiveIdx.Num()) % LiveIdx.Num();
				NoEarCount = 0;
			}
			else
			{
				ScanIdx = (i + 1) % mNow;
				if (++NoEarCount >= mNow)
				{
					return false; // 扫完一圈无可用耳朵（自交/严重退化）——静默失败
				}
			}
		}

		OutTriangles.Add(LiveIdx[0]);
		OutTriangles.Add(LiveIdx[1]);
		OutTriangles.Add(LiveIdx[2]);
		return true;
	}

	/**
	 * 三角形列表展开：把"顶点 + 三角形索引"一次性展开成逐三角形顶点列（OutTriVerts[i] =
	 * Vertices[Triangles[i]]）与恒等索引列（0..N-1）。MakeCustomVerts 的绘制侧本就是
	 * "顶点按索引展开 + 恒等索引"，展开结果随几何缓存复用，消除每帧的索引间接寻址与
	 * 恒等索引缓冲重建（几何脏标记缓存的配套工具）。
	 */
	inline void ExpandTriangleList(const TArray<FVector2D>& Vertices, const TArray<int32>& Triangles,
		TArray<FVector2D>& OutTriVerts, TArray<SlateIndex>& OutIdentityIndices)
	{
		const int32 M = Triangles.Num();
		OutTriVerts.SetNumUninitialized(M);
		for (int32 i = 0; i < M; ++i)
		{
			OutTriVerts[i] = Vertices[Triangles[i]];
		}
		OutIdentityIndices.SetNumUninitialized(M);
		for (int32 i = 0; i < M; ++i)
		{
			OutIdentityIndices[i] = static_cast<SlateIndex>(i);
		}
	}

	/**
	 * 等宽笔画包络：对中心线（应为已完成重合点清理/拐角圆弧替换的稠密折线）两侧各偏移
	 * HalfWidth，生成**单一封闭多边形**（左链正向 + 右链逆向，端点平齐 = butt 端帽）。
	 *
	 * 顶点 join：两侧都用相邻偏移线**交点**（内外统一公式）；外侧交点距离超过
	 * HalfWidth×10（≈SVG miterlimit 语义）或近共线段时退化为 bevel（该顶点两侧偏移点）。
	 * 圆滑来自调用前对中心线做的拐角圆弧替换（BuildRoundedPolyline，drawio rounded 语义），
	 * 本函数不感知圆角——偏移后的圆弧自然是同心圆弧。
	 * 失败（有效点 <2 或 HalfWidth≤0）返回 false。
	 */
	inline bool BuildStrokeOutline(const TArray<FVector2D>& Centerline, float HalfWidth, TArray<FVector2D>& OutLoop)
	{
		OutLoop.Reset();
		if (HalfWidth <= 0.0f)
		{
			return false;
		}

		// 重合点清理（偏移线方向未定义）
		TArray<FVector2D> C;
		C.Reserve(Centerline.Num());
		for (const FVector2D& P : Centerline)
		{
			if (C.Num() == 0 || !C.Last().Equals(P, 1e-3f))
			{
				C.Add(P);
			}
		}
		const int32 n = C.Num();
		if (n < 2)
		{
			return false;
		}

		// 各段单位方向（退化段兜底沿用前向；首段退化取 X 轴）
		TArray<FVector2D> Dir;
		Dir.SetNumUninitialized(n - 1);
		for (int32 i = 0; i < n - 1; ++i)
		{
			FVector2D D = (C[i + 1] - C[i]).GetSafeNormal();
			if (D.IsNearlyZero())
			{
				D = (i > 0) ? Dir[i - 1] : FVector2D(1.0, 0.0);
			}
			Dir[i] = D;
		}

		// 单侧链（s=+1 左 / s=-1 右），沿中心线正向构建
		auto BuildSide = [&](float s, TArray<FVector2D>& Out)
		{
			auto OffsetNormal = [&](int32 SegIdx) -> FVector2D
			{
				// 左法线 × 侧向 × 半宽（右侧 = 左法线取反，s=-1）
				return FVector2D(-Dir[SegIdx].Y, Dir[SegIdx].X) * (s * HalfWidth);
			};
			Out.Add(C[0] + OffsetNormal(0));
			for (int32 i = 1; i < n - 1; ++i)
			{
				const FVector2D D1 = Dir[i - 1];
				const FVector2D D2 = Dir[i];
				const FVector2D O1 = C[i] + OffsetNormal(i - 1);
				const FVector2D O2 = C[i] + OffsetNormal(i);
				const float Den = D1.X * D2.Y - D1.Y * D2.X;
				if (FMath::Abs(Den) > 1e-4f)
				{
					// 两偏移线交点：O1 + t·D1 = O2 + u·D2
					const FVector2D W = O2 - O1;
					const float T = (W.X * D2.Y - W.Y * D2.X) / Den;
					const FVector2D P = O1 + D1 * T;
					const float Dist = (float)(P - C[i]).Size();
					if (Dist <= HalfWidth * 10.0f && FMath::IsFinite(Dist))
					{
						Out.Add(P);
						continue;
					}
				}
				// 近共线/超限/平行退化：bevel（两侧偏移点，外侧交点退平角）
				Out.Add(O1);
				Out.Add(O2);
			}
			Out.Add(C[n - 1] + OffsetNormal(n - 2));
		};

		TArray<FVector2D> Left;
		TArray<FVector2D> Right;
		BuildSide(1.0f, Left);
		BuildSide(-1.0f, Right);
		OutLoop = MoveTemp(Left);
		for (int32 i = Right.Num() - 1; i >= 0; --i)
		{
			OutLoop.Add(Right[i]);
		}
		return OutLoop.Num() >= 3;
	}
}
