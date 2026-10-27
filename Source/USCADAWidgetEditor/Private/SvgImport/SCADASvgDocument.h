// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * SVG 文档中间表示（SCADA 图元导入用，纯数据、无 UMG 依赖）。
 * 支持子集：rect（含 rx/ry 圆角）/ circle / ellipse / line / polyline / polygon /
 * path（d 全命令 M/L/H/V/C/S/Q/T/A/Z，贝塞尔与椭圆弧导入期采样折线化，闭合或有填充 →
 * 多边形、仅描边开口 → 折线，一条 path 可产多个图元）/ g（分组的 transform 只要能把图元模型画得动，
 *  一律精确换算进几何、不留嵌套画布：translate / scale / 镜像 scale(-1,1) / 对角 matrix / 90° 整数倍
 *  旋转对任何图元都无损；任意角度旋转在子树只含点列类图元（path/polyline/polygon/line、可展开成
 *  四点多边形的无圆角 rect、等比下的圆）时同样精确 —— Visio 的整页旋转、左右对称部件与斜放管件
 *  就全靠这三类。只有 skew、奇异矩阵，或子树里出现椭圆 / 带圆角矩形 / 非等比下的圆时，
 *  才保留容器节点（RenderTransform），可当动画锚点），
 * 样式 fill / stroke / stroke-width / opacity / fill-opacity / stroke-opacity（沿组继承），
 * 同一元素上的取值优先级 = 样式表声明 > presentation attribute（SVG 规范的级联方向）；
 * defs 内 linearGradient 完整解析为线性渐变（stops + 渐变轴 + gradientUnits + gradientTransform
 * 的 translate/rotate/scale/matrix/skew 折算 + href 继承，逐顶点色精确绘制），
 * radialGradient 回退为加权平均纯色（真径向渐变排 P2）。
 * clip-path 支持 rect 组合（轴对齐裁剪框：rect 叶子钳制、完全裁掉的跳过，其余形状越界仅警告）；
 * mask 不支持。
 *
 * <style> 样式表（Visio / Illustrator 之类把 fill 写进类规则、元素只剩 class="st1" 的写法）：
 * 解析期先摘走 style 块单独解析为规则表，支持简单复合选择器 tag / .class / #id 与
 * display:none，按特异度（id>class>tag）与文档顺序级联；含组合器、属性选择器、伪类的选择器
 * 与 at-rule（@media 等）跳过。元素选择器的后代/子选择器不支持——图元是扁平元素，
 * 继承链只由 SVG 分组表达。
 *
 * 交给 FXmlFile 之前的文本预处理见 PrepareSvgTextForXmlParser：FXmlFile 是行式 token 解析器，
 * CDATA 段、style 内的 CSS、跨行的属性值（Visio 的 path d 就是多行的）三者都会让它报
 * "Malformed Xml File"，必须先摘除/折平。
 *
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

/** SVG 线性渐变定义（填充用；解析期产物，工厂折算成控件的 FSCADALinearGradient） */
struct FSCADASvgLinearGradient
{
	/** 停靠点（offset 0-1 升序，颜色含 stop-opacity 与 fill-opacity） */
	TArray<TPair<float, FLinearColor>> Stops;
	/** 渐变轴起点/终点：bObjectBoundingBox = 相对元素包围盒的 0-1 归一化；
	 *  否则 = viewBox 空间绝对坐标（已做 -ViewBoxMin 修正），工厂按元素包围盒折算归一化 */
	FVector2D AxisStart = FVector2D(0.0, 0.0);
	FVector2D AxisEnd = FVector2D(1.0, 0.0);
	bool bObjectBoundingBox = true;
};

/** 表现样式（组继承已解析）：Fill/Stroke 未设置 = SVG 语义 none（不画） */
struct FSCADASvgStyle
{
	/** 填充色（SVG 缺省 = 纯黑，解析时按规范填入；fill="none" → Reset）。
	 *  渐变填充时 = 渐变加权平均色（控件 FillColor 代表色/渐变停用回退色/轮廓 AA 覆层色） */
	TOptional<FLinearColor> Fill;
	/** 线性渐变填充（fill="url(#linearGradient)" 时设置；径向渐变不支持真渐变，只给 Fill 平均色） */
	TOptional<FSCADASvgLinearGradient> FillGradient;
	/** 描边色（SVG 缺省 = none；渐变描边取平均色） */
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
	/** true = 由 path 采样生成的点列（亚像素坐标，工厂据此关闭控件的像素对齐） */
	bool bSubPixelSampling = false;

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
	/** viewBox 尺寸（无 viewBox 时用 width/height 兜底，再缺省 100×100；CropToContent 后为内容包围盒尺寸） */
	FVector2D ViewBoxSize = FVector2D(100.0, 100.0);
	/** 含 rotate 组的容器坐标空间尺寸（组内容画布大小与 Pivot 换算基准）= 裁剪前的 viewBox 尺寸：
	 *  组内子元素保持原始 SVG 坐标，与 CropToContent 的顶层裁剪无关；未裁剪时恒等于 ViewBoxSize。 */
	FVector2D GroupContainerSize = FVector2D(100.0, 100.0);
	TArray<FSCADASvgNode> Roots;

	/** 从 SVG 文本解析（FXmlFile）。不支持的元素/属性跳过并记 Warning 日志（LogSCADASvg）。
	 *  未知命名空间的元素/属性（inkscape:/sodipodi: 等厂商扩展）按 SVG 规范静默忽略。
	 *  返回 false = XML 本身无法解析。 */
	static bool Parse(const FString& SvgText, FSCADASvgDocument& OutDocument);

	/** 按内容包围盒裁剪 viewBox：顶层坐标平移 -内容Min，ViewBoxSize 改为内容尺寸
	 *  （余量 = 各元素描边半宽 + Padding；分组节点的槽平移 Translation 随顶层一同修正，
	 *  组内容器本地坐标与旋转锚点不变）。无内容时不做任何修改。 */
	void CropToContent(float Padding = 0.0f);
};
