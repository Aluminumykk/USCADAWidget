// Copyright Pr_UEDraw. All Rights Reserved.

#include "SvgImport/SCADASvgDocument.h"

#include "XmlFile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSCADASvg, Log, All);

namespace SCADASvgImport
{
	/** 常用命名色（SVG/CSS basic keywords 子集，工业图标常用） */
	static bool TryGetNamedColor(const FString& Name, FLinearColor& OutColor)
	{
		static const TMap<FString, FLinearColor> NamedColors = {
			{ TEXT("black"), FLinearColor::Black }, { TEXT("white"), FLinearColor::White },
			{ TEXT("red"), FLinearColor::Red }, { TEXT("green"), FLinearColor(0.f, 0.5f, 0.f, 1.f) },
			{ TEXT("blue"), FLinearColor::Blue }, { TEXT("yellow"), FLinearColor::Yellow },
			{ TEXT("gray"), FLinearColor::Gray }, { TEXT("grey"), FLinearColor::Gray },
			{ TEXT("orange"), FLinearColor(1.f, 0.647f, 0.f, 1.f) }, { TEXT("purple"), FLinearColor(0.5f, 0.f, 0.5f, 1.f) },
			{ TEXT("brown"), FLinearColor(0.647f, 0.165f, 0.165f, 1.f) }, { TEXT("pink"), FLinearColor(1.f, 0.753f, 0.796f, 1.f) },
			{ TEXT("cyan"), FLinearColor(0.f, 1.f, 1.f, 1.f) }, { TEXT("magenta"), FLinearColor(1.f, 0.f, 1.f, 1.f) },
			{ TEXT("lime"), FLinearColor(0.f, 1.f, 0.f, 1.f) }, { TEXT("navy"), FLinearColor(0.f, 0.f, 0.5f, 1.f) },
			{ TEXT("teal"), FLinearColor(0.f, 0.5f, 0.5f, 1.f) }, { TEXT("silver"), FLinearColor(0.753f, 0.753f, 0.753f, 1.f) },
			{ TEXT("transparent"), FLinearColor(0.f, 0.f, 0.f, 0.f) },
		};
		const FLinearColor* Found = NamedColors.Find(Name.ToLower());
		if (Found)
		{
			OutColor = *Found;
			return true;
		}
		return false;
	}

	/** 解析颜色值：#RGB / #RRGGBB / #RRGGBBAA / rgb() / rgba() / 命名色；失败返回 false */
	static bool TryParseColor(const FString& InValue, FLinearColor& OutColor)
	{
		FString V = InValue.TrimStartAndEnd();
		if (V.IsEmpty())
		{
			return false;
		}

		if (V.StartsWith(TEXT("#")))
		{
			const FString Hex = V.Mid(1);
			auto HexByte = [&Hex](int32 Index) -> int32
			{
				return FParse::HexDigit(Hex[Index * 2]) * 16 + FParse::HexDigit(Hex[Index * 2 + 1]);
			};
			if (Hex.Len() == 3 || Hex.Len() == 4)
			{
				// #RGB / #RGBA：每位翻倍
				const float R = FParse::HexDigit(Hex[0]) / 15.0f;
				const float G = FParse::HexDigit(Hex[1]) / 15.0f;
				const float B = FParse::HexDigit(Hex[2]) / 15.0f;
				const float A = (Hex.Len() == 4) ? FParse::HexDigit(Hex[3]) / 15.0f : 1.0f;
				OutColor = FLinearColor(R, G, B, A);
				return true;
			}
			if (Hex.Len() == 6 || Hex.Len() == 8)
			{
				const float A = (Hex.Len() == 8) ? HexByte(3) / 255.0f : 1.0f;
				OutColor = FLinearColor(HexByte(0) / 255.0f, HexByte(1) / 255.0f, HexByte(2) / 255.0f, A);
				return true;
			}
			return false;
		}

		if (V.StartsWith(TEXT("rgb")))
		{
			FString Inner = V.Mid(V.Find(TEXT("(")) + 1);
			Inner = Inner.LeftChop(1);
			Inner.ReplaceInline(TEXT("%"), TEXT(""));
			TArray<FString> Parts;
			Inner.ParseIntoArray(Parts, TEXT(","), true);
			if (Parts.Num() >= 3)
			{
				const float R = FCString::Atof(*Parts[0]) / 255.0f;
				const float G = FCString::Atof(*Parts[1]) / 255.0f;
				const float B = FCString::Atof(*Parts[2]) / 255.0f;
				const float A = (Parts.Num() >= 4) ? FCString::Atof(*Parts[3]) : 1.0f;
				OutColor = FLinearColor(R, G, B, FMath::Clamp(A, 0.0f, 1.0f));
				return true;
			}
			return false;
		}

		return TryGetNamedColor(V, OutColor);
	}

