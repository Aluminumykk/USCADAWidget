// Copyright Pr_UEDraw. All Rights Reserved.

#include "Slate/SSCADALine.h"

#include "Slate/SCADAStrokeGeometry.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateResourceHandle.h"
#include "Styling/SlateBrush.h"

// 像素对齐 / 纯色笔刷 / 白色填充句柄已收进共享头 Slate/SCADAStrokeGeometry.h（SCADAStroke 命名空间）

/** 三角形列表实心填充（笔画包络用）：一次 MakeCustomVerts；顶点**不取整**（包络是亚像素精确
 *  轮廓，取整会把肘部圆弧量化成台阶；边缘柔化由沿包络的 DrawContourAA 覆层负责） */
static void DrawTriFilled(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
	const TArray<FVector2D>& Vertices, const TArray<int32>& Triangles, const FLinearColor& InColor, ESlateDrawEffect DrawEffects)
{
	const FSlateRenderTransform& RT = AllottedGeometry.GetAccumulatedRenderTransform();
	const FColor Tint = InColor.ToFColor(true);
	TArray<FSlateVertex> Verts;
	Verts.Reserve(Triangles.Num());
	for (int32 i = 0; i < Triangles.Num(); ++i)
	{
		Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, UE::Slate::CastToVector2f(Vertices[Triangles[i]]), FVector2f::ZeroVector, Tint));
	}
	TArray<SlateIndex> Indexes;
	Indexes.Reserve(Triangles.Num());
	for (int32 i = 0; i < Triangles.Num(); ++i)
	{
		Indexes.Add(static_cast<SlateIndex>(i));
	}
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, SCADAStroke::GetWhiteFillHandle(), Verts, Indexes, nullptr, 0, 0, DrawEffects);
}

void SSCADALine::Construct(const FArguments& InArgs)
{
	Points = InArgs._Points;
	Color = InArgs._Color;
	Thickness = InArgs._Thickness;
	LineStyle = InArgs._LineStyle;
	LineStartStyle = InArgs._LineStartStyle;
	LineEndStyle = InArgs._LineEndStyle;
	LineCap = InArgs._LineCap;
	bFlashEnabled = InArgs._bFlashEnabled;
	FlashColor = InArgs._FlashColor;
	bFlashInverted = InArgs._bFlashInverted;
	DashLength = InArgs._DashLength.Get();
	GapLength = InArgs._GapLength.Get();
	ArrowSize = InArgs._ArrowSize.Get();

	// 闪烁需要在相位翻转时重绘
	SetCanTick(true);
}

bool SSCADALine::IsFlashPhaseNormal()
{
	// 全局统一定时器：所有闪烁图元使用同一个墙钟相位，1 秒换相，天然同步
	return (FMath::FloorToInt64(FPlatformTime::Seconds()) % 2) == 0;
}

