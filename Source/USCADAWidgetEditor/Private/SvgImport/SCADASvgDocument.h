// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * SVG 文档中间表示（SCADA 图元导入用，纯数据、无 UMG 依赖）。
 * P0 支持子集：rect / circle / ellipse / line / polyline / polygon / g（分组，仅 rotate 变换），
 * 样式 fill / stroke / stroke-width / fill-opacity / stroke-opacity（沿组继承），
 * defs 内 linearGradient / radialGradient 解析为加权平均纯色（真渐变排 P1）。
 * 坐标一律为 viewBox 空间（左上角原点、Y 向下，与 SCADA 画布设计坐标同构）。
 */

/** SVG 元素类型（P0 支持的子集） */
enum class ESCADASvgNodeKind : uint8
{
	Group,
	Rect,
	Circle,
	Ellipse,
	Line,
	Polyline,
	Polygon
};

/** 表现样式（组继承已解析）：Fill/Stroke 未设置 = SVG 语义 none（不画） */
struct FSCADASvgStyle
{
	/** 填充色（SVG 缺省 = 纯黑，解析时按规范填入；fill="none" → Reset） */
	TOptional<FLinearColor> Fill;
	/** 描边色（SVG 缺省 = none） */
	TOptional<FLinearColor> Stroke;
	/** 描边宽度 */
	float StrokeWidth = 1.0f;
};

/** SVG 元素节点（分组形成树） */
struct FSCADASvgNode
{
	ESCADASvgNodeKind Kind = ESCADASvgNodeKind::Group;
	/** SVG id 属性（用于命名生成控件；可为空） */
	FString Id;
	FSCADASvgStyle Style;

	// —— 几何（viewBox 坐标；无旋转变换的组平移已烘入；含义按 Kind）——
	/** Rect: 左上角 / Circle、Ellipse: 圆心 / Line: 起点 */
	FVector2D P1 = FVector2D::ZeroVector;
	/** Rect: 宽高 / Ellipse: (rx, ry) / Line: 终点 */
	FVector2D P2 = FVector2D::ZeroVector;
	/** Rect: 圆角半径 (rx, ry)（ry 缺省 = rx；0 = 直角） */
	FVector2D CornerRadius = FVector2D::ZeroVector;
	/** Circle: 半径 */
	float Radius = 0.0f;
	/** Polyline / Polygon: 顶点列 */
	TArray<FVector2D> Points;

	// —— 分组（Kind=Group，仅含 rotate 变换的组才保留为节点；无旋转组已在解析层扁平化）——
	/** 组平移（transform 列表中全部 translate 与父链平移的累计，viewBox 空间绝对值） */
	FVector2D Translation = FVector2D::ZeroVector;
	bool bHasRotation = false;
	float RotationAngle = 0.0f;
	/** 旋转中心（组容器本地空间 = 原始 SVG 坐标，不含组平移与 viewBox 修正） */
	FVector2D RotationCenter = FVector2D::ZeroVector;

	TArray<FSCADASvgNode> Children;
};

/** 解析结果 */
struct FSCADASvgDocument
{
	/** viewBox 尺寸（无 viewBox 时用 width/height 兜底，再缺省 100×100） */
	FVector2D ViewBoxSize = FVector2D(100.0, 100.0);
	TArray<FSCADASvgNode> Roots;

	/** 从 SVG 文本解析（FXmlFile）。不支持的元素/属性跳过并记 Warning 日志（LogSCADASvg）。
	 *  未知命名空间的元素/属性（inkscape:/sodipodi: 等厂商扩展）按 SVG 规范静默忽略。
	 *  返回 false = XML 本身无法解析。 */
	static bool Parse(const FString& SvgText, FSCADASvgDocument& OutDocument);
};