	/** 渐变 stops 的加权平均色（trapezoid 积分，P0 渐变近似；url(#id) 引用查不到时返回空） */
	struct FGradientStops
	{
		TArray<TPair<float, FLinearColor>> Stops; // (offset, color)，按 offset 升序

		FLinearColor AverageColor() const
		{
			if (Stops.Num() == 0)
			{
				return FLinearColor::Gray;
			}
			if (Stops.Num() == 1)
			{
				return Stops[0].Value;
			}
			FLinearColor Sum(0.f, 0.f, 0.f, 0.f);
			for (int32 i = 0; i + 1 < Stops.Num(); ++i)
			{
				const float W = Stops[i + 1].Key - Stops[i].Key;
				Sum += (Stops[i].Value + Stops[i + 1].Value) * (0.5f * W);
			}
			return Sum;
		}
	};

	/** 从 defs 节点收集渐变表（id → 平均色）。xlink:href 渐变继承不支持（P0，引用空的渐变会按全黑平均色兜底并可在日志中发现） */
	static TMap<FString, FLinearColor> CollectGradients(const FXmlNode* Root)
	{
		TMap<FString, FGradientStops> Raw;

		for (const FXmlNode* Node : Root->GetChildrenNodes())
		{
			if (!Node->GetTag().Equals(TEXT("defs"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			for (const FXmlNode* Def : Node->GetChildrenNodes())
			{
				const FString& Tag = Def->GetTag();
				if (!Tag.Equals(TEXT("linearGradient"), ESearchCase::IgnoreCase) && !Tag.Equals(TEXT("radialGradient"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				const FString Id = Def->GetAttribute(TEXT("id"));
				if (Id.IsEmpty())
				{
					continue;
				}
				FGradientStops& G = Raw.FindOrAdd(Id);
				for (const FXmlNode* Stop : Def->GetChildrenNodes())
				{
					if (!Stop->GetTag().Equals(TEXT("stop"), ESearchCase::IgnoreCase))
					{
						continue;
					}
					float Offset = 0.0f;
					{
						FString OffsetStr = Stop->GetAttribute(TEXT("offset"));
						const bool bPercent = OffsetStr.RemoveFromEnd(TEXT("%"));
						Offset = FCString::Atof(*OffsetStr);
						Offset = bPercent ? Offset / 100.0f : FMath::Clamp(Offset, 0.0f, 1.0f);
					}
					FLinearColor C = FLinearColor::Black;
					const FString StopColor = Stop->GetAttribute(TEXT("stop-color"));
					if (StopColor.IsEmpty() || !TryParseColor(StopColor, C))
					{
						UE_LOG(LogSCADASvg, Warning, TEXT("渐变 '%s' 的 stop 无有效 stop-color（可能在命名空间扩展属性中），按黑色参与平均"), *Id);
					}
					const FString StopOpacity = Stop->GetAttribute(TEXT("stop-opacity"));
					if (!StopOpacity.IsEmpty())
					{
						C.A *= FCString::Atof(*StopOpacity);
					}
					G.Stops.Add(TPair<float, FLinearColor>(Offset, C));
				}
				G.Stops.Sort([](const TPair<float, FLinearColor>& A, const TPair<float, FLinearColor>& B) { return A.Key < B.Key; });
			}
		}

		TMap<FString, FLinearColor> Result;
		for (const auto& Pair : Raw)
		{
			Result.Add(Pair.Key, Pair.Value.AverageColor());
		}
		return Result;
	}

	/** 解析样式类属性值 → 颜色或 none 或渐变引用 */
	static TOptional<FLinearColor> ParsePaintValue(const FString& Value, const TMap<FString, FLinearColor>& Gradients, float Opacity, bool& bOutRecognized)
	{
		TOptional<FLinearColor> Result;
		bOutRecognized = true;
		const FString V = Value.TrimStartAndEnd();
		if (V.IsEmpty() || V.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			return Result; // none
		}
		if (V.StartsWith(TEXT("url(")))
		{
			FString Ref = V.Mid(4).TrimStartAndEnd();
			Ref.RemoveFromEnd(TEXT(")"));
			Ref.RemoveFromStart(TEXT("#"));
			if (const FLinearColor* Avg = Gradients.Find(Ref))
			{
				Result = *Avg;
			}
			else
			{
				UE_LOG(LogSCADASvg, Warning, TEXT("未找到渐变引用 url(#%s)，按灰色处理"), *Ref);
				Result = FLinearColor::Gray;
			}
		}
		else
		{
			FLinearColor C;
			if (TryParseColor(V, C))
			{
				Result = C;
			}
			else
			{
				UE_LOG(LogSCADASvg, Warning, TEXT("无法解析颜色值 '%s'，按 none 处理"), *V);
				bOutRecognized = false;
			}
		}
		if (Result.IsSet() && Opacity < 1.0f)
		{
			Result->A *= Opacity;
		}
		return Result;
	}

	/** 解析 points 属性（"x1,y1 x2,y2" 或逗号/空白任意混合分隔） */
	static TArray<FVector2D> ParsePointsList(const FString& Value)
	{
		FString Normalized = Value;
		Normalized.ReplaceInline(TEXT(","), TEXT(" "));
		Normalized.ReplaceInline(TEXT("\t"), TEXT(" "));
		Normalized.ReplaceInline(TEXT("\n"), TEXT(" "));
		Normalized.ReplaceInline(TEXT("\r"), TEXT(" "));
		TArray<FString> Tokens;
		Normalized.ParseIntoArrayWS(Tokens);
		TArray<FVector2D> Points;
		for (int32 i = 0; i + 1 < Tokens.Num(); i += 2)
		{
			Points.Add(FVector2D(FCString::Atof(*Tokens[i]), FCString::Atof(*Tokens[i + 1])));
		}
		return Points;
	}

	/** 解析 g 的 transform 列表：全部 translate 累加进 OutTranslation；识别 rotate(a[,cx,cy])；
	 *  scale/matrix/skew 警告忽略。translate 与 rotate 混排时的顺序语义按近似处理（先平移后旋转）。 */
	static void ParseGroupTransform(const FString& Value, const FString& GroupId,
		FVector2D& OutTranslation, bool& bOutHasRotation, float& OutRotationAngle, FVector2D& OutRotationCenter)
	{
		FString V = Value;
		V.ReplaceInline(TEXT("\n"), TEXT(" "));
		int32 Pos = 0;
		while (Pos < V.Len())
		{
			const int32 Open = V.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (Open == INDEX_NONE)
			{
				break;
			}
			const int32 Close = V.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
			if (Close == INDEX_NONE)
			{
				break;
			}
			// 关键字 = '(' 前紧邻的连续字母
			int32 KeyStart = Open;
			while (KeyStart > 0 && FChar::IsAlpha(V[KeyStart - 1]))
			{
				--KeyStart;
			}
			const FString Keyword = V.Mid(KeyStart, Open - KeyStart).ToLower();

			FString Args = V.Mid(Open + 1, Close - Open - 1);
			Args.ReplaceInline(TEXT(","), TEXT(" "));
			TArray<FString> Tokens;
			Args.ParseIntoArrayWS(Tokens);

			if (Keyword == TEXT("translate") && Tokens.Num() >= 1)
			{
				OutTranslation += FVector2D(FCString::Atof(*Tokens[0]), Tokens.Num() >= 2 ? FCString::Atof(*Tokens[1]) : 0.0f);
			}
			else if (Keyword == TEXT("rotate") && Tokens.Num() >= 1)
			{
				bOutHasRotation = true;
				OutRotationAngle = FCString::Atof(*Tokens[0]);
				if (Tokens.Num() >= 3)
				{
					OutRotationCenter = FVector2D(FCString::Atof(*Tokens[1]), FCString::Atof(*Tokens[2]));
				}
			}
			else if (Keyword == TEXT("scale") || Keyword == TEXT("matrix") || Keyword.StartsWith(TEXT("skew")))
			{
				UE_LOG(LogSCADASvg, Warning, TEXT("分组 '%s' 含 %s 变换，暂不支持已忽略（图形可能变形/偏差）"), *GroupId, *Keyword);
			}
			Pos = Close + 1;
		}
	}

	static float ParseLength(const FString& Value, float Default = 0.0f)
	{
		if (Value.IsEmpty())
		{
			return Default;
		}
		FString V = Value;
		V.RemoveFromEnd(TEXT("px"));
		return FCString::Atof(*V);
	}

	/** 递归解析元素节点，产物追加到 OutNodes（一个 XML 元素可产 0..N 个节点——无旋转组扁平化时
	 *  组本身不产出、子节点直接挂父级）。样式沿组继承；AccumOffset = 父链 translate 与
	 *  viewBox min 修正的累计平移（顶层调用传 -ViewBoxMin），烘入叶子坐标。 */
	static void ParseElement(const FXmlNode* XmlNode, const FSCADASvgStyle& ParentStyle,
		const TMap<FString, FLinearColor>& Gradients, const FVector2D& AccumOffset, const FVector2D& ViewBoxMin,
		TArray<FSCADASvgNode>& OutNodes)
	{
		const FString& Tag = XmlNode->GetTag();

		// 未知命名空间元素（inkscape:/sodipodi: 等厂商扩展）按 SVG 规范静默忽略
		if (Tag.Contains(TEXT(":")))
		{
			return;
		}

		// display="none" 整棵子树跳过
		if (XmlNode->GetAttribute(TEXT("display")).Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			return;
		}

		ESCADASvgNodeKind Kind;
		if (Tag.Equals(TEXT("g"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Group; }
		else if (Tag.Equals(TEXT("rect"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Rect; }
		else if (Tag.Equals(TEXT("circle"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Circle; }
		else if (Tag.Equals(TEXT("ellipse"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Ellipse; }
		else if (Tag.Equals(TEXT("line"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Line; }
		else if (Tag.Equals(TEXT("polyline"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Polyline; }
		else if (Tag.Equals(TEXT("polygon"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Polygon; }
		else
		{
			// title/desc/defs/metadata 等静默跳过；其余提示
			static const TSet<FString> SilentTags = { TEXT("title"), TEXT("desc"), TEXT("defs"), TEXT("metadata"), TEXT("xml") };
			if (!SilentTags.Contains(Tag.ToLower()))
			{
				UE_LOG(LogSCADASvg, Warning, TEXT("不支持的 SVG 元素 <%s>，已跳过（支持 rect/circle/ellipse/line/polyline/polygon/g）"), *Tag);
			}
			return;
		}

		const FString Id = XmlNode->GetAttribute(TEXT("id"));

		// —— 样式：父样式 + 本元素覆盖（fill/stroke/stroke-width/fill-opacity/stroke-opacity）——
		FSCADASvgStyle Style = ParentStyle;
		{
			const FString FillOpacity = XmlNode->GetAttribute(TEXT("fill-opacity"));
			const float FOp = FillOpacity.IsEmpty() ? 1.0f : FCString::Atof(*FillOpacity);
			const FString StrokeOpacity = XmlNode->GetAttribute(TEXT("stroke-opacity"));
			const float SOp = StrokeOpacity.IsEmpty() ? 1.0f : FCString::Atof(*StrokeOpacity);

			const FString FillValue = XmlNode->GetAttribute(TEXT("fill"));
			if (!FillValue.IsEmpty())
			{
				bool bRecognized = true;
				Style.Fill = ParsePaintValue(FillValue, Gradients, FOp, bRecognized);
			}
			else if (Style.Fill.IsSet() && FOp < 1.0f)
			{
				Style.Fill->A *= FOp;
			}

			const FString StrokeValue = XmlNode->GetAttribute(TEXT("stroke"));
			if (!StrokeValue.IsEmpty())
			{
				bool bRecognized = true;
				Style.Stroke = ParsePaintValue(StrokeValue, Gradients, SOp, bRecognized);
			}
			else if (Style.Stroke.IsSet() && SOp < 1.0f)
			{
				Style.Stroke->A *= SOp;
			}

			const FString StrokeWidthValue = XmlNode->GetAttribute(TEXT("stroke-width"));
			if (!StrokeWidthValue.IsEmpty())
			{
				Style.StrokeWidth = FMath::Max(ParseLength(StrokeWidthValue, 1.0f), 0.0f);
			}
		}

		// —— 分组：translate 烘平 / rotate 保留容器节点 ——
		if (Kind == ESCADASvgNodeKind::Group)
		{
			FVector2D T = FVector2D::ZeroVector;
			bool bHasRotation = false;
			float RotationAngle = 0.0f;
			FVector2D RotationCenter = FVector2D::ZeroVector;
			const FString Transform = XmlNode->GetAttribute(TEXT("transform"));
			if (!Transform.IsEmpty())
			{
				ParseGroupTransform(Transform, Id, T, bHasRotation, RotationAngle, RotationCenter);
			}

			if (!bHasRotation)
			{
				// 无旋转组扁平化：平移累加进子坐标，组节点不产出（样式继承链不断——传本组 Style）
				const FVector2D ChildOffset = AccumOffset + T;
				for (const FXmlNode* Child : XmlNode->GetChildrenNodes())
				{
					ParseElement(Child, Style, Gradients, ChildOffset, ViewBoxMin, OutNodes);
				}
				return;
			}

			// 含 rotate 的组保留为容器节点：槽位置承担平移（Translation = 父链累计 + 本组 translate，
			// 含 viewBox min 修正），子元素保持原始 SVG 坐标（容器本地空间），RenderTransform 绕
			// RotationCenter（原始 SVG 坐标）旋转 ≈ SVG 的 T*R 语义
			FSCADASvgNode Node;
			Node.Kind = ESCADASvgNodeKind::Group;
			Node.Id = Id;
			Node.Style = Style;
			Node.Translation = AccumOffset + T;
			Node.bHasRotation = true;
			Node.RotationAngle = RotationAngle;
			Node.RotationCenter = RotationCenter;
			for (const FXmlNode* Child : XmlNode->GetChildrenNodes())
			{
				ParseElement(Child, Style, Gradients, FVector2D::ZeroVector, ViewBoxMin, Node.Children);
			}
			// 空分组无意义
			if (Node.Children.Num() > 0)
			{
				OutNodes.Add(MoveTemp(Node));
			}
			return;
		}

		// —— 叶子元素：几何 + 累积平移烘入 ——
		FSCADASvgNode Node;
		Node.Kind = Kind;
		Node.Id = Id;
		Node.Style = Style;
		switch (Kind)
		{
		case ESCADASvgNodeKind::Rect:
			Node.P1 = FVector2D(ParseLength(XmlNode->GetAttribute(TEXT("x"))), ParseLength(XmlNode->GetAttribute(TEXT("y"))));
			Node.P2 = FVector2D(ParseLength(XmlNode->GetAttribute(TEXT("width"))), ParseLength(XmlNode->GetAttribute(TEXT("height"))));
			Node.CornerRadius.X = ParseLength(XmlNode->GetAttribute(TEXT("rx")));
			Node.CornerRadius.Y = ParseLength(XmlNode->GetAttribute(TEXT("ry")), (float)Node.CornerRadius.X);
			break;
		case ESCADASvgNodeKind::Circle:
			Node.P1 = FVector2D(ParseLength(XmlNode->GetAttribute(TEXT("cx"))), ParseLength(XmlNode->GetAttribute(TEXT("cy"))));
			Node.Radius = ParseLength(XmlNode->GetAttribute(TEXT("r")));
			break;
		case ESCADASvgNodeKind::Ellipse:
			Node.P1 = FVector2D(ParseLength(XmlNode->GetAttribute(TEXT("cx"))), ParseLength(XmlNode->GetAttribute(TEXT("cy"))));
			Node.P2 = FVector2D(ParseLength(XmlNode->GetAttribute(TEXT("rx"))), ParseLength(XmlNode->GetAttribute(TEXT("ry"))));
			break;
		case ESCADASvgNodeKind::Line:
			Node.P1 = FVector2D(ParseLength(XmlNode->GetAttribute(TEXT("x1"))), ParseLength(XmlNode->GetAttribute(TEXT("y1"))));
			Node.P2 = FVector2D(ParseLength(XmlNode->GetAttribute(TEXT("x2"))), ParseLength(XmlNode->GetAttribute(TEXT("y2"))));
			break;
		case ESCADASvgNodeKind::Polyline:
		case ESCADASvgNodeKind::Polygon:
			Node.Points = ParsePointsList(XmlNode->GetAttribute(TEXT("points")));
			break;
		case ESCADASvgNodeKind::Group:
			break;
		}

		Node.P1 += AccumOffset;
		if (Kind == ESCADASvgNodeKind::Line)
		{
			Node.P2 += AccumOffset;
		}
		for (FVector2D& P : Node.Points)
		{
			P += AccumOffset;
		}
		OutNodes.Add(MoveTemp(Node));
	}
}

bool FSCADASvgDocument::Parse(const FString& SvgText, FSCADASvgDocument& OutDocument)
{
	FXmlFile XmlFile(SvgText, EConstructMethod::ConstructFromBuffer);
	if (!XmlFile.IsValid())
	{
		UE_LOG(LogSCADASvg, Error, TEXT("SVG XML 解析失败：%s"), *XmlFile.GetLastError());
		return false;
	}
	const FXmlNode* Root = XmlFile.GetRootNode();
	if (Root == nullptr || !Root->GetTag().Equals(TEXT("svg"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogSCADASvg, Error, TEXT("SVG 根元素缺失或不是 <svg>"));
		return false;
	}

	// viewBox / width / height
	FVector2D ViewBoxMin = FVector2D::ZeroVector;
	{
		const FString ViewBox = Root->GetAttribute(TEXT("viewBox"));
		if (!ViewBox.IsEmpty())
		{
			FString Normalized = ViewBox;
			Normalized.ReplaceInline(TEXT(","), TEXT(" "));
			TArray<FString> Tokens;
			Normalized.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() == 4)
			{
				ViewBoxMin = FVector2D(FCString::Atof(*Tokens[0]), FCString::Atof(*Tokens[1]));
				OutDocument.ViewBoxSize = FVector2D(FCString::Atof(*Tokens[2]), FCString::Atof(*Tokens[3]));
			}
		}
		else
		{
			const float W = SCADASvgImport::ParseLength(Root->GetAttribute(TEXT("width")));
			const float H = SCADASvgImport::ParseLength(Root->GetAttribute(TEXT("height")));
			if (W > 0.0f && H > 0.0f)
			{
				OutDocument.ViewBoxSize = FVector2D(W, H);
			}
			else
			{
				UE_LOG(LogSCADASvg, Warning, TEXT("SVG 无 viewBox 且 width/height 无效，按 100x100 处理"));
			}
		}
	}

	const TMap<FString, FLinearColor> Gradients = SCADASvgImport::CollectGradients(Root);

	// SVG 规范缺省样式：fill = 纯黑，stroke = none
	FSCADASvgStyle RootStyle;
	RootStyle.Fill = FLinearColor::Black;

	for (const FXmlNode* Child : Root->GetChildrenNodes())
	{
		SCADASvgImport::ParseElement(Child, RootStyle, Gradients, -ViewBoxMin, ViewBoxMin, OutDocument.Roots);
	}

	UE_LOG(LogSCADASvg, Log, TEXT("SVG 解析完成：viewBox %.0fx%.0f，顶层元素 %d 个"),
		OutDocument.ViewBoxSize.X, OutDocument.ViewBoxSize.Y, OutDocument.Roots.Num());
	return true;
}