void SSCADALine::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (ExternalTickCallback)
	{
		ExternalTickCallback();
	}

	if (bFlashEnabled.Get())
	{
		const bool bPhaseNormal = IsFlashPhaseNormal();
		if (bPhaseNormal != bLastFlashPhaseNormal)
		{
			bLastFlashPhaseNormal = bPhaseNormal;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
}

void SSCADALine::SetPoints(const TAttribute<TArray<FVector2D>>& InPoints) { Points = InPoints; }
void SSCADALine::SetColor(const TAttribute<FLinearColor>& InColor) { Color = InColor; }
void SSCADALine::SetThickness(const TAttribute<float>& InThickness) { Thickness = InThickness; }
void SSCADALine::SetLineStyle(const TAttribute<ESCADALineStyle>& InStyle) { LineStyle = InStyle; }
void SSCADALine::SetLineStartStyle(const TAttribute<ESCADALineEndStyle>& InStyle) { LineStartStyle = InStyle; }
void SSCADALine::SetLineEndStyle(const TAttribute<ESCADALineEndStyle>& InStyle) { LineEndStyle = InStyle; }
void SSCADALine::SetLineCap(const TAttribute<ESCADALineCap>& InCap) { LineCap = InCap; }
void SSCADALine::SetFlashEnabled(const TAttribute<bool>& bInEnabled) { bFlashEnabled = bInEnabled; }
void SSCADALine::SetFlashColor(const TAttribute<FLinearColor>& InColor) { FlashColor = InColor; }
void SSCADALine::SetFlashInverted(const TAttribute<bool>& bInInverted) { bFlashInverted = bInInverted; }

int32 SSCADALine::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const TArray<FVector2D>& RawPolyline = Points.Get();
	if (RawPolyline.Num() < 2)
	{
		return LayerId;
	}

	TArray<FVector2D> Polyline;
	Polyline.SetNumUninitialized(RawPolyline.Num());
	for (int32 i = 0; i < RawPolyline.Num(); ++i)
	{
		Polyline[i] = SCADAStroke::SnapToPixel(RawPolyline[i]);
	}

	// 拐角处理两条路线（实线包络法内部也复用圆弧替换，见 SCADAStrokeGeometry.h 设计说明）：
	// 实线 = 中心线拐角圆弧替换（默认半径，drawio rounded 语义）→ 两侧偏移包络 → 一次填色 +
	//   沿包络覆层（见下方实线分支）。不用"圆弧微段描边"：FLineBuilder 对短微段的斜接判定
	//   必然失败，粗线下退化为"矩形盖帽+断裂重开"留一串楔形缺口；包络法无描边，无此问题。
	// 虚线 = 以 线宽/2 为半径把尖角替换成圆弧采样，让划线模式沿圆角流动
	//   （虚线各段本就独立成对绘制，段间没有连接问题）。
	if (LineStyle.Get() != ESCADALineStyle::Solid)
	{
		TArray<FVector2D> Smoothed;
		BuildRoundedPolyline(Polyline, Thickness.Get() * 0.5f, Smoothed);
		Polyline = MoveTemp(Smoothed);
	}

	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	// 闪烁：相位为“闪烁色”时整条线（含箭头、线端）换色；FlashColor 透明度 0 即隐藏。
	// 翻转组（bFlashInverted）与正常组取色相反：正常相位显示闪烁色、闪烁相位显示正常色。
	const bool bShowFlash = bFlashEnabled.Get() && (IsFlashPhaseNormal() == bFlashInverted.Get());
	const FLinearColor BaseColor = bShowFlash ? FlashColor.Get() : Color.Get();
	const FLinearColor FinalColor = BaseColor * InWidgetStyle.GetColorAndOpacityTint();
	const FPaintGeometry PaintGeom = AllottedGeometry.ToPaintGeometry();
	const float LineThickness = Thickness.Get();
	// MakeLines 的线宽不随几何体变换缩放（点位会），而圆顶/点方块/箭头都是按几何体绘制的，
	// 画布 Zoom（或 DPI 缩放）≠1 时线身宽度会与它们脱节；这里手动乘累积变换缩放。
	const float RenderScale = (float)(AllottedGeometry.LocalToAbsolute(FVector2D(1.0, 0.0)) - AllottedGeometry.LocalToAbsolute(FVector2D::ZeroVector)).Size();
	const float ScaledLineThickness = LineThickness * RenderScale;

	// 注意：MakeLines 会把传入的所有点连成一条折线（A,B,C → A-B-C）。
	// 画不相连的独立线段必须每两个点调用一次 MakeLines。
	// 复用缓冲（MakeLines/DrawContourAA 只接受 const TArray&，无 TArrayView 重载）：
	// Reset(N) 首段分配一次、后续只覆写元素，消除逐段 initializer-list 堆分配。
	TArray<FVector2D> SegPair;
	TArray<FVector2D> DotLoopBuf;
	auto DrawSegment = [&](const FVector2D& A, const FVector2D& B, float SegThickness)
	{
		SegPair.Reset(2);
		SegPair.Add(A);
		SegPair.Add(B);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeom, SegPair, DrawEffects, FinalColor, /*bAntialias=*/true, SegThickness);
	};

	// 点样式的点：严格 1:1 的实心方块（边长 = 线宽），沿线段方向旋转
	auto DrawDot = [&](const FVector2D& A, const FVector2D& B)
	{
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
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, DotGeom.ToPaintGeometry(), &SCADAStroke::GetSolidBrush(), DrawEffects | ESlateDrawEffect::NoPixelSnapping, FinalColor);

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
		DrawContourAA(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), DotLoopBuf, FinalColor, DrawEffects);
	};

	// 箭头尺寸随线宽自适应：长度至少为 4 倍线宽，半宽为长度的一半。
	const ESCADALineEndStyle StartStyle = LineStartStyle.Get();
	const ESCADALineEndStyle EndStyle = LineEndStyle.Get();
	const float EffectiveArrowSize = FMath::Max(ArrowSize, LineThickness * 4.0f);
	const float TrimStart = (StartStyle == ESCADALineEndStyle::Arrow) ? EffectiveArrowSize : 0.0f;
	const float TrimEnd = (EndStyle == ESCADALineEndStyle::Arrow) ? EffectiveArrowSize : 0.0f;

	if (LineStyle.Get() == ESCADALineStyle::Solid)
	{
		// 有箭头的端把线体缩短一个箭头长度，再往回探入 ArrowOverlap：
		// 线尾钻到箭头底边内侧（箭头不透明且后绘制，盖住重叠区），
		// 避免"恰好相接"时接缝露缝。
		const float ArrowOverlap = FMath::Max(2.0f, LineThickness * 0.25f);
		TArray<FVector2D> Body = Polyline;
		if (TrimStart > 0.f) { Body[0] = Polyline[0] + (Polyline[1] - Polyline[0]).GetSafeNormal() * FMath::Max(TrimStart - ArrowOverlap, 0.0f); }
		if (TrimEnd > 0.f) { const int32 N = Body.Num(); Body[N - 1] = Polyline.Last() + (Polyline[Polyline.Num() - 2] - Polyline.Last()).GetSafeNormal() * FMath::Max(TrimEnd - ArrowOverlap, 0.0f); }

		// 线体 = **笔画包络，一次填色**（不是"逐段拼贴 + join 补偿"的堆叠）：
		// ① 中心线先做拐角小圆角替换（drawio rounded 语义：切点 = min(R/tan(θ/2), 邻段半长)，
		//    圆角半径与线宽无关——这就是"小圆角过渡"的正确来源；join 圆盘方案被否：其半径只能
		//    = 半线宽，锐角必成球、内侧还会多填鼓包，即此前"突兀/生硬"两难之根源）；
		// ② 两侧各偏移 ±半线宽构造**封闭包络多边形**（顶点 join = 相邻偏移线交点，外侧交点
		//    超 10×半宽退化 bevel，≈SVG miterlimit 语义；端点 butt 平齐）；
		// ③ 包络一次耳切三角化填色 + 沿包络 3px 轮廓覆层柔化边缘。
		// 依据：Slate 无 path-stroke API，只能自实现 stroke 语义；drawio 渲染器表达不了变宽
		// 笔画时（mxArrowConnector）同样退化为"自算封闭轮廓 + 填充"，路线同构。
		// LineCap round / 箭头不参与包络：round 帽仍以端点圆盘叠加（butt 包络 ∪ 圆盘 = 圆帽），
		// 箭头端先裁中心线、后画箭头盖根——与 drawio 同序，避免两层 AA 边缘混色接缝。
		{
			static constexpr float CornerArcRadius = 10.0f; // 同 drawio LINE_ARCSIZE/2；将来可做成"圆角半径"暴露属性（drawio arcSize 语义）
			TArray<FVector2D> RoundedBody;
			BuildRoundedPolyline(Body, CornerArcRadius, RoundedBody);
			TArray<FVector2D> Envelope;
			if (LineThickness > KINDA_SMALL_NUMBER && SCADAStroke::BuildStrokeOutline(RoundedBody, LineThickness * 0.5f, Envelope))
			{
				TArray<int32> EnvelopeTris;
				if (SCADAStroke::TriangulateEarClipping(Envelope, EnvelopeTris))
				{
					DrawTriFilled(OutDrawElements, LayerId, AllottedGeometry, Envelope, EnvelopeTris, FinalColor, DrawEffects);
				}
				// 填充可能因自交失败（极端尖角×超厚线），仍沿包络描边覆层，保证轮廓不缺
				TArray<FVector2D> EnvelopeClosed = Envelope;
				EnvelopeClosed.Add(Envelope[0]);
				DrawContourAA(OutDrawElements, LayerId, PaintGeom, EnvelopeClosed, FinalColor, DrawEffects);
			}
		}
	}
	else
	{
		// 虚线/点线/点划线/双点划线：逐段独立绘制；“点”段画 1:1 旋转实心方块。
		// 有箭头的端裁剪掉箭头长度，避免点/划与箭头叠成平头。
		TArray<FVector2D> DashPairs;
		TArray<bool> DashIsDot;
		BuildSegmentPoints(Polyline, DashPairs, DashIsDot, TrimStart, TrimEnd);
		for (int32 Seg = 0; 2 * Seg + 1 < DashPairs.Num(); ++Seg)
		{
			if (DashIsDot.IsValidIndex(Seg) && DashIsDot[Seg])
			{
				DrawDot(DashPairs[2 * Seg], DashPairs[2 * Seg + 1]);
			}
			else
			{
				DrawSegment(DashPairs[2 * Seg], DashPairs[2 * Seg + 1], ScaledLineThickness);
			}
		}
	}

	// 线端装饰：起点/终点独立设置（实心箭头）。
	// 箭头用单个实心三角形（MakeCustomVerts，白色资源 tint = FinalColor）填充。
	if (StartStyle == ESCADALineEndStyle::Arrow)
	{
		const FVector2D DirIn = (Polyline[1] - Polyline[0]).GetSafeNormal();
		AppendArrow(OutDrawElements, LayerId, AllottedGeometry, Polyline[0], DirIn, EffectiveArrowSize, DrawEffects, FinalColor);
	}
	if (EndStyle == ESCADALineEndStyle::Arrow)
	{
		const FVector2D DirIn = (Polyline[Polyline.Num() - 2] - Polyline.Last()).GetSafeNormal();
		AppendArrow(OutDrawElements, LayerId, AllottedGeometry, Polyline.Last(), DirIn, EffectiveArrowSize, DrawEffects, FinalColor);
	}

	// 线端形状：圆形时在端点画以端点为圆心的实心圆盘（单个 MakeBox + 全圆圆角矩形笔刷）。
	// 与箭头互斥：该端已是箭头则不画圆顶（两者叠加会很奇怪）。
	if (LineCap.Get() == ESCADALineCap::Round)
	{
		if (StartStyle != ESCADALineEndStyle::Arrow)
		{
			AppendRoundCap(OutDrawElements, LayerId, AllottedGeometry, Polyline[0], DrawEffects, FinalColor);
		}
		if (EndStyle != ESCADALineEndStyle::Arrow)
		{
			AppendRoundCap(OutDrawElements, LayerId, AllottedGeometry, Polyline.Last(), DrawEffects, FinalColor);
		}
	}

	return LayerId;
}

