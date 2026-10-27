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

	/** stops 加权平均色（trapezoid 积分；径向渐变回退色 & 线性渐变的 FillColor 代表色） */
	static FLinearColor AverageStops(const TArray<TPair<float, FLinearColor>>& Stops)
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

	/** 2D 仿射矩阵（SVG transform 语义：[A B TX; C D TY]）；用于 gradientTransform 折算渐变轴 */
	struct FSvgAffine
	{
		double A = 1.0, B = 0.0, TX = 0.0;
		double C = 0.0, D = 1.0, TY = 0.0;

		FVector2D Apply(const FVector2D& P) const
		{
			return FVector2D(A * P.X + B * P.Y + TX, C * P.X + D * P.Y + TY);
		}

		/** transform 列表的语义是 M = M1 × M2 × …（左到右），故每个算子按出现顺序累乘到右侧 */
		void Append(const FSvgAffine& R)
		{
			const double NA = A * R.A + B * R.C;
			const double NB = A * R.B + B * R.D;
			const double NTX = A * R.TX + B * R.TY + TX;
			const double NC = C * R.A + D * R.C;
			const double ND = C * R.B + D * R.D;
			const double NTY = C * R.TX + D * R.TY + TY;
			A = NA; B = NB; TX = NTX; C = NC; D = ND; TY = NTY;
		}
	};

	static FSvgAffine MakeAffine(double InA, double InB, double InTX, double InC, double InD, double InTY)
	{
		FSvgAffine M;
		M.A = InA; M.B = InB; M.TX = InTX;
		M.C = InC; M.D = InD; M.TY = InTY;
		return M;
	}

	/** 解析 SVG transform 列表为仿射矩阵：translate / rotate(a[,cx,cy]) / scale / matrix / skewX / skewY
	 *  （角度为度）。未知关键字或参数不足 → false，调用方按"不支持"处理并保持单位矩阵。 */
	static bool TryParseAffineTransform(const FString& Value, FSvgAffine& Out)
	{
		FString V = Value;
		V.ReplaceInline(TEXT("\n"), TEXT(" "));
		Out = FSvgAffine();

		int32 Pos = 0;
		while (Pos < V.Len())
		{
			const int32 Open = V.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (Open == INDEX_NONE)
			{
				return false;
			}
			const int32 Close = V.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
			if (Close == INDEX_NONE)
			{
				return false;
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
			auto Num = [&Tokens](int32 Index) -> double
			{
				return Tokens.IsValidIndex(Index) ? FCString::Atod(*Tokens[Index]) : 0.0;
			};

			if (Keyword == TEXT("translate"))
			{
				Out.Append(MakeAffine(1.0, 0.0, Num(0), 0.0, 1.0, Num(1)));
			}
			else if (Keyword == TEXT("rotate"))
			{
				if (Tokens.Num() < 1)
				{
					return false;
				}
				const double Rad = FMath::DegreesToRadians(Num(0));
				const double Cos = FMath::Cos(Rad), Sin = FMath::Sin(Rad);
				if (Tokens.Num() >= 3)
				{
					// rotate(a,cx,cy) = translate(cx,cy) · rotate(a) · translate(-cx,-cy)
					const double CX = Num(1), CY = Num(2);
					Out.Append(MakeAffine(1.0, 0.0, CX, 0.0, 1.0, CY));
					Out.Append(MakeAffine(Cos, -Sin, 0.0, Sin, Cos, 0.0));
					Out.Append(MakeAffine(1.0, 0.0, -CX, 0.0, 1.0, -CY));
				}
				else
				{
					Out.Append(MakeAffine(Cos, -Sin, 0.0, Sin, Cos, 0.0));
				}
			}
			else if (Keyword == TEXT("scale"))
			{
				if (Tokens.Num() < 1)
				{
					return false;
				}
				const double SX = Num(0);
				Out.Append(MakeAffine(SX, 0.0, 0.0, 0.0, Tokens.Num() >= 2 ? Num(1) : SX, 0.0));
			}
			else if (Keyword == TEXT("matrix"))
			{
				if (Tokens.Num() < 6)
				{
					return false;
				}
				Out.Append(MakeAffine(Num(0), Num(2), Num(4), Num(1), Num(3), Num(5)));
			}
			else if (Keyword == TEXT("skewx") || Keyword == TEXT("skewy"))
			{
				if (Tokens.Num() < 1)
				{
					return false;
				}
				const double Tan = FMath::Tan(FMath::DegreesToRadians(Num(0)));
				Out.Append(Keyword == TEXT("skewx")
					? MakeAffine(1.0, Tan, 0.0, 0.0, 1.0, 0.0)
					: MakeAffine(1.0, 0.0, 0.0, Tan, 1.0, 0.0));
			}
			else
			{
				return false;
			}
			Pos = Close + 1;
		}
		return true;
	}

	/** 渐变定义（linearGradient 完整解析；radialGradient 只留 stops 算平均色） */
	struct FGradientDef
	{
		bool bRadial = false;
		FSCADASvgLinearGradient Linear; // 径向时仅 Stops 有效
		FLinearColor Average = FLinearColor::Gray;
	};

	/** 从 defs 节点收集渐变表（id → 定义）。解析 x1/y1/x2/y2（% 与 0-1 小数同义）、gradientUnits
	 *  （objectBoundingBox 缺省 / userSpaceOnUse）、gradientTransform（translate/rotate/scale/matrix/
	 *  skew 折算到渐变轴；不经 href 继承）与 xlink:href/href 继承（自身未给出的 stops 与
	 *  属性从引用渐变补齐，链式解析，Visited 防环）。
	 *  径向渐变（cx/cy/r/fx/fy）不完整解析——只收集 stops 供平均色回退。 */
	static TMap<FString, FGradientDef> CollectGradients(const FXmlNode* Root)
	{
		struct FGradientRaw
		{
			bool bRadial = false;
			TArray<TPair<float, FLinearColor>> Stops;
			TOptional<FVector2D> AxisStart, AxisEnd;
			TOptional<bool> bUserSpace;
			TOptional<FSvgAffine> Transform;
			FString Href;
		};
		TMap<FString, FGradientRaw> Raw;

		// 轴坐标：% → /100；小数值原样（objectBoundingBox 下即 0-1 分数；userSpace 下为绝对坐标）
		auto ParseAxisNumber = [](const FString& S) -> float
		{
			FString V = S;
			const bool bPercent = V.RemoveFromEnd(TEXT("%"));
			const float F = FCString::Atof(*V);
			return bPercent ? F / 100.0f : F;
		};

		for (const FXmlNode* Node : Root->GetChildrenNodes())
		{
			if (!Node->GetTag().Equals(TEXT("defs"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			for (const FXmlNode* Def : Node->GetChildrenNodes())
			{
				const FString& Tag = Def->GetTag();
				const bool bLinear = Tag.Equals(TEXT("linearGradient"), ESearchCase::IgnoreCase);
				const bool bRadial = !bLinear && Tag.Equals(TEXT("radialGradient"), ESearchCase::IgnoreCase);
				if (!bLinear && !bRadial)
				{
					continue;
				}
				const FString Id = Def->GetAttribute(TEXT("id"));
				if (Id.IsEmpty())
				{
					continue;
				}
				FGradientRaw& G = Raw.FindOrAdd(Id);
				G.bRadial = bRadial;

				FString Href = Def->GetAttribute(TEXT("xlink:href"));
				if (Href.IsEmpty())
				{
					Href = Def->GetAttribute(TEXT("href"));
				}
				Href.RemoveFromStart(TEXT("#"));
				G.Href = Href;

				if (Def->GetAttribute(TEXT("gradientUnits")).Equals(TEXT("userSpaceOnUse"), ESearchCase::IgnoreCase))
				{
					G.bUserSpace = true;
				}
				const FString GradientTransform = Def->GetAttribute(TEXT("gradientTransform"));
				if (!GradientTransform.IsEmpty())
				{
					FSvgAffine M;
					if (TryParseAffineTransform(GradientTransform, M))
					{
						G.Transform = M;
					}
					else
					{
						UE_LOG(LogSCADASvg, Warning, TEXT("渐变 '%s' 的 gradientTransform 无法解析，已忽略"), *Id);
					}
				}

				// 渐变轴 x1/y1/x2/y2（缺省 0%,0% → 100%,0%；单分量独立缺省）
				if (bLinear)
				{
					const FString X1 = Def->GetAttribute(TEXT("x1"));
					const FString Y1 = Def->GetAttribute(TEXT("y1"));
					const FString X2 = Def->GetAttribute(TEXT("x2"));
					const FString Y2 = Def->GetAttribute(TEXT("y2"));
					if (!X1.IsEmpty() || !Y1.IsEmpty())
					{
						G.AxisStart = FVector2D(X1.IsEmpty() ? 0.0 : ParseAxisNumber(X1), Y1.IsEmpty() ? 0.0 : ParseAxisNumber(Y1));
					}
					if (!X2.IsEmpty() || !Y2.IsEmpty())
					{
						G.AxisEnd = FVector2D(X2.IsEmpty() ? 1.0 : ParseAxisNumber(X2), Y2.IsEmpty() ? 0.0 : ParseAxisNumber(Y2));
					}
				}

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
						UE_LOG(LogSCADASvg, Warning, TEXT("渐变 '%s' 的 stop 无有效 stop-color（可能在命名空间扩展属性中），按黑色参与"), *Id);
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

		// href 继承解析：自身显式给出的项优先，缺失项沿链补齐（Visited 防环，原地解析天然限深）
		TFunction<void(const FString&, FGradientRaw&, TSet<FString>&)> ResolveHref = [&](const FString& Id, FGradientRaw& G, TSet<FString>& Visited)
		{
			if (G.Href.IsEmpty() || Visited.Contains(G.Href))
			{
				return;
			}
			Visited.Add(G.Href);
			FGradientRaw* Target = Raw.Find(G.Href);
			if (Target == nullptr)
			{
				return;
			}
			ResolveHref(G.Href, *Target, Visited);
			if (G.Stops.Num() == 0) { G.Stops = Target->Stops; }
			if (!G.AxisStart.IsSet()) { G.AxisStart = Target->AxisStart; }
			if (!G.AxisEnd.IsSet()) { G.AxisEnd = Target->AxisEnd; }
			if (!G.bUserSpace.IsSet()) { G.bUserSpace = Target->bUserSpace; }
		};
		for (auto& Pair : Raw)
		{
			TSet<FString> Visited;
			Visited.Add(Pair.Key);
			ResolveHref(Pair.Key, Pair.Value, Visited);
		}

		TMap<FString, FGradientDef> Result;
		for (const auto& Pair : Raw)
		{
			const FGradientRaw& G = Pair.Value;
			FGradientDef D;
			D.bRadial = G.bRadial;
			D.Linear.Stops = G.Stops;
			// 缺省轴（objectBoundingBox 下 (0,0)→(1,0)，userSpaceOnUse 下同样按 SVG 缺省）也要参与
			// gradientTransform：SVG 语义中轴在渐变自己的坐标系内解释，变换作用于轴而非元素
			FVector2D AxisStart = G.AxisStart.Get(FVector2D(0.0, 0.0));
			FVector2D AxisEnd = G.AxisEnd.Get(FVector2D(1.0, 0.0));
			if (G.Transform.IsSet())
			{
				AxisStart = G.Transform->Apply(AxisStart);
				AxisEnd = G.Transform->Apply(AxisEnd);
			}
			D.Linear.AxisStart = AxisStart;
			D.Linear.AxisEnd = AxisEnd;
			D.Linear.bObjectBoundingBox = !G.bUserSpace.Get(false);
			D.Average = AverageStops(G.Stops);
			Result.Add(Pair.Key, D);
		}
		return Result;
	}

	/** 从 defs 收集 clipPath（id → 轴对齐裁剪框，viewBox 空间）：rect 子元素贡献自身矩形（rx 圆角忽略），
	 *  circle/ellipse 贡献外接矩形并警告（近似）；其余形状警告忽略。多形状取**并集包围盒**
	 *  （不相交形状的并集区间会多包，属可接受近似）。clipPathUnits=objectBoundingBox / clipPath 自带
	 *  transform 暂不支持（警告）。 */
	static TMap<FString, FBox2D> CollectClipPaths(const FXmlNode* Root)
	{
		TMap<FString, FBox2D> Result;
		for (const FXmlNode* Node : Root->GetChildrenNodes())
		{
			if (!Node->GetTag().Equals(TEXT("defs"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			for (const FXmlNode* Def : Node->GetChildrenNodes())
			{
				if (!Def->GetTag().Equals(TEXT("clipPath"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				const FString Id = Def->GetAttribute(TEXT("id"));
				if (Id.IsEmpty())
				{
					continue;
				}
				const FString Units = Def->GetAttribute(TEXT("clipPathUnits"));
				if (Units.Equals(TEXT("objectBoundingBox"), ESearchCase::IgnoreCase))
				{
					UE_LOG(LogSCADASvg, Warning, TEXT("clipPath '%s' 的 clipPathUnits=objectBoundingBox 暂不支持，该裁剪被忽略"), *Id);
					continue;
				}
				if (!Def->GetAttribute(TEXT("transform")).IsEmpty())
				{
					UE_LOG(LogSCADASvg, Warning, TEXT("clipPath '%s' 自带 transform 暂不支持，该裁剪被忽略"), *Id);
					continue;
				}

				FBox2D Union(ForceInit);
				for (const FXmlNode* Shape : Def->GetChildrenNodes())
				{
					const FString& Tag = Shape->GetTag();
					if (Tag.Equals(TEXT("rect"), ESearchCase::IgnoreCase))
					{
						// 注：ParseLength 定义在本函数之后，此处直接 Atof（空串 = 0，与 ParseLength 缺省一致）
						const FVector2D P(FCString::Atof(*Shape->GetAttribute(TEXT("x"))), FCString::Atof(*Shape->GetAttribute(TEXT("y"))));
						const FVector2D S(FCString::Atof(*Shape->GetAttribute(TEXT("width"))), FCString::Atof(*Shape->GetAttribute(TEXT("height"))));
						Union += P;
						Union += P + S;
					}
					else if (Tag.Equals(TEXT("circle"), ESearchCase::IgnoreCase) || Tag.Equals(TEXT("ellipse"), ESearchCase::IgnoreCase))
					{
						const bool bCircle = Tag.Equals(TEXT("circle"), ESearchCase::IgnoreCase);
						const FVector2D C(FCString::Atof(*Shape->GetAttribute(TEXT("cx"))), FCString::Atof(*Shape->GetAttribute(TEXT("cy"))));
						const double RX = FCString::Atof(*Shape->GetAttribute(bCircle ? TEXT("r") : TEXT("rx")));
						const double RY = bCircle ? RX : FCString::Atof(*Shape->GetAttribute(TEXT("ry")));
						Union += C - FVector2D(RX, RY);
						Union += C + FVector2D(RX, RY);
						UE_LOG(LogSCADASvg, Warning, TEXT("clipPath '%s' 内 <%s> 按外接矩形裁剪（近似）"), *Id, *Tag);
					}
					else
					{
						UE_LOG(LogSCADASvg, Warning, TEXT("clipPath '%s' 内 <%s> 暂不支持，已忽略"), *Id, *Tag);
					}
				}
				if (Union.bIsValid)
				{
					Result.Add(Id, Union);
				}
			}
		}
		return Result;
	}

	// ———— CSS 样式表（Visio / Illustrator 之类导出器把 fill/stroke 写进 <style> 的类规则，
	//      元素上只剩 class="st1"；只读属性的解析器会把整份图形认成缺省黑色）————

	/** 去掉文本里的 CDATA 标记（换成空格，内容作为普通文本保留）。FXmlFile 完全不认 CDATA 段：
	 *  `<![CDATA[` 会被切成 `<` + `![CDATA[` 当成开始标签，后面的内容被吞作它的"属性"，
	 *  直到下一个 `>` 才闭合，于是真正的结束标签与栈顶对不上 → "Malformed Xml File"。 */
	static void RemoveCDataMarkers(FString& Text)
	{
		Text.ReplaceInline(TEXT("<![CDATA["), TEXT(" "));
		Text.ReplaceInline(TEXT("]]>"), TEXT(" "));
	}

	/** 摘出 SVG 文本里的全部 `<style>` 元素（含内容），返回剩余文本，摘出的内容进 OutCssTexts。
	 *  这些块必须在交给 FXmlFile 之前先取走：除了 CDATA，CSS 文本里的 `>`（子选择器）同样会把
	 *  它的行式 token 解析器判成畸形。摘走后原位置留一个空格，其余文本原样保留。 */
	static FString StripStyleElements(const FString& SvgText, TArray<FString>& OutCssTexts)
	{
		FString Result;
		Result.Reserve(SvgText.Len());

		int32 Pos = 0;
		while (Pos < SvgText.Len())
		{
			const int32 Start = SvgText.Find(TEXT("<style"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos);
			if (Start == INDEX_NONE)
			{
				break;
			}
			const int32 OpenEnd = SvgText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
			if (OpenEnd == INDEX_NONE)
			{
				break; // 开始标签都没闭合，剩下的交给 FXmlFile 报错
			}
			int32 End = OpenEnd + 1;
			if (!SvgText.Mid(Start, OpenEnd - Start + 1).EndsWith(TEXT("/>")))
			{
				const int32 CloseTag = SvgText.Find(TEXT("</style"), ESearchCase::IgnoreCase, ESearchDir::FromStart, OpenEnd + 1);
				if (CloseTag != INDEX_NONE)
				{
					const int32 CloseEnd = SvgText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, CloseTag);
					if (CloseEnd == INDEX_NONE)
					{
						OutCssTexts.Add(SvgText.Mid(OpenEnd + 1));
						End = SvgText.Len();
					}
					else
					{
						OutCssTexts.Add(SvgText.Mid(OpenEnd + 1, CloseEnd - (OpenEnd + 1)));
						End = CloseEnd + 1;
					}
				}
			}
			Result += SvgText.Mid(Pos, Start - Pos);
			Result += TEXT(" ");
			Pos = End;
		}
		Result += SvgText.Mid(Pos);
		return Result;
	}

	/** 引号内的换行折成空格。FXmlFile 是按行 token 化的：跨行的属性值会被切成多段，首段引号不配平
	 *  就整个丢弃（Visio 导出的 `path d` 正是多行书写的，于是几何整条消失）。只改引号内的换行，
	 *  其余文本原样。必须在摘走 `<style>` 之后做——CSS 里的引号会打乱这里的引号配对状态。 */
	static FString FlattenNewlinesInQuotedValues(const FString& Text)
	{
		FString Result = Text;
		bool bInQuote = false;
		for (int32 i = 0; i < Result.Len(); ++i)
		{
			const TCHAR C = Result[i];
			if (C == TEXT('"'))
			{
				bInQuote = !bInQuote;
			}
			else if (bInQuote && (C == TEXT('\n') || C == TEXT('\r')))
			{
				Result[i] = TEXT(' ');
			}
		}
		return Result;
	}

	/** 交给 FXmlFile 之前的文本预处理，三步缺一不可（各自的原因见对应函数注释）：
	 *  摘走 `<style>` 块 → 折平引号内换行 → 去掉残余 CDATA 标记。
	 *  返回可被 FXmlFile 接受的 SVG 文本；摘出的 CSS 内容进 OutCssTexts，由 ParseCssRules 解析。 */
	static FString PrepareSvgTextForXmlParser(const FString& SvgText, TArray<FString>& OutCssTexts)
	{
		FString Result = StripStyleElements(SvgText, OutCssTexts);
		Result = FlattenNewlinesInQuotedValues(Result);
		RemoveCDataMarkers(Result);
		return Result;
	}

	/** CSS 规则。选择器只支持简单复合式（tag / .class / #id）：图元是扁平元素、嵌套复用由 SVG 分组
	 *  表达，因此含组合器（空格 > + ~）、属性选择器、伪类、通配、命名空间或变量的选择器整条跳过。 */
	struct FCssRule
	{
		struct FSelector
		{
			FString Tag;             // 空 = 不限元素
			TArray<FString> Classes; // 需要全部命中
			FString Id;              // 空 = 不限
			int32 Specificity = 0;   // id=100 / class=10 / tag=1，级联排序用
		};
		TArray<FSelector> Selectors;
		TMap<FString, FString> Declarations; // 属性名（小写）→ 值
		int32 Specificity = 0; // 各选择器中的最大特异度
		int32 Order = 0;       // 文档顺序：同特异度时后者覆盖前者
	};

	/** 解析简单复合选择器 `rect.cls#id`（各段顺序任意）。含不支持的写法 → false，调用方跳过该选择器 */
	static bool TryParseCssSelector(const FString& InSelector, FCssRule::FSelector& Out)
	{
		const FString S = InSelector.TrimStartAndEnd();
		for (const TCHAR Ch : S)
		{
			if (Ch == TEXT('>') || Ch == TEXT('+') || Ch == TEXT('~') || Ch == TEXT('[') || Ch == TEXT('(') ||
				Ch == TEXT(':') || Ch == TEXT('*') || Ch == TEXT('|') || Ch == TEXT('%') || Ch == TEXT(',') ||
				Ch == TEXT('$') || FChar::IsWhitespace(Ch))
			{
				return false;
			}
		}

		Out = FCssRule::FSelector();
		int32 Pos = 0;
		while (Pos < S.Len())
		{
			const bool bClass = S[Pos] == TEXT('.');
			const bool bId = S[Pos] == TEXT('#');
			const int32 NameStart = (bClass || bId) ? Pos + 1 : Pos;
			int32 Next = NameStart;
			while (Next < S.Len() && S[Next] != TEXT('.') && S[Next] != TEXT('#'))
			{
				++Next;
			}
			const FString Name = S.Mid(NameStart, Next - NameStart);
			if (Name.IsEmpty())
			{
				return false;
			}
			if (bClass)
			{
				const FString ClassName = Name.ToLower();
				Out.Classes.Add(ClassName);
				Out.Specificity += 10;
			}
			else if (bId)
			{
				if (!Out.Id.IsEmpty())
				{
					return false;
				}
				Out.Id = Name.ToLower();
				Out.Specificity += 100;
			}
			else
			{
				// 元素名只允许出现在最前面
				if (!Out.Tag.IsEmpty() || Out.Classes.Num() > 0 || !Out.Id.IsEmpty())
				{
					return false;
				}
				const FString TagName = Name.ToLower();
				Out.Tag = TagName;
				Out.Specificity += 1;
			}
			Pos = Next;
		}
		return !Out.Tag.IsEmpty() || Out.Classes.Num() > 0 || !Out.Id.IsEmpty();
	}

	/** 解析一段 style 文本为规则表（`sel1, sel2 { prop: value; ... }`）。先去掉 CDATA 标记与 CSS 注释；
	 *  `@import` 这类无块 at-statement 整句剔除，`@media` 这类带块 at-rule 整块跳过（Log 级别，
	 *  不制造警告噪音——保存资产时后台加载会把 Warning 当资产错误报）。 */
	static void ParseCssRules(const FString& CssText, TArray<FCssRule>& OutRules)
	{
		FString S = CssText;
		RemoveCDataMarkers(S);

		// 剔除 CSS 注释
		for (;;)
		{
			const int32 From = S.Find(TEXT("/*"), ESearchCase::CaseSensitive);
			if (From == INDEX_NONE)
			{
				break;
			}
			const int32 To = S.Find(TEXT("*/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From + 2);
			S = (To == INDEX_NONE) ? S.Left(From) : S.Left(From) + TEXT(" ") + S.Mid(To + 2);
		}

		// 剔除无块的 at-statement（@import / @charset……；块状 at-rule 留给下面的深度跳过）
		for (;;)
		{
			const int32 At = S.Find(TEXT("@"), ESearchCase::CaseSensitive);
			if (At == INDEX_NONE)
			{
				break;
			}
			const int32 Semi = S.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
			const int32 Brace = S.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
			if (Semi == INDEX_NONE || (Brace != INDEX_NONE && Brace < Semi))
			{
				break; // 有块（或没结尾）→ 交给 at-rule 分支处理
			}
			S = S.Left(At) + TEXT(" ") + S.Mid(Semi + 1);
		}

		int32 Pos = 0;
		while (Pos < S.Len())
		{
			const int32 Open = S.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (Open == INDEX_NONE)
			{
				break;
			}
			const FString SelectorText = S.Mid(Pos, Open - Pos).TrimStartAndEnd();

			if (SelectorText.StartsWith(TEXT("@")))
			{
				// 块状 at-rule：按花括号深度整块跳过
				int32 Depth = 1;
				int32 Scan = Open + 1;
				while (Scan < S.Len() && Depth > 0)
				{
					if (S[Scan] == TEXT('{')) { ++Depth; }
					else if (S[Scan] == TEXT('}')) { --Depth; }
					++Scan;
				}
				UE_LOG(LogSCADASvg, Log, TEXT("CSS at-rule '%s' 暂不支持，整块跳过"), *SelectorText.Left(64));
				Pos = Scan;
				continue;
			}

			const int32 Close = S.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
			if (Close == INDEX_NONE)
			{
				break;
			}
			const FString Body = S.Mid(Open + 1, Close - Open - 1);
			Pos = Close + 1;

			FCssRule Rule;
			TArray<FString> SelectorList;
			SelectorText.ParseIntoArray(SelectorList, TEXT(","), true);
			for (const FString& Selector : SelectorList)
			{
				FCssRule::FSelector Parsed;
				if (TryParseCssSelector(Selector, Parsed))
				{
					Rule.Selectors.Add(MoveTemp(Parsed));
				}
			}
			if (Rule.Selectors.Num() == 0)
			{
				continue;
			}

			TArray<FString> Declarations;
			Body.ParseIntoArray(Declarations, TEXT(";"), true);
			for (const FString& Declaration : Declarations)
			{
				const int32 Colon = Declaration.Find(TEXT(":"), ESearchCase::CaseSensitive);
				if (Colon == INDEX_NONE)
				{
					continue;
				}
				const FString NamePart = Declaration.Left(Colon);
				const FString NameTrimmed = NamePart.TrimStartAndEnd();
				const FString Name = NameTrimmed.ToLower();
				const FString ValuePart = Declaration.Mid(Colon + 1);
				FString ValueClean = ValuePart.TrimStartAndEnd();
				ValueClean.ReplaceInline(TEXT("!important"), TEXT(""));
				const FString Value = ValueClean.TrimStartAndEnd();
				if (Name.IsEmpty() || Value.IsEmpty())
				{
					continue;
				}
				Rule.Declarations.Add(Name, Value);
			}

			for (const FCssRule::FSelector& Selector : Rule.Selectors)
			{
				Rule.Specificity = FMath::Max(Rule.Specificity, Selector.Specificity);
			}
			Rule.Order = OutRules.Num();
			OutRules.Add(MoveTemp(Rule));
		}
	}

	/** 元素匹配的声明合并表：CSS 级联 = 特异度升序、同特异度按文档顺序，后者覆盖前者。
	 *  调用方用它覆盖同名 XML 属性——SVG 规范里 presentation attribute 的优先级低于作者样式表。 */
	static TMap<FString, FString> ResolveElementDeclarations(const FXmlNode* Node, const TArray<FCssRule>& Rules)
	{
		TMap<FString, FString> Result;
		if (Rules.Num() == 0)
		{
			return Result;
		}

		const FString Tag = Node->GetTag().ToLower();
		const FString Id = Node->GetAttribute(TEXT("id")).ToLower();
		FString ClassAttr = Node->GetAttribute(TEXT("class"));
		TArray<FString> ClassList;
		ClassAttr.ParseIntoArrayWS(ClassList);
		for (FString& Name : ClassList)
		{
			Name = Name.ToLower();
		}

		TArray<const FCssRule*> Matches;
		for (const FCssRule& Rule : Rules)
		{
			for (const FCssRule::FSelector& Selector : Rule.Selectors)
			{
				if ((!Selector.Tag.IsEmpty() && Selector.Tag != Tag) || (!Selector.Id.IsEmpty() && Selector.Id != Id))
				{
					continue;
				}
				bool bAllClasses = true;
				for (const FString& Needed : Selector.Classes)
				{
					if (!ClassList.Contains(Needed))
					{
						bAllClasses = false;
						break;
					}
				}
				if (bAllClasses)
				{
					Matches.Add(&Rule);
					break;
				}
			}
		}
		if (Matches.IsEmpty())
		{
			return Result;
		}

		// TArray::Sort 对指针元素会解引用，故谓词按引用写
		Matches.Sort([](const FCssRule& A, const FCssRule& B)
		{
			return A.Specificity != B.Specificity ? A.Specificity < B.Specificity : A.Order < B.Order;
		});
		for (const FCssRule* Rule : Matches)
		{
			for (const TPair<FString, FString>& Declaration : Rule->Declarations)
			{
				Result.Add(Declaration.Key, Declaration.Value);
			}
		}
		return Result;
	}

	/** 一次 SVG 解析的共享上下文（Parse 建一次，递归传递）：渐变表 + 裁剪表 + 样式表规则 */
	struct FParseContext
	{
		TMap<FString, FGradientDef> Gradients;
		TMap<FString, FBox2D> ClipPaths;
		TArray<FCssRule> Rules;
	};

	/** 解析样式类属性值 → 颜色或 none 或渐变引用。OutGradient 非空（仅填充传入，描边不要渐变）
	 *  且引用的是 stops ≥2 的线性渐变时输出渐变定义（Opacity 已烘入 stops 透明度）；
	 *  径向渐变不输出（回退平均色）。 */
	static TOptional<FLinearColor> ParsePaintValue(const FString& Value, const TMap<FString, FGradientDef>& Gradients, float Opacity, bool& bOutRecognized,
		TOptional<FSCADASvgLinearGradient>* OutGradient = nullptr)
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
			if (const FGradientDef* Def = Gradients.Find(Ref))
			{
				Result = Def->Average;
				if (OutGradient != nullptr && !Def->bRadial && Def->Linear.Stops.Num() >= 2)
				{
					*OutGradient = Def->Linear;
				}
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
		if (OutGradient != nullptr && OutGradient->IsSet() && Opacity < 1.0f)
		{
			for (TPair<float, FLinearColor>& S : OutGradient->GetValue().Stops)
			{
				S.Value.A *= Opacity;
			}
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

	// ———— path 数据解析：贝塞尔/椭圆弧采样为点列（导入期一次性烘焙，契合"点列真值"数据模型）————

	/** 从 path 数据流读一个数（SVG 数值语法：符号可直连下一数 "10-5"=10,-5；双点 "0.4.3"=0.4,.3） */
	static bool ReadPathNumber(const FString& D, int32& Pos, float& Out)
	{
		while (Pos < D.Len() && (FChar::IsWhitespace(D[Pos]) || D[Pos] == TEXT(','))) { ++Pos; }
		if (Pos >= D.Len() || FChar::IsAlpha(D[Pos]))
		{
			return false;
		}
		const int32 Start = Pos;
		if (D[Pos] == TEXT('+') || D[Pos] == TEXT('-')) { ++Pos; }
		bool bDot = false;
		while (Pos < D.Len())
		{
			const TCHAR C = D[Pos];
			if (FChar::IsDigit(C)) { ++Pos; continue; }
			if (C == TEXT('.') && !bDot) { bDot = true; ++Pos; continue; }
			break;
		}
		if (Pos < D.Len() && (D[Pos] == TEXT('e') || D[Pos] == TEXT('E')))
		{
			int32 E = Pos + 1;
			if (E < D.Len() && (D[E] == TEXT('+') || D[E] == TEXT('-'))) { ++E; }
			if (E < D.Len() && FChar::IsDigit(D[E]))
			{
				Pos = E;
				while (Pos < D.Len() && FChar::IsDigit(D[Pos])) { ++Pos; }
			}
		}
		if (Pos == Start || (Pos == Start + 1 && (D[Start] == TEXT('+') || D[Start] == TEXT('-'))))
		{
			Pos = Start;
			return false;
		}
		Out = FCString::Atof(*D.Mid(Start, Pos - Start));
		return true;
	}

	/** 三次贝塞尔按近似长度自适应采样（~2px 一段、最短 2 段——小尺寸图标不产生点挨点的密集顶点），含终点、不含起点 */
	static void SampleCubicBezier(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, TArray<FVector2D>& Out)
	{
		const double ApproxLen = (P1 - P0).Size() + (P2 - P1).Size() + (P3 - P2).Size();
		const int32 N = FMath::Clamp(FMath::CeilToInt((float)ApproxLen / 2.0f), 2, 64);
		for (int32 i = 1; i <= N; ++i)
		{
			const double t = (double)i / N, u = 1.0 - t;
			Out.Add(u * u * u * P0 + 3.0 * u * u * t * P1 + 3.0 * u * t * t * P2 + t * t * t * P3);
		}
	}

	/** 二次贝塞尔采样，含终点、不含起点 */
	static void SampleQuadraticBezier(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, TArray<FVector2D>& Out)
	{
		const double ApproxLen = (P1 - P0).Size() + (P2 - P1).Size();
		const int32 N = FMath::Clamp(FMath::CeilToInt((float)ApproxLen / 2.0f), 2, 64);
		for (int32 i = 1; i <= N; ++i)
		{
			const double t = (double)i / N, u = 1.0 - t;
			Out.Add(u * u * P0 + 2.0 * u * t * P1 + t * t * P2);
		}
	}

	/** SVG 椭圆弧（端点参数化 → 中心参数化，规范 F.6.6）采样，含终点、不含起点 */
	static void SampleEllipticalArc(const FVector2D& P0, float Rx, float Ry, float RotDeg,
		bool bLargeArc, bool bSweep, const FVector2D& P1, TArray<FVector2D>& Out)
	{
		Rx = FMath::Abs(Rx);
		Ry = FMath::Abs(Ry);
		if (Rx < 1e-4f || Ry < 1e-4f || (P1 - P0).IsNearlyZero())
		{
			Out.Add(P1);
			return;
		}
		const double Phi = FMath::DegreesToRadians(RotDeg);
		const double CosP = FMath::Cos(Phi), SinP = FMath::Sin(Phi);
		const double Dx = (P0.X - P1.X) * 0.5, Dy = (P0.Y - P1.Y) * 0.5;
		const double X1p = CosP * Dx + SinP * Dy;
		const double Y1p = -SinP * Dx + CosP * Dy;

		double Rxd = Rx, Ryd = Ry;
		const double Lam = X1p * X1p / (Rxd * Rxd) + Y1p * Y1p / (Ryd * Ryd);
		if (Lam > 1.0)
		{
			const double S = FMath::Sqrt(Lam);
			Rxd *= S;
			Ryd *= S;
		}

		const double Num = Rxd * Rxd * Ryd * Ryd - Rxd * Rxd * Y1p * Y1p - Ryd * Ryd * X1p * X1p;
		const double Den = Rxd * Rxd * Y1p * Y1p + Ryd * Ryd * X1p * X1p;
		const double Coef = (bLargeArc != bSweep ? 1.0 : -1.0) * FMath::Sqrt(FMath::Max(0.0, Num / FMath::Max(Den, 1e-12)));
		const double Cxp = Coef * Rxd * Y1p / Ryd;
		const double Cyp = -Coef * Ryd * X1p / Rxd;
		const double Cx = CosP * Cxp - SinP * Cyp + (P0.X + P1.X) * 0.5;
		const double Cy = SinP * Cxp + CosP * Cyp + (P0.Y + P1.Y) * 0.5;

		auto VecAngle = [](double Ux, double Uy, double Vx, double Vy)
		{
			const double Dot = Ux * Vx + Uy * Vy;
			const double Len = FMath::Sqrt((Ux * Ux + Uy * Uy) * (Vx * Vx + Vy * Vy));
			double Ang = FMath::Acos(FMath::Clamp(Dot / FMath::Max(Len, 1e-12), -1.0, 1.0));
			if (Ux * Vy - Uy * Vx < 0.0) { Ang = -Ang; }
			return Ang;
		};
		const double Theta1 = VecAngle(1.0, 0.0, (X1p - Cxp) / Rxd, (Y1p - Cyp) / Ryd);
		double DTheta = VecAngle((X1p - Cxp) / Rxd, (Y1p - Cyp) / Ryd, (-X1p - Cxp) / Rxd, (-Y1p - Cyp) / Ryd);
		const double TwoPi = 2.0 * PI;
		if (!bSweep && DTheta > 0.0) { DTheta -= TwoPi; }
		if (bSweep && DTheta < 0.0) { DTheta += TwoPi; }

		const double ApproxLen = FMath::Max(Rxd, Ryd) * FMath::Abs(DTheta);
		const int32 N = FMath::Clamp(FMath::CeilToInt((float)ApproxLen / 2.0f), 2, 128);
		for (int32 i = 1; i <= N; ++i)
		{
			const double T = Theta1 + DTheta * (double)i / N;
			Out.Add(FVector2D(
				CosP * Rxd * FMath::Cos(T) - SinP * Ryd * FMath::Sin(T) + Cx,
				SinP * Rxd * FMath::Cos(T) + CosP * Ryd * FMath::Sin(T) + Cy));
		}
	}

	/** 解析 path 的 d 属性为子路径点列（M/L/H/V/C/S/Q/T/A/Z，含相对命令）。
	 *  OutClosed[i] = 第 i 条子路径是否以 Z 闭合。贝塞尔/弧已采样折线化。 */
	static void ParsePathData(const FString& D, TArray<TArray<FVector2D>>& OutSubpaths, TArray<bool>& OutClosed)
	{
		int32 Pos = 0;
		TCHAR Cmd = 0;
		FVector2D Cur(0.0, 0.0), StartPt(0.0, 0.0);
		FVector2D PrevCubicCtrl(0.0, 0.0), PrevQuadCtrl(0.0, 0.0);
		bool bHasCubicCtrl = false, bHasQuadCtrl = false;
		int32 CurSub = INDEX_NONE;

		auto NextIsNumber = [&D](int32 P)
		{
			while (P < D.Len() && (FChar::IsWhitespace(D[P]) || D[P] == TEXT(','))) { ++P; }
			return P < D.Len() && !FChar::IsAlpha(D[P]);
		};

		while (Pos < D.Len())
		{
			while (Pos < D.Len() && (FChar::IsWhitespace(D[Pos]) || D[Pos] == TEXT(','))) { ++Pos; }
			if (Pos >= D.Len())
			{
				break;
			}
			if (FChar::IsAlpha(D[Pos]))
			{
				Cmd = D[Pos];
				++Pos;
			}
			else if (Cmd == 0)
			{
				break; // 数据不以命令开头，非法
			}

			TCHAR U = FChar::ToUpper(Cmd);
			bool bRel = FChar::IsLower(Cmd);
			if (U == TEXT('Z'))
			{
				if (CurSub != INDEX_NONE)
				{
					OutClosed[CurSub] = true;
				}
				Cur = StartPt;
				CurSub = INDEX_NONE;
				Cmd = 0;
				bHasCubicCtrl = bHasQuadCtrl = false;
				continue;
			}

			bool bFirstOfM = (U == TEXT('M'));
			while (true)
			{
				if (U == TEXT('M') && !bFirstOfM)
				{
					U = TEXT('L'); // M 后续坐标对按隐式 L 处理
				}

				if (U == TEXT('M') || U == TEXT('L'))
				{
					float X, Y;
					if (!ReadPathNumber(D, Pos, X) || !ReadPathNumber(D, Pos, Y)) { break; }
					Cur = bRel ? Cur + FVector2D(X, Y) : FVector2D(X, Y);
					if (U == TEXT('M'))
					{
						CurSub = OutSubpaths.AddDefaulted();
						OutClosed.Add(false);
						StartPt = Cur;
					}
					if (CurSub == INDEX_NONE) { CurSub = OutSubpaths.AddDefaulted(); OutClosed.Add(false); StartPt = Cur; }
					OutSubpaths[CurSub].Add(Cur);
					bHasCubicCtrl = bHasQuadCtrl = false;
				}
				else if (U == TEXT('H'))
				{
					float X;
					if (!ReadPathNumber(D, Pos, X)) { break; }
					Cur.X = bRel ? Cur.X + X : X;
					if (CurSub != INDEX_NONE) { OutSubpaths[CurSub].Add(Cur); }
					bHasCubicCtrl = bHasQuadCtrl = false;
				}
				else if (U == TEXT('V'))
				{
					float Y;
					if (!ReadPathNumber(D, Pos, Y)) { break; }
					Cur.Y = bRel ? Cur.Y + Y : Y;
					if (CurSub != INDEX_NONE) { OutSubpaths[CurSub].Add(Cur); }
					bHasCubicCtrl = bHasQuadCtrl = false;
				}
				else if (U == TEXT('C'))
				{
					float X1, Y1, X2, Y2, X, Y;
					if (!ReadPathNumber(D, Pos, X1) || !ReadPathNumber(D, Pos, Y1) ||
						!ReadPathNumber(D, Pos, X2) || !ReadPathNumber(D, Pos, Y2) ||
						!ReadPathNumber(D, Pos, X) || !ReadPathNumber(D, Pos, Y)) { break; }
					const FVector2D Off = bRel ? Cur : FVector2D::ZeroVector;
					const FVector2D C1 = Off + FVector2D(X1, Y1), C2 = Off + FVector2D(X2, Y2), End = Off + FVector2D(X, Y);
					if (CurSub != INDEX_NONE) { SampleCubicBezier(Cur, C1, C2, End, OutSubpaths[CurSub]); }
					PrevCubicCtrl = C2;
					bHasCubicCtrl = true;
					bHasQuadCtrl = false;
					Cur = End;
				}
				else if (U == TEXT('S'))
				{
					float X2, Y2, X, Y;
					if (!ReadPathNumber(D, Pos, X2) || !ReadPathNumber(D, Pos, Y2) ||
						!ReadPathNumber(D, Pos, X) || !ReadPathNumber(D, Pos, Y)) { break; }
					const FVector2D Off = bRel ? Cur : FVector2D::ZeroVector;
					const FVector2D C1 = bHasCubicCtrl ? 2.0 * Cur - PrevCubicCtrl : Cur;
					const FVector2D C2 = Off + FVector2D(X2, Y2), End = Off + FVector2D(X, Y);
					if (CurSub != INDEX_NONE) { SampleCubicBezier(Cur, C1, C2, End, OutSubpaths[CurSub]); }
					PrevCubicCtrl = C2;
					bHasCubicCtrl = true;
					bHasQuadCtrl = false;
					Cur = End;
				}
				else if (U == TEXT('Q'))
				{
					float X1, Y1, X, Y;
					if (!ReadPathNumber(D, Pos, X1) || !ReadPathNumber(D, Pos, Y1) ||
						!ReadPathNumber(D, Pos, X) || !ReadPathNumber(D, Pos, Y)) { break; }
					const FVector2D Off = bRel ? Cur : FVector2D::ZeroVector;
					const FVector2D C1 = Off + FVector2D(X1, Y1), End = Off + FVector2D(X, Y);
					if (CurSub != INDEX_NONE) { SampleQuadraticBezier(Cur, C1, End, OutSubpaths[CurSub]); }
					PrevQuadCtrl = C1;
					bHasQuadCtrl = true;
					bHasCubicCtrl = false;
					Cur = End;
				}
				else if (U == TEXT('T'))
				{
					float X, Y;
					if (!ReadPathNumber(D, Pos, X) || !ReadPathNumber(D, Pos, Y)) { break; }
					const FVector2D Off = bRel ? Cur : FVector2D::ZeroVector;
					const FVector2D C1 = bHasQuadCtrl ? 2.0 * Cur - PrevQuadCtrl : Cur;
					const FVector2D End = Off + FVector2D(X, Y);
					if (CurSub != INDEX_NONE) { SampleQuadraticBezier(Cur, C1, End, OutSubpaths[CurSub]); }
					PrevQuadCtrl = C1;
					bHasQuadCtrl = true;
					bHasCubicCtrl = false;
					Cur = End;
				}
				else if (U == TEXT('A'))
				{
					float Rx, Ry, Rot, LargeF, SweepF, X, Y;
					if (!ReadPathNumber(D, Pos, Rx) || !ReadPathNumber(D, Pos, Ry) || !ReadPathNumber(D, Pos, Rot) ||
						!ReadPathNumber(D, Pos, LargeF) || !ReadPathNumber(D, Pos, SweepF) ||
						!ReadPathNumber(D, Pos, X) || !ReadPathNumber(D, Pos, Y)) { break; }
					const FVector2D End = bRel ? Cur + FVector2D(X, Y) : FVector2D(X, Y);
					if (CurSub != INDEX_NONE) { SampleEllipticalArc(Cur, Rx, Ry, Rot, LargeF != 0.0f, SweepF != 0.0f, End, OutSubpaths[CurSub]); }
					bHasCubicCtrl = bHasQuadCtrl = false;
					Cur = End;
				}
				else
				{
					UE_LOG(LogSCADASvg, Warning, TEXT("path 含未支持命令 '%c'，其后数据跳过"), U);
					return;
				}
				bFirstOfM = false;
				if (!NextIsNumber(Pos))
				{
					break; // 命令组结束，回到外层读下一命令字母
				}
			}
		}
	}

	/** 解析 g 的 transform 列表：全部 translate 累加进 OutTranslation；识别 rotate(a[,cx,cy])；
	 *  scale/matrix/skew 警告忽略。translate 与 rotate 混排时的顺序语义按近似处理（先平移后旋转）。
	 *  只有"保留容器节点"的老路在用——能被精确烘进几何的变换在分组分支里已先行处理掉了。 */
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

	/** 只有**轴对齐**仿射才能把轴对齐矩形仍映射成轴对齐矩形，因而能无损烘进图元几何：
	 *  B=C=0（translate / scale / 镜像 / 只含对角的 matrix）或 A=D=0（90° 整数倍旋转 ± 缩放镜像）。
	 *  带 skew 或任意角度旋转的映射不出矩形，只能走"保留容器节点"的老路。
	 *  A=D=0 这一路正是 Visio 的整页旋转 + 左右镜像 scale(-1,1) 组合出来的形态。 */
	static bool IsAxisAlignedAffine(const FSvgAffine& M)
	{
		if (FMath::Abs(M.A * M.D - M.B * M.C) < 1e-9)
		{
			return false; // scale(0,..) / 奇异 matrix：面积本该塌成 0，不硬烘
		}
		const bool bStraight = FMath::Abs(M.B) < 1e-9 && FMath::Abs(M.C) < 1e-9;
		const bool bSwapped = FMath::Abs(M.A) < 1e-9 && FMath::Abs(M.D) < 1e-9;
		return bStraight || bSwapped;
	}

	/** 线性部是否恒等（= 纯平移组，走更直接的摊平路径，不必过这道换算） */
	static bool IsPureTranslateAffine(const FSvgAffine& M)
	{
		return FMath::Abs(M.A - 1.0) < 1e-9 && FMath::Abs(M.D - 1.0) < 1e-9
			&& FMath::Abs(M.B) < 1e-9 && FMath::Abs(M.C) < 1e-9;
	}

	/** 分组变换的可表示性分级 —— 决定"烘进几何"还是"保留带 RenderTransform 的容器节点" */
	enum class ESCADASvgXformKind : uint8
	{
		Translate,          ///< 没有 transform 或纯平移：偏移直接累加进子坐标
		AxisAligned,        ///< 轴对齐（缩放 / 镜像 scale(-1,1) / 对角 matrix / 90° 整数倍旋转）：图元全部无损换算
		RotatedSafe,        ///< 任意角度旋转（可带缩放，不含 skew）：只有逐点映射得动的图元能烘，见 SubtreeIsRotationSafe
		Unrepresentable     ///< skew / 奇异 matrix / 解析失败：保留容器节点
	};

	/** 解析分组 transform 为真实矩阵（各算子按书写顺序相乘，SVG 语义）并分级；空 transform 记单位阵。 */
	static ESCADASvgXformKind ClassifyGroupTransform(const FString& Value, FSvgAffine& OutM)
	{
		OutM = FSvgAffine();
		if (Value.IsEmpty())
		{
			return ESCADASvgXformKind::Translate;
		}
		if (!TryParseAffineTransform(Value, OutM))
		{
			return ESCADASvgXformKind::Unrepresentable;
		}
		if (FMath::Abs(OutM.A * OutM.D - OutM.B * OutM.C) < 1e-9)
		{
			return ESCADASvgXformKind::Unrepresentable; // scale(0,..) / 奇异 matrix：面积塌成 0
		}
		if (IsPureTranslateAffine(OutM))
		{
			return ESCADASvgXformKind::Translate;
		}
		if (IsAxisAlignedAffine(OutM))
		{
			return ESCADASvgXformKind::AxisAligned;
		}
		// 线性部两列正交 = 旋转（可带不等比缩放），不是 skew
		const double Dot = OutM.A * OutM.B + OutM.C * OutM.D;
		const double ScaleU2 = OutM.A * OutM.A + OutM.C * OutM.C;
		const double ScaleV2 = OutM.B * OutM.B + OutM.D * OutM.D;
		if (FMath::Abs(Dot) <= 1e-9 * FMath::Sqrt(ScaleU2 * ScaleV2))
		{
			return ESCADASvgXformKind::RotatedSafe;
		}
		return ESCADASvgXformKind::Unrepresentable;
	}

	/** 等比缩放？（圆只有在等比下才还是圆） */
	static bool IsUniformScaleAffine(const FSvgAffine& M)
	{
		const double ScaleU2 = M.A * M.A + M.C * M.C;
		return FMath::Abs(ScaleU2 - (M.B * M.B + M.D * M.D)) <= 1e-9 * FMath::Max(ScaleU2, 1e-9);
	}

	/** 任意角度旋转下哪些图元还画得对：path / polyline / polygon / line 本来就是逐点映射的点列模型 ✓；
	 *  没有圆角的 rect 展开成四点 polygon，几何完全等价 ✓；圆在等比缩放下仍是圆 ✓。
	 *  画不动的是带圆角的 rect（展开会丢圆角）与椭圆（图元模型没有"旋转角"这个参数），
	 *  以及非等比缩放下的圆 —— 有这些就别硬烘，保留容器。
	 *  （注：这里直接 Atof 而不走 ParseLength——本函数定义在它之前，且 rx/ry 带 px 单位的写法罕见。） */
	static bool SubtreeIsRotationSafe(const FXmlNode* Group, bool bUniformScale)
	{
		for (const FXmlNode* Child : Group->GetChildrenNodes())
		{
			const FString Tag = Child->GetTag().ToLower();
			if (Tag == TEXT("rect"))
			{
				if (FCString::Atof(*Child->GetAttribute(TEXT("rx"))) > 0.0 || FCString::Atof(*Child->GetAttribute(TEXT("ry"))) > 0.0)
				{
					return false;
				}
			}
			else if (Tag == TEXT("ellipse"))
			{
				return false;
			}
			else if (Tag == TEXT("circle") && !bUniformScale)
			{
				return false;
			}
			if (!SubtreeIsRotationSafe(Child, bUniformScale))
			{
				return false;
			}
		}
		return true;
	}

	/** 这个分组自己的变换能不能烘进几何（RotatedSafe 时连子树里的图元类型一起判）。
	 *  OutKind 回传分级，供调用方选哪条换算路径。 */
	static bool GroupTransformIsBakeable(const FXmlNode* Group, ESCADASvgXformKind& OutKind)
	{
		FSvgAffine M;
		OutKind = ClassifyGroupTransform(Group->GetAttribute(TEXT("transform")), M);
		if (OutKind == ESCADASvgXformKind::Unrepresentable)
		{
			return false;
		}
		if (OutKind == ESCADASvgXformKind::RotatedSafe)
		{
			return SubtreeIsRotationSafe(Group, IsUniformScaleAffine(M));
		}
		return true;
	}

	/** 图元的紧包围盒（渐变轴换算要用；Rect/Circle/Ellipse 走解析式，点列取顶点） */
	static void CollectNodeBox(const FSCADASvgNode& Node, FBox2D& OutBox)
	{
		switch (Node.Kind)
		{
		case ESCADASvgNodeKind::Rect:
			OutBox += Node.P1;
			OutBox += Node.P1 + Node.P2;
			break;
		case ESCADASvgNodeKind::Circle:
			OutBox += Node.P1 - FVector2D(Node.Radius, Node.Radius);
			OutBox += Node.P1 + FVector2D(Node.Radius, Node.Radius);
			break;
		case ESCADASvgNodeKind::Ellipse:
			OutBox += Node.P1 - Node.P2;
			OutBox += Node.P1 + Node.P2;
			break;
		case ESCADASvgNodeKind::Line:
			OutBox += Node.P1;
			OutBox += Node.P2;
			break;
		case ESCADASvgNodeKind::Polyline:
		case ESCADASvgNodeKind::Polygon:
			for (const FVector2D& Point : Node.Points)
			{
				OutBox += Point;
			}
			break;
		case ESCADASvgNodeKind::Group:
			break;
		}
	}

	/** objectBoundingBox 渐变的归一化轴随图元一起搬：旧包围盒还原成世界点 → 同一个仿射 →
	 *  新包围盒重新归一化。线性渐变完全由两个轴端点决定，端点搬到正确处，画出来的渐变方向就
	 *  和浏览器一致（轴对齐路径里那套符号/换轴映射是它的退化特例，那边已有更省事的写法）。 */
	static FVector2D RemapNormalizedAxis(const FVector2D& Axis, const FBox2D& OldBox, const FBox2D& NewBox,
		const TFunction<FVector2D(const FVector2D&)>& Map)
	{
		const FVector2D OldSize = OldBox.Max - OldBox.Min;
		const FVector2D NewSize = NewBox.Max - NewBox.Min;
		if (!OldBox.bIsValid || !NewBox.bIsValid || OldSize.X <= KINDA_SMALL_NUMBER || OldSize.Y <= KINDA_SMALL_NUMBER)
		{
			return Axis;
		}
		const FVector2D World = Map(OldBox.Min + FVector2D(Axis.X * OldSize.X, Axis.Y * OldSize.Y));
		return FVector2D(
			NewSize.X > KINDA_SMALL_NUMBER ? (World.X - NewBox.Min.X) / NewSize.X : 0.5,
			NewSize.Y > KINDA_SMALL_NUMBER ? (World.Y - NewBox.Min.Y) / NewSize.Y : 0.5);
	}

	/** 把轴对齐仿射作用到已产出的一批节点上（p' = Origin + M(p - Origin)，Origin = 已烘入的
	 *  父链累计平移）。镜像 / 缩放 / 90° 整数倍都是精确换算：rect 取四角变换后的外接矩形
	 *  （仍轴对齐）；圆按两轴伸长率转成椭圆（SVG 语义下 scale 本来就会把圆变成椭圆）；椭圆、
	 *  线段、点列同理逐点/逐轴换算。圆角与半径按各自**源轴**的伸长率缩放，映射到另一条轴时交换。
	 *  线宽按 SVG 语义随变换缩放，两轴不等时取几何均值折中（各向异性笔画在单一线宽模型里只能
	 *  取一个代表值）。objectBoundingBox 渐变的轴是相对包围盒的归一化量，镜像/换轴同样要跟着
	 *  映射，否则镜像那半边的渐变方向和另一半相反。 */
	static void ApplyAxisAlignedAffineToNodes(TArray<FSCADASvgNode>& Nodes, FSvgAffine M, const FVector2D& Origin)
	{
		// 清掉三角函数的亚噪声（cos(90°) 是 6e-17 而不是 0），免得把后面的像素对齐打散
		auto Clean = [](double& V) { if (FMath::Abs(V) < 1e-9) { V = 0.0; } };
		Clean(M.A); Clean(M.B); Clean(M.TX); Clean(M.C); Clean(M.D); Clean(M.TY);

		auto Map = [&](const FVector2D& P)
		{
			const double X = P.X - Origin.X;
			const double Y = P.Y - Origin.Y;
			return Origin + FVector2D(M.A * X + M.B * Y + M.TX, M.C * X + M.D * Y + M.TY);
		};
		const double SrcXStretch = FMath::Sqrt(M.A * M.A + M.C * M.C);
		const double SrcYStretch = FMath::Sqrt(M.B * M.B + M.D * M.D);
		const bool bSwapAxes = FMath::Abs(M.A) + FMath::Abs(M.D) < FMath::Abs(M.B) + FMath::Abs(M.C);
		const double StrokeScale = FMath::Sqrt(SrcXStretch * SrcYStretch);

		// 归一化渐变轴的映射 = 线性部的符号与换轴（两路只有一路非零），被翻转的那一侧补回 1 的偏移
		auto Sign = [](double V) { return V < 0.0 ? -1.0 : (V > 0.0 ? 1.0 : 0.0); };
		const double NA = Sign(M.A), NB = Sign(M.B), NC = Sign(M.C), ND = Sign(M.D);
		auto MapAxis = [&](const FVector2D& Axis)
		{
			return FVector2D(NA * Axis.X + NB * Axis.Y + (NA < 0.0 || NB < 0.0 ? 1.0 : 0.0),
				NC * Axis.X + ND * Axis.Y + (NC < 0.0 || ND < 0.0 ? 1.0 : 0.0));
		};

		for (FSCADASvgNode& Node : Nodes)
		{
			Node.Style.StrokeWidth *= (float)StrokeScale;
			if (Node.Style.FillGradient.IsSet() && Node.Style.FillGradient->bObjectBoundingBox)
			{
				FSCADASvgLinearGradient& G = Node.Style.FillGradient.GetValue();
				G.AxisStart = MapAxis(G.AxisStart);
				G.AxisEnd = MapAxis(G.AxisEnd);
			}

			switch (Node.Kind)
			{
			case ESCADASvgNodeKind::Rect:
				{
					FVector2D Min = Map(Node.P1);
					FVector2D Max = Min;
					const FVector2D Corners[3] = {
						Node.P1 + FVector2D(Node.P2.X, 0.0), Node.P1 + Node.P2, Node.P1 + FVector2D(0.0, Node.P2.Y)
					};
					for (const FVector2D& Corner : Corners)
					{
						const FVector2D P = Map(Corner);
						Min = FVector2D::Min(Min, P);
						Max = FVector2D::Max(Max, P);
					}
					Node.P1 = Min;
					Node.P2 = Max - Min;
					const float Rx = (float)(Node.CornerRadius.X * SrcXStretch);
					const float Ry = (float)(Node.CornerRadius.Y * SrcYStretch);
					Node.CornerRadius = bSwapAxes ? FVector2D(Ry, Rx) : FVector2D(Rx, Ry);
				}
				break;
			case ESCADASvgNodeKind::Circle:
				{
					Node.P1 = Map(Node.P1);
					if (FMath::Abs(SrcXStretch - SrcYStretch) > 1e-6)
					{
						const float Rx = (float)(Node.Radius * SrcXStretch);
						const float Ry = (float)(Node.Radius * SrcYStretch);
						Node.Kind = ESCADASvgNodeKind::Ellipse;
						Node.P2 = bSwapAxes ? FVector2D(Ry, Rx) : FVector2D(Rx, Ry);
						Node.Radius = 0.0f;
					}
					else
					{
						Node.Radius *= (float)SrcXStretch;
					}
				}
				break;
			case ESCADASvgNodeKind::Ellipse:
				{
					Node.P1 = Map(Node.P1);
					const float Rx = (float)(Node.P2.X * SrcXStretch);
					const float Ry = (float)(Node.P2.Y * SrcYStretch);
					Node.P2 = bSwapAxes ? FVector2D(Ry, Rx) : FVector2D(Rx, Ry);
				}
				break;
			case ESCADASvgNodeKind::Line:
				Node.P1 = Map(Node.P1);
				Node.P2 = Map(Node.P2);
				break;
			case ESCADASvgNodeKind::Polyline:
			case ESCADASvgNodeKind::Polygon:
				for (FVector2D& Point : Node.Points)
				{
					Point = Map(Point);
				}
				break;
			case ESCADASvgNodeKind::Group:
				// 纯 translate 的组在解析层已被摊平；带旋转的组由调用方的子树检查挡在外面，不会到这儿
				break;
			}
		}
	}

	/** 任意角度旋转（可带缩放）的换算路径。调用方已确认子树里没有椭圆、没有带圆角的 rect、
	 *  非等比时也没有圆，所以这里处理的每种图元都是逐点映射得动的：
	 *  - rect → 四点 polygon（无圆角时几何完全等价）；
	 *  - 圆 → 仍是圆（等比）；
	 *  - 线段 / 折线 / 多边形 → 逐点。
	 *  旋转后的顶点是无理数，顺手置 bSubPixelSampling（工厂据此关掉数据层取整，否则斜边会打阶梯——
	 *  与 SVG path 采样导入同一套处理）。线宽按线性部的面积比缩放（等比时就是 |缩放|）。 */
	static void ApplyRotationToNodes(TArray<FSCADASvgNode>& Nodes, FSvgAffine M, const FVector2D& Origin)
	{
		auto Clean = [](double& V) { if (FMath::Abs(V) < 1e-9) { V = 0.0; } };
		Clean(M.A); Clean(M.B); Clean(M.TX); Clean(M.C); Clean(M.D); Clean(M.TY);

		auto Map = [&](const FVector2D& P)
		{
			const double X = P.X - Origin.X;
			const double Y = P.Y - Origin.Y;
			return Origin + FVector2D(M.A * X + M.B * Y + M.TX, M.C * X + M.D * Y + M.TY);
		};
		const double Stretch = FMath::Sqrt(FMath::Abs(M.A * M.D - M.B * M.C));

		for (FSCADASvgNode& Node : Nodes)
		{
			FBox2D OldBox(ForceInit);
			CollectNodeBox(Node, OldBox);

			Node.Style.StrokeWidth *= (float)Stretch;
			Node.bSubPixelSampling = true;

			switch (Node.Kind)
			{
			case ESCADASvgNodeKind::Rect:
				{
					const FVector2D Corners[4] = {
						Node.P1,
						Node.P1 + FVector2D(Node.P2.X, 0.0),
						Node.P1 + Node.P2,
						Node.P1 + FVector2D(0.0, Node.P2.Y)
					};
					Node.Kind = ESCADASvgNodeKind::Polygon;
					Node.Points.Reset(4);
					for (const FVector2D& Corner : Corners)
					{
						Node.Points.Add(Map(Corner));
					}
					Node.P2 = FVector2D::ZeroVector;
					Node.CornerRadius = FVector2D::ZeroVector;
				}
				break;
			case ESCADASvgNodeKind::Circle:
				Node.P1 = Map(Node.P1);
				Node.Radius *= (float)Stretch;
				break;
			case ESCADASvgNodeKind::Ellipse:
				// 正常到不了这儿（安全检查会拦住椭圆），保底只搬中心、半径按比例缩放，长短轴方向不转
				Node.P1 = Map(Node.P1);
				Node.P2 *= (float)Stretch;
				break;
			case ESCADASvgNodeKind::Line:
				Node.P1 = Map(Node.P1);
				Node.P2 = Map(Node.P2);
				break;
			case ESCADASvgNodeKind::Polyline:
			case ESCADASvgNodeKind::Polygon:
				for (FVector2D& Point : Node.Points)
				{
					Point = Map(Point);
				}
				break;
			case ESCADASvgNodeKind::Group:
				// 会产出容器节点的后代已被 SubtreeHasUnbakeableGroup 挡在门外
				break;
			}

			if (Node.Style.FillGradient.IsSet() && Node.Style.FillGradient->bObjectBoundingBox)
			{
				FBox2D NewBox(ForceInit);
				CollectNodeBox(Node, NewBox);
				FSCADASvgLinearGradient& G = Node.Style.FillGradient.GetValue();
				G.AxisStart = RemapNormalizedAxis(G.AxisStart, OldBox, NewBox, Map);
				G.AxisEnd = RemapNormalizedAxis(G.AxisEnd, OldBox, NewBox, Map);
			}
		}
	}

	/** 子树里是否存在会退化成容器节点的分组（自己的变换烘不进几何）。
	 *  非轴对齐的父变换搬不动"槽矩形 + RenderTransform"这种容器（槽矩形是不转的），
	 *  所以有这种后代时整组都不烘，一起走容器路径。
	 *  注意别再退回成"子树里出现 rotate 就拒绝"：Visio 用父 rotate(90) + 子 rotate(-90)
	 *  给整页旋转里的文字反向转正，两级各自都能烘，合成后更是干脆没旋转——按字符串判断会把
	 *  整页锁进一个嵌套画布（实测 10 个 Visio 文件里 63 个画布堆叠全这么来的）。 */
	static bool SubtreeHasUnbakeableGroup(const FXmlNode* Node)
	{
		for (const FXmlNode* Child : Node->GetChildrenNodes())
		{
			ESCADASvgXformKind Kind = ESCADASvgXformKind::Unrepresentable;
			if (Child->GetTag().Equals(TEXT("g"), ESearchCase::IgnoreCase) && !GroupTransformIsBakeable(Child, Kind))
			{
				return true;
			}
			if (SubtreeHasUnbakeableGroup(Child))
			{
				return true;
			}
		}
		return false;
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

	/** 元素最终样式 = 父样式（组继承）+ 本元素的样式取值。GetValue 按属性名给出该元素生效的样式值
	 *  （调用方负责 "CSS 声明优先于同名 presentation attribute" 的级联），空串 = 未声明。
	 *  参与计算的属性：opacity / fill-opacity / stroke-opacity / fill / stroke / stroke-width。
	 *  "inherit" 关键字（SVG2/CSS）= 取父级计算值：与缺省同路径处理（我们的样式本就沿组继承，
	 *  保持父样式不动即可），不会再落入"无法解析颜色值"被当 none 清空填充 */
	static FSCADASvgStyle ComputeElementStyle(const FSCADASvgStyle& ParentStyle,
		const TFunction<FString(const TCHAR*)>& GetValue,
		const TMap<FString, FGradientDef>& Gradients, const FVector2D& ViewBoxMin)
	{
		FSCADASvgStyle Style = ParentStyle;

		const FString Opacity = GetValue(TEXT("opacity"));
		const float Op = Opacity.IsEmpty() ? 1.0f : FMath::Clamp(FCString::Atof(*Opacity), 0.0f, 1.0f);
		const FString FillOpacity = GetValue(TEXT("fill-opacity"));
		const float FOp = (FillOpacity.IsEmpty() ? 1.0f : FCString::Atof(*FillOpacity)) * Op;
		const FString StrokeOpacity = GetValue(TEXT("stroke-opacity"));
		const float SOp = (StrokeOpacity.IsEmpty() ? 1.0f : FCString::Atof(*StrokeOpacity)) * Op;

		const FString FillValue = GetValue(TEXT("fill"));
		if (!FillValue.IsEmpty() && !FillValue.Equals(TEXT("inherit"), ESearchCase::IgnoreCase))
		{
			bool bRecognized = true;
			TOptional<FSCADASvgLinearGradient> FillGrad;
			Style.Fill = ParsePaintValue(FillValue, Gradients, FOp, bRecognized, &FillGrad);
			if (FillGrad.IsSet() && !FillGrad->bObjectBoundingBox)
			{
				// userSpaceOnUse 轴 = 原始 SVG 坐标：与叶子坐标同样做 -ViewBoxMin 修正
				// （轴在元素用户空间内解释、包围盒含组链累计平移，两边语义一致）
				FillGrad->AxisStart -= ViewBoxMin;
				FillGrad->AxisEnd -= ViewBoxMin;
			}
			Style.FillGradient = FillGrad;
		}
		else if (FOp < 1.0f)
		{
			// 继承样式的 fill-opacity 级联：纯色与渐变 stops 的透明度都要乘
			if (Style.Fill.IsSet())
			{
				Style.Fill->A *= FOp;
			}
			if (Style.FillGradient.IsSet())
			{
				for (TPair<float, FLinearColor>& S : Style.FillGradient->Stops)
				{
					S.Value.A *= FOp;
				}
			}
		}

		const FString StrokeValue = GetValue(TEXT("stroke"));
		if (!StrokeValue.IsEmpty() && !StrokeValue.Equals(TEXT("inherit"), ESearchCase::IgnoreCase))
		{
			bool bRecognized = true;
			Style.Stroke = ParsePaintValue(StrokeValue, Gradients, SOp, bRecognized);
		}
		else if (Style.Stroke.IsSet() && SOp < 1.0f)
		{
			Style.Stroke->A *= SOp;
		}

		const FString StrokeWidthValue = GetValue(TEXT("stroke-width"));
		if (!StrokeWidthValue.IsEmpty())
		{
			Style.StrokeWidth = FMath::Max(ParseLength(StrokeWidthValue, 1.0f), 0.0f);
		}
		return Style;
	}

	/** 递归解析元素节点，产物追加到 OutNodes（一个 XML 元素可产 0..N 个节点——无旋转组扁平化时
	 *  组本身不产出、子节点直接挂父级）。样式沿组继承，本元素的样式值 = 样式表声明优先于同名属性；
	 *  AccumOffset = 父链 translate 与 viewBox min 修正的累计平移（顶层调用传 -ViewBoxMin），烘入叶子坐标。
	 *  ActiveClip = 祖先链上 clip-path 的累计裁剪框（已烘到与叶子坐标同一空间）；
	 *  本元素自身 clip-path 的坐标系 = AccumOffset 空间（不含自身 transform）， rect 叶子被钳制、
	 *  其余叶子越界时警告且不裁剪。 */
	static void ParseElement(const FXmlNode* XmlNode, const FSCADASvgStyle& ParentStyle,
		const FParseContext& Context,
		const FVector2D& AccumOffset, const FVector2D& ViewBoxMin, const TOptional<FBox2D>& ActiveClip,
		TArray<FSCADASvgNode>& OutNodes)
	{
		const FString& Tag = XmlNode->GetTag();

		// 未知命名空间元素（inkscape:/sodipodi: 等厂商扩展）按 SVG 规范静默忽略
		if (Tag.Contains(TEXT(":")))
		{
			return;
		}

		// 元素生效值取值器：匹配的样式表声明（CSS 级联已合并）覆盖同名 XML 属性
		const TMap<FString, FString> Declarations = ResolveElementDeclarations(XmlNode, Context.Rules);
		auto GetStyleValue = [&](const TCHAR* Name) -> FString
		{
			if (const FString* Found = Declarations.Find(FString(Name).ToLower()))
			{
				return *Found;
			}
			return XmlNode->GetAttribute(Name);
		};

		// display="none"（属性或样式表）整棵子树跳过
		if (GetStyleValue(TEXT("display")).Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			return;
		}

		// clip-path：本元素用户空间 = AccumOffset 空间（不含自身 transform，与 SVG 语义一致）；
		// 与祖先裁剪求交。交集为空 = 整棵子树不可见，直接跳过。
		TOptional<FBox2D> MyClip = ActiveClip;
		{
			const FString ClipRef = GetStyleValue(TEXT("clip-path"));
			if (ClipRef.StartsWith(TEXT("url(")))
			{
				FString Ref = ClipRef.Mid(4).TrimStartAndEnd();
				Ref.RemoveFromEnd(TEXT(")"));
				Ref.RemoveFromStart(TEXT("#"));
				if (const FBox2D* Clip = Context.ClipPaths.Find(Ref))
				{
					const FBox2D Moved(Clip->Min + AccumOffset, Clip->Max + AccumOffset);
					MyClip = MyClip.IsSet() ? MyClip->Overlap(Moved) : Moved;
					if (!MyClip->bIsValid || MyClip->GetSize().X <= 0.0 || MyClip->GetSize().Y <= 0.0)
					{
						return;
					}
				}
				else
				{
					UE_LOG(LogSCADASvg, Warning, TEXT("未找到 clip-path 引用 url(#%s)，按不裁剪处理"), *Ref);
				}
			}
		}

		ESCADASvgNodeKind Kind;
		bool bIsPath = false;
		if (Tag.Equals(TEXT("g"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Group; }
		else if (Tag.Equals(TEXT("rect"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Rect; }
		else if (Tag.Equals(TEXT("circle"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Circle; }
		else if (Tag.Equals(TEXT("ellipse"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Ellipse; }
		else if (Tag.Equals(TEXT("line"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Line; }
		else if (Tag.Equals(TEXT("polyline"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Polyline; }
		else if (Tag.Equals(TEXT("polygon"), ESearchCase::IgnoreCase)) { Kind = ESCADASvgNodeKind::Polygon; }
		else if (Tag.Equals(TEXT("path"), ESearchCase::IgnoreCase)) { bIsPath = true; Kind = ESCADASvgNodeKind::Polygon; }
		else
		{
			// title/desc/defs/metadata/style 等静默跳过（style 的内容已在解析前摘走，这里只兜底）；其余提示
			static const TSet<FString> SilentTags = { TEXT("title"), TEXT("desc"), TEXT("defs"), TEXT("metadata"), TEXT("xml"), TEXT("style"), TEXT("script") };
			if (!SilentTags.Contains(Tag.ToLower()))
			{
				UE_LOG(LogSCADASvg, Warning, TEXT("不支持的 SVG 元素 <%s>，已跳过（支持 rect/circle/ellipse/line/polyline/polygon/path/g）"), *Tag);
			}
			return;
		}

		const FString Id = XmlNode->GetAttribute(TEXT("id"));

		// —— 样式：级联到本元素（样式表声明优先于同名属性）后叠加到父样式 ——
		const FSCADASvgStyle Style = ComputeElementStyle(ParentStyle, GetStyleValue, Context.Gradients, ViewBoxMin);

		// —— path：d 解析为子路径点列，闭合或有填充 → 多边形，否则 → 折线（一条 path 可产多个图元）——
		if (bIsPath)
		{
			if (!Style.Fill.IsSet() && !Style.Stroke.IsSet())
			{
				UE_LOG(LogSCADASvg, Warning, TEXT("Path '%s' 无 fill 且无 stroke（不可见），已跳过"), *Id);
				return;
			}
			TArray<TArray<FVector2D>> Subpaths;
			TArray<bool> Closed;
			ParsePathData(XmlNode->GetAttribute(TEXT("d")), Subpaths, Closed);
			int32 Produced = 0;
			for (int32 i = 0; i < Subpaths.Num(); ++i)
			{
				if (Subpaths[i].Num() < 2)
				{
					continue;
				}
				FSCADASvgNode Node;
				Node.Id = Id;
				Node.Style = Style;
				Node.bSubPixelSampling = true;
				// 闭合子路径或有填充 → 多边形（多边形自带边框）；仅描边的开口子路径 → 折线
				const bool bFilled = Closed[i] || Style.Fill.IsSet();
				Node.Kind = bFilled ? ESCADASvgNodeKind::Polygon : ESCADASvgNodeKind::Polyline;
				if (!bFilled)
				{
					Node.Style.Fill.Reset();
				}
				Node.Points = MoveTemp(Subpaths[i]);
				for (FVector2D& P : Node.Points)
				{
					P += AccumOffset;
				}
				if (MyClip.IsSet())
				{
					FBox2D B(ForceInit);
					for (const FVector2D& P : Node.Points)
					{
						B += P;
					}
					const FBox2D& Clip = MyClip.GetValue();
					if (B.bIsValid && !(B.Min.X >= Clip.Min.X && B.Min.Y >= Clip.Min.Y && B.Max.X <= Clip.Max.X && B.Max.Y <= Clip.Max.Y))
					{
						UE_LOG(LogSCADASvg, Warning, TEXT("Path '%s' 超出 clip-path 裁剪框，path 暂不支持裁剪（按不裁剪导入）"), *Id);
					}
				}
				OutNodes.Add(MoveTemp(Node));
				++Produced;
			}
			if (Produced == 0)
			{
				UE_LOG(LogSCADASvg, Warning, TEXT("Path '%s' 的 d 属性无可解析几何，已跳过"), *Id);
			}
			return;
		}

		// —— 分组：凡是能烘进图元模型的变换一律烘平（平移 / 缩放 / 镜像 / 对角 matrix / 90° 整数倍
		//      旋转 / 只含点列类图元时的任意角度旋转）；只剩 skew、奇异矩阵，或者子树里有椭圆、
		//      带圆角矩形、任意角下的圆这类画不动的，才保留带 RenderTransform 的容器节点 ——
		if (Kind == ESCADASvgNodeKind::Group)
		{
			const FString Transform = XmlNode->GetAttribute(TEXT("transform"));

			// 按真实矩阵合成一次（scale / 镜像 / matrix 与各算子的书写顺序都在其中）再分级。
			// Visio 的整页旋转是 translate × rotate(90)，左右对称部件是 scale(-1,1) 镜像，
			// 斜放的管件是 rotate(30) 之类——过去这些要么被丢掉只留警告，要么因为"子树里出现了
			// rotate 字样"这个粗糙守卫被整组锁进嵌套画布，于是浏览器里正常、UE 里错位 + 画布堆叠。
			FSvgAffine Exact;
			const ESCADASvgXformKind XformKind = ClassifyGroupTransform(Transform, Exact);
			const bool bSelfBakeable = XformKind != ESCADASvgXformKind::Unrepresentable
				&& (XformKind != ESCADASvgXformKind::RotatedSafe || SubtreeIsRotationSafe(XmlNode, IsUniformScaleAffine(Exact)));
			if (XformKind == ESCADASvgXformKind::Translate)
			{
				// 没有 transform 或纯平移：偏移直接累加进子坐标，组节点不产出（样式继承链不断——传本组 Style）
				const FVector2D ChildOffset = AccumOffset + FVector2D(Exact.TX, Exact.TY);
				for (const FXmlNode* Child : XmlNode->GetChildrenNodes())
				{
					ParseElement(Child, Style, Context, ChildOffset, ViewBoxMin, MyClip, OutNodes);
				}
				return;
			}
			if (bSelfBakeable && !SubtreeHasUnbakeableGroup(XmlNode))
			{
				// 子节点先按父链累计平移产出，再整体过一遍仿射：两条换算路径里的
				// p' = Origin + M(p - Origin) 都与 SVG 的矩阵合成同解
				TArray<FSCADASvgNode> Baked;
				for (const FXmlNode* Child : XmlNode->GetChildrenNodes())
				{
					ParseElement(Child, Style, Context, AccumOffset, ViewBoxMin, MyClip, Baked);
				}
				if (XformKind == ESCADASvgXformKind::AxisAligned)
				{
					ApplyAxisAlignedAffineToNodes(Baked, Exact, AccumOffset);
				}
				else
				{
					ApplyRotationToNodes(Baked, Exact, AccumOffset);
				}
				OutNodes.Append(MoveTemp(Baked));
				return;
			}

			// 到这里说明变换里有烘不动的成分（skew、奇异矩阵），或图元里有任意角度下画不动的
			// （椭圆、带圆角的矩形、非等比缩放下的圆），或子树里有会退化成容器的分组：
			// 退回老办法——拆出 translate 累加，rotate 保留容器节点，其余警告忽略
			FVector2D T = FVector2D::ZeroVector;
			bool bHasRotation = false;
			float RotationAngle = 0.0f;
			FVector2D RotationCenter = FVector2D::ZeroVector;
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
					ParseElement(Child, Style, Context, ChildOffset, ViewBoxMin, MyClip, OutNodes);
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
			if (MyClip.IsSet())
			{
				// 旋转组子元素保持原始 SVG 坐标（容器本地空间），与已烘平的裁剪框不同空间，无法钳制
				UE_LOG(LogSCADASvg, Warning, TEXT("旋转组 '%s' 内的 clip-path 暂不支持，已忽略裁剪"), *Id);
			}
			for (const FXmlNode* Child : XmlNode->GetChildrenNodes())
			{
				ParseElement(Child, Style, Context, FVector2D::ZeroVector, ViewBoxMin, TOptional<FBox2D>(), Node.Children);
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

		// clip-path 裁剪：rect 叶子直接钳制到裁剪框（被钳边的圆角丢失，属可接受近似；
		// 完全裁掉的元素跳过——液位管里"溢出的液体"即由此消失）；其余叶子越界仅警告不裁剪
		if (MyClip.IsSet())
		{
			const FBox2D& Clip = MyClip.GetValue();
			if (Kind == ESCADASvgNodeKind::Rect)
			{
				const FVector2D Max = Node.P1 + Node.P2;
				const FVector2D CMin(FMath::Max(Node.P1.X, Clip.Min.X), FMath::Max(Node.P1.Y, Clip.Min.Y));
				const FVector2D CMax(FMath::Min(Max.X, Clip.Max.X), FMath::Min(Max.Y, Clip.Max.Y));
				if (CMax.X - CMin.X <= 0.0 || CMax.Y - CMin.Y <= 0.0)
				{
					return; // 被裁剪框完全裁掉
				}
				Node.P1 = CMin;
				Node.P2 = CMax - CMin;
			}
			else
			{
				FBox2D B(ForceInit);
				switch (Kind)
				{
				case ESCADASvgNodeKind::Circle:
					B += Node.P1 - FVector2D(Node.Radius, Node.Radius);
					B += Node.P1 + FVector2D(Node.Radius, Node.Radius);
					break;
				case ESCADASvgNodeKind::Ellipse:
					B += Node.P1 - Node.P2;
					B += Node.P1 + Node.P2;
					break;
				case ESCADASvgNodeKind::Line:
					B += Node.P1;
					B += Node.P2;
					break;
				case ESCADASvgNodeKind::Polyline:
				case ESCADASvgNodeKind::Polygon:
					for (const FVector2D& P : Node.Points)
					{
						B += P;
					}
					break;
				default:
					break;
				}
				if (B.bIsValid && !(B.Min.X >= Clip.Min.X && B.Min.Y >= Clip.Min.Y && B.Max.X <= Clip.Max.X && B.Max.Y <= Clip.Max.Y))
				{
					UE_LOG(LogSCADASvg, Warning, TEXT("元素 '%s' 超出 clip-path 裁剪框，非 rect 元素暂不支持裁剪（按不裁剪导入）"), *Id);
				}
			}
		}
		OutNodes.Add(MoveTemp(Node));
	}
}

bool FSCADASvgDocument::Parse(const FString& SvgText, FSCADASvgDocument& OutDocument)
{
	// FXmlFile 是行式 token 解析器，对 SVG 常用的写法有三处不接受（CDATA 段、<style> 里的 CSS、
	// 跨行属性值），任一都会让它报 "Malformed Xml File"。先做文本预处理，样式内容另走样式表解析。
	TArray<FString> CssTexts;
	const FString CleanText = SCADASvgImport::PrepareSvgTextForXmlParser(SvgText, CssTexts);

	FXmlFile XmlFile(CleanText, EConstructMethod::ConstructFromBuffer);
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

	SCADASvgImport::FParseContext Context;
	Context.Gradients = SCADASvgImport::CollectGradients(Root);
	Context.ClipPaths = SCADASvgImport::CollectClipPaths(Root);
	for (const FString& CssText : CssTexts)
	{
		SCADASvgImport::ParseCssRules(CssText, Context.Rules);
	}

	// 根样式起点 = SVG 规范缺省（fill 纯黑、stroke none），根元素自身的 class / 属性参与级联
	// （Visio 的 class="st9" { fill:none; …} 就是这样给整页定基调的）
	FSCADASvgStyle RootStyle;
	RootStyle.Fill = FLinearColor::Black;
	{
		const TMap<FString, FString> RootDeclarations = SCADASvgImport::ResolveElementDeclarations(Root, Context.Rules);
		auto GetRootStyleValue = [&](const TCHAR* Name) -> FString
		{
			if (const FString* Found = RootDeclarations.Find(FString(Name).ToLower()))
			{
				return *Found;
			}
			return Root->GetAttribute(Name);
		};
		RootStyle = SCADASvgImport::ComputeElementStyle(RootStyle, GetRootStyleValue, Context.Gradients, ViewBoxMin);
	}

	for (const FXmlNode* Child : Root->GetChildrenNodes())
	{
		SCADASvgImport::ParseElement(Child, RootStyle, Context, -ViewBoxMin, ViewBoxMin, TOptional<FBox2D>(), OutDocument.Roots);
	}

	// 组容器坐标空间 = 原始 viewBox（CropToContent 只改顶层 ViewBoxSize，不动本值）
	OutDocument.GroupContainerSize = OutDocument.ViewBoxSize;

	UE_LOG(LogSCADASvg, Log, TEXT("SVG 解析完成：viewBox %.0fx%.0f，顶层元素 %d 个"),
		OutDocument.ViewBoxSize.X, OutDocument.ViewBoxSize.Y, OutDocument.Roots.Num());
	return true;
}

namespace SCADASvgImport
{
	/** 递归累计节点在父空间中的包围盒（组：子包围盒先过组的放置变换——含旋转时就是工厂写进
	 *  RenderTransform 的那个变换——再取四角外接矩形）。描边元素外扩半线宽（笔画包络以中心线向两侧各伸 T/2）。 */
	static void AccumNodeBBox(const FSCADASvgNode& Node, FVector2D& OutMin, FVector2D& OutMax, bool& bOutAny)
	{
		auto AccumPoint = [&](const FVector2D& P, float Pad)
		{
			const FVector2D Lo = P - FVector2D(Pad, Pad);
			const FVector2D Hi = P + FVector2D(Pad, Pad);
			if (!bOutAny)
			{
				OutMin = Lo;
				OutMax = Hi;
				bOutAny = true;
			}
			else
			{
				OutMin = FVector2D::Min(OutMin, Lo);
				OutMax = FVector2D::Max(OutMax, Hi);
			}
		};

		const float Pad = (Node.Style.Stroke.IsSet() && Node.Style.StrokeWidth > 0.0f) ? Node.Style.StrokeWidth * 0.5f : 0.0f;
		switch (Node.Kind)
		{
		case ESCADASvgNodeKind::Group:
			{
				// 组的放置变换与工厂写进槽位置 + RenderTransform 的完全一致：
				// p → Translation + C + R·(p - C)，C = RotationCenter（= SVG 的 translate 后 rotate）。
				// 旋转必须算进范围：90°/180° 这类整角旋转（Visio 页面导出的常态）不取四角
				// 会把内容范围算错，裁出来的画布装不下内容。
				FSvgAffine Placed = MakeAffine(1.0, 0.0, Node.Translation.X, 0.0, 1.0, Node.Translation.Y);
				if (Node.bHasRotation)
				{
					const double Rad = FMath::DegreesToRadians((double)Node.RotationAngle);
					const double Cos = FMath::Cos(Rad), Sin = FMath::Sin(Rad);
					const double CX = Node.RotationCenter.X, CY = Node.RotationCenter.Y;
					Placed = MakeAffine(Cos, -Sin, Node.Translation.X + CX - (Cos * CX - Sin * CY),
						Sin, Cos, Node.Translation.Y + CY - (Sin * CX + Cos * CY));
				}

				for (const FSCADASvgNode& Child : Node.Children)
				{
					FVector2D ChildMin, ChildMax;
					bool bChildAny = false;
					AccumNodeBBox(Child, ChildMin, ChildMax, bChildAny);
					if (!bChildAny)
					{
						continue;
					}
					const FVector2D Corners[4] = {
						FVector2D(ChildMin.X, ChildMin.Y), FVector2D(ChildMax.X, ChildMin.Y),
						FVector2D(ChildMax.X, ChildMax.Y), FVector2D(ChildMin.X, ChildMax.Y)
					};
					FVector2D Lo = Placed.Apply(Corners[0]);
					FVector2D Hi = Lo;
					for (const FVector2D& Corner : Corners)
					{
						const FVector2D P = Placed.Apply(Corner);
						Lo = FVector2D::Min(Lo, P);
						Hi = FVector2D::Max(Hi, P);
					}
					AccumPoint(Lo, 0.0f);
					AccumPoint(Hi, 0.0f);
				}
				break;
			}
		case ESCADASvgNodeKind::Rect:
			AccumPoint(Node.P1, Pad);
			AccumPoint(Node.P1 + Node.P2, Pad);
			break;
		case ESCADASvgNodeKind::Circle:
			AccumPoint(Node.P1, Node.Radius + Pad);
			break;
		case ESCADASvgNodeKind::Ellipse:
			AccumPoint(Node.P1 - Node.P2, Pad);
			AccumPoint(Node.P1 + Node.P2, Pad);
			break;
		case ESCADASvgNodeKind::Line:
			AccumPoint(Node.P1, Pad);
			AccumPoint(Node.P2, Pad);
			break;
		case ESCADASvgNodeKind::Polyline:
		case ESCADASvgNodeKind::Polygon:
			for (const FVector2D& P : Node.Points)
			{
				AccumPoint(P, Pad);
			}
			break;
		}
	}
}

void FSCADASvgDocument::CropToContent(float Padding)
{
	FVector2D Min(0.0, 0.0), Max(0.0, 0.0);
	bool bAny = false;
	for (const FSCADASvgNode& Node : Roots)
	{
		SCADASvgImport::AccumNodeBBox(Node, Min, Max, bAny);
	}
	if (!bAny)
	{
		return;
	}
	Min -= FVector2D(Padding, Padding);
	Max += FVector2D(Padding, Padding);

	// 顶层平移修正：叶子坐标与组槽平移都在 viewBox 空间，统一 -Min；
	// 组内容器本地坐标（含嵌套组的 Translation 与 RotationCenter）不变
	for (FSCADASvgNode& Node : Roots)
	{
		if (Node.Kind == ESCADASvgNodeKind::Group)
		{
			Node.Translation -= Min;
			continue;
		}
		Node.P1 -= Min;
		if (Node.Kind == ESCADASvgNodeKind::Line)
		{
			Node.P2 -= Min;
		}
		for (FVector2D& P : Node.Points)
		{
			P -= Min;
		}
	}

	ViewBoxSize = FVector2D(FMath::Max(Max.X - Min.X, 1.0), FMath::Max(Max.Y - Min.Y, 1.0));
	UE_LOG(LogSCADASvg, Log, TEXT("按内容包围盒裁剪：min (%.1f, %.1f) → viewBox %.1fx%.1f"),
		Min.X, Min.Y, ViewBoxSize.X, ViewBoxSize.Y);
}