FVector2D SSCADALine::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	FVector2D Max(1.0, 1.0);
	for (const FVector2D& P : Points.Get())
	{
		Max = FVector2D::Max(Max, SCADAStroke::SnapToPixel(P));
	}
	return Max;
}

void SSCADALine::BuildSegmentPoints(const TArray<FVector2D>& Polyline, TArray<FVector2D>& OutSegments, TArray<bool>& OutIsDot, float TrimStart, float TrimEnd) const
{
	OutSegments.Reset();
	OutIsDot.Reset();

	// 线型 → 交替的“画/空”长度模式（点 = 线宽长度）；PatternDot 标记该位是否为“点”。
	// 长度值依赖运行时 Dash/Gap/Dot，填入栈缓冲（最多 6 槽，零堆分配）；
	// 点/划标记每样式固定，用 static const 表（Slate 绘制单线程，函数内 static const 初始化自 C++11 起线程安全）。
	const float Dot = FMath::Max(Thickness.Get(), 1.0f);
	const float Dash = DashLength;
	const float Gap = GapLength;

	static const bool DotFlagsDashed[]     = { false, false };
	static const bool DotFlagsDotted[]     = { true,  false };
	static const bool DotFlagsDashDot[]    = { false, false, true,  false };
	static const bool DotFlagsDashDotDot[] = { false, false, true,  false, true, false };

	float Pattern[6];
	const bool* PatternDot = nullptr;
	int32 PatternNum = 0;
	switch (LineStyle.Get())
	{
	case ESCADALineStyle::Dashed:
		Pattern[0] = Dash; Pattern[1] = Gap;
		PatternDot = DotFlagsDashed; PatternNum = 2;
		break;
	case ESCADALineStyle::Dotted:
		Pattern[0] = Dot; Pattern[1] = Gap;
		PatternDot = DotFlagsDotted; PatternNum = 2;
		break;
	case ESCADALineStyle::DashDot:
		Pattern[0] = Dash; Pattern[1] = Gap; Pattern[2] = Dot; Pattern[3] = Gap;
		PatternDot = DotFlagsDashDot; PatternNum = 4;
		break;
	case ESCADALineStyle::DashDotDot:
		Pattern[0] = Dash; Pattern[1] = Gap; Pattern[2] = Dot; Pattern[3] = Gap; Pattern[4] = Dot; Pattern[5] = Gap;
		PatternDot = DotFlagsDashDotDot; PatternNum = 6;
		break;
	default: return;
	}

	// 计算折线总长，用于首/尾裁剪（箭头区域）
	float TotalLen = 0.f;
	for (int32 i = 0; i + 1 < Polyline.Num(); ++i)
	{
		TotalLen += FVector2D::Distance(Polyline[i], Polyline[i + 1]);
	}

	float Cumulative = 0.f;
	for (int32 i = 0; i + 1 < Polyline.Num(); ++i)
	{
		const FVector2D A = Polyline[i];
		const FVector2D B = Polyline[i + 1];

		const float Len = FVector2D::Distance(A, B);
		if (Len <= SMALL_NUMBER)
		{
			continue;
		}
		const FVector2D Dir = (B - A) / Len;

		float Dist = 0.f;
		int32 PatternIdx = 0;
		while (Dist < Len)
		{
			const float Step = Pattern[PatternIdx];
			const float Next = FMath::Min(Dist + Step, Len);
			if ((PatternIdx & 1) == 0) // 偶数位 = 画线，奇数位 = 间隔
			{
				// 段中点落在首/尾裁剪区内则跳过（让位给箭头）
				const float SegMid = Cumulative + (Dist + Next) * 0.5f;
				if (SegMid >= TrimStart && SegMid <= TotalLen - TrimEnd)
				{
					OutSegments.Add(A + Dir * Dist);
					OutSegments.Add(A + Dir * Next);
					OutIsDot.Add(PatternDot[PatternIdx]);
				}
			}
			Dist = Next;
			PatternIdx = (PatternIdx + 1) % PatternNum;
		}
		Cumulative += Len;
	}
}

void SSCADALine::BuildRoundedPolyline(const TArray<FVector2D>& In, float Radius, TArray<FVector2D>& Out) const
{
	Out.Reset();
	if (Radius <= 0.0f || In.Num() < 3)
	{
		Out = In;
		return;
	}

	Out.Add(In[0]);
	for (int32 i = 1; i + 1 < In.Num(); ++i)
	{
		const FVector2D& V = In[i];
		const FVector2D D1 = V - In[i - 1];
		const FVector2D D2 = In[i + 1] - V;
		const float Len1 = D1.Size();
		const float Len2 = D2.Size();
		if (Len1 <= SMALL_NUMBER || Len2 <= SMALL_NUMBER)
		{
			// 相邻点重合：无法定方向，保持原顶点
			Out.Add(V);
			continue;
		}

		const FVector2D d1 = -D1 / Len1; // 顶点 → 前驱 的单位方向（D1 指向前进方向，需取反）
		const FVector2D d2 = D2 / Len2; // 顶点 → 后继 的单位方向
		const float CosHalf = FVector2D::DotProduct(d1, d2);

		// 退化：近乎共线（cos≈-1）或近乎掉头（cos≈+1），该顶点保持尖角原样输出
		if (CosHalf <= -0.999f || CosHalf >= 0.999f)
		{
			Out.Add(V);
			continue;
		}

		// 切点距离：L = R / tan(θ/2)，θ 为两邻边夹角（tan(θ/2) = sqrt((1-cos)/(1+cos))），
		// 再夹制到两侧邻边各不超过一半，避免切点越过头/尾
		const float TanHalf = FMath::Sqrt(FMath::Max(0.0f, (1.0f - CosHalf) / (1.0f + CosHalf)));
		const float L = FMath::Min(Radius / TanHalf, FMath::Min(Len1, Len2) * 0.5f);
		if (L <= SMALL_NUMBER)
		{
			Out.Add(V);
			continue;
		}

		const FVector2D T1 = V + d1 * L; // 入边切点
		const FVector2D T2 = V + d2 * L; // 出边切点

		// T1→T2 用二次贝塞尔（控制点 = 原顶点）近似半径 R 的圆弧，采样成 K 段折线；
		// 采样按约 1px 一段——该点列既供虚线划段生成，也供实线笔画包络偏移（包络外侧圆弧
		// 半径 = R+T/2 更大，2px/段会在肘部显出可感折角，1px/段配合 3px 覆层完全平滑）
		const float ArcLen = 2.0f * L;
		const int32 K = FMath::Max(6, FMath::CeilToInt(ArcLen));
		Out.Add(T1);
		for (int32 s = 1; s < K; ++s)
		{
			const float T = (float)s / (float)K;
			const float U = 1.0f - T;
			Out.Add(T1 * (U * U) + V * (2.0f * U * T) + T2 * (T * T));
		}
		Out.Add(T2);
	}
	Out.Add(In.Last());
}

void SSCADALine::AppendRoundCap(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry, const FVector2D& Tip, ESlateDrawEffect DrawEffects, const FLinearColor& InColor) const
{
	// 圆形线端：以端点为圆心的实心圆盘（直径 = 线宽），一次 MakeBox 用全圆圆角矩形笔刷绘制。
	const float Radius = FMath::Max(Thickness.Get(), 1.0f) * 0.5f;
	const FVector2D Center = SCADAStroke::SnapToPixel(Tip);
	const float S = Radius * 2.0f;
	const FGeometry CapGeom = AllottedGeometry.MakeChild(
		FVector2f(S, S),
		FSlateLayoutTransform(FVector2f((float)Center.X - Radius, (float)Center.Y - Radius)),
		FSlateRenderTransform(),
		FVector2f(0.5f, 0.5f));
	// 全圆圆角矩形笔刷：四角半径 = 边长/2，填充区即整圆；填充色由 MakeBox 的 tint（InColor）决定。
	FSlateRoundedBoxBrush CapBrush(FLinearColor::White, Radius);
	// NoPixelSnapping：圆顶/连接盘在缩放非整数比例下同样会被 Box 四角取整破坏亚像素位置
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, CapGeom.ToPaintGeometry(), &CapBrush, DrawEffects | ESlateDrawEffect::NoPixelSnapping, InColor);
}

void SSCADALine::AppendArrow(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry, const FVector2D& Tip, const FVector2D& DirIn, float InSize, ESlateDrawEffect DrawEffects, const FLinearColor& InColor) const
{
	if (DirIn.IsNearlyZero())
	{
		return;
	}
	// 实心箭头：单个三角形，顶点=Tip，底边=Base±Wing，一次 MakeCustomVerts 实心填充
	const FVector2D N = DirIn.GetSafeNormal();
	const FVector2D Perp(-N.Y, N.X);
	const FVector2D Base = Tip + N * InSize;
	const FVector2D Wing = Perp * (InSize * 0.5f);

	// 顶点位置在“绘制空间”（累积渲染变换后做像素对齐，与线体顶点同一规则）
	const FSlateRenderTransform& RT = AllottedGeometry.GetAccumulatedRenderTransform();
	const FColor Tint = InColor.ToFColor(true);
	TArray<FSlateVertex> Verts;
	Verts.Reserve(3);
	Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Enabled>(RT, UE::Slate::CastToVector2f(Tip), FVector2f::ZeroVector, Tint));
	Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Enabled>(RT, UE::Slate::CastToVector2f(Base + Wing), FVector2f::ZeroVector, Tint));
	Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Enabled>(RT, UE::Slate::CastToVector2f(Base - Wing), FVector2f::ZeroVector, Tint));
	TArray<SlateIndex> Indexes = { 0, 1, 2 };
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, SCADAStroke::GetWhiteFillHandle(), Verts, Indexes, nullptr, 0, 0, DrawEffects);

	// 箭头轮廓抗锯齿覆层（CustomVerts 三角无 AA；见 DrawContourAA 说明）。
	// 复用缓冲：函数内 static（Slate 绘制单线程，static 初始化自 C++11 起线程安全，
	// 与 SCADAStroke::GetSolidBrush 的 static 笔刷同一先例）——Reset(4) 后跨调用只覆写，
	// 消除每次调用的 4 元素 initializer-list 堆分配（普通局部 TArray 仍会每次分配，不解决问题）。
	static TArray<FVector2D> ArrowLoopBuf;
	ArrowLoopBuf.Reset(4);
	ArrowLoopBuf.Add(Tip);
	ArrowLoopBuf.Add(Base + Wing);
	ArrowLoopBuf.Add(Base - Wing);
	ArrowLoopBuf.Add(Tip);
	DrawContourAA(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), ArrowLoopBuf, InColor, DrawEffects);
}

void SSCADALine::DrawContourAA(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FPaintGeometry& PaintGeom,
	const TArray<FVector2D>& ClosedLoop, const FLinearColor& InColor, ESlateDrawEffect DrawEffects) const
{
	if (ClosedLoop.Num() < 2 || InColor.ToFColor(true).A == 0)
	{
		return;
	}
	// 厚度按屏幕像素语义（5.7 的 MakeLines thickness = 屏幕像素，内部除 scale）：3px =
	// 引擎保证的两侧各 1px 渐变 + 中间 1px 实心核，正好覆盖被覆图元边缘的 ±1px 像素量化
	// 区；1px 宽（两个软边、零实芯）盖不住硬台阶，等于没覆。屏幕宽度固定 → 任何缩放下
	// 柔边外观一致。
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeom, ClosedLoop, DrawEffects, InColor, /*bAntialias=*/true, 3.0f);
}
