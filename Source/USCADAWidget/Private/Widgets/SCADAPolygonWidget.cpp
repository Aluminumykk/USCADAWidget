// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADAPolygonWidget.h"

#include "Slate/SSCADAPolygon.h"
#include "Components/CanvasPanelSlot.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADAPolygonWidget::RebuildWidget()
{
	MyPolygon = SNew(SSCADAPolygon);

	// 挂接 Slate Tick 回调 + 注册核心 Ticker 兜底（编辑器/非游戏世界守卫在基类内部）
	SetupSlotSyncCallbacks(MyPolygon.Get());

	return MyPolygon.ToSharedRef();
}

void USCADAPolygonWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 以点位为基准：槽矩形始终跟随点位包围盒。
	// 反方向（设计器拖动/拉伸槽 → 回写点位）由 Tick 轮询 CheckSlotSync 闭环，
	// 因此这里无条件把槽对齐到点位，保证编译/重建后点位编辑不丢失。
	SyncSlotFromGeometry();

	if (MyPolygon.IsValid())
	{
		PushGeometryToSlate();
		MyPolygon->SetColor(BorderColor);
		MyPolygon->SetThickness(Thickness);
		MyPolygon->SetLineStyle(LineStyle);
		MyPolygon->SetFillColor(FillColor);
		MyPolygon->SetFillPattern(FillPattern);
		MyPolygon->SetFlashEnabled(bFlashEnabled);
		MyPolygon->SetFlashColor(FlashColor);
		MyPolygon->SetFlashInverted(bFlashInverted);
	}

	// 属性同步后补一次绑定检查：绑定先于本次同步应用（初始化时序）时在此完成注册；
	// 无任何委托绑定（设计器实例/未绑定运行实例）时是零成本 no-op。
	EnsurePropertyBindingTicker();
}

void USCADAPolygonWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyPolygon.Reset();
}

void USCADAPolygonWidget::PushGeometryToSlate()
{
	if (MyPolygon.IsValid())
	{
		// Slate 拿到的是槽内局部坐标：画布设计坐标 - 槽位置。
		FVector2D SlotPos = FVector2D::ZeroVector;
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			SlotPos = CanvasSlot->GetPosition();
		}

		// 复用成员缓冲，避免设计器拖动中每帧一次的新数组分配；
		// Slate 层 SetPoints(TAttribute) 按值拷贝存储，缓冲复用无悬垂风险
		PointsScratch.Reset();
		PointsScratch.Reserve(Points.Num());
		for (const FVector2D& P : Points)
		{
			PointsScratch.Add(P - SlotPos);
		}
		MyPolygon->SetPoints(PointsScratch);
	}
}

void USCADAPolygonWidget::SnapPropertiesToPixel()
{
	for (FVector2D& P : Points)
	{
		P.X = FMath::RoundToFloat(P.X);
		P.Y = FMath::RoundToFloat(P.Y);
	}
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 0.0f);
	// 点位版本号递增：本函数覆盖 SetPoints/AddPoint/RemovePointAtIndex/PostEditChangeProperty
	// （含撤销/重做经 PostEditUndo → PostEditChange → PostEditChangeProperty 的通知路径）的全部写入
	++PointsVersion;
}

void USCADAPolygonWidget::GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (Points.Num() == 0)
	{
		// 无点位时给 1x1 默认包围盒，保证槽/同步逻辑不出现除零或退化
		OutMin = FVector2D::ZeroVector;
		OutMax = FVector2D(1.0, 1.0);
		return;
	}

	OutMin = Points[0];
	OutMax = Points[0];
	for (int32 i = 1; i < Points.Num(); ++i)
	{
		OutMin.X = FMath::Min(OutMin.X, Points[i].X);
		OutMin.Y = FMath::Min(OutMin.Y, Points[i].Y);
		OutMax.X = FMath::Max(OutMax.X, Points[i].X);
		OutMax.Y = FMath::Max(OutMax.Y, Points[i].Y);
	}
}

void USCADAPolygonWidget::TranslateGeometryBy(const FVector2D& Delta)
{
	for (FVector2D& P : Points)
	{
		P += Delta;
	}
	++PointsVersion;
}

bool USCADAPolygonWidget::ShouldVerifyGeometryBounds() const
{
	return PointsVersion != BoundsVerifiedPointsVersion;
}

void USCADAPolygonWidget::OnGeometryBoundsVerified()
{
	BoundsVerifiedPointsVersion = PointsVersion;
}

void USCADAPolygonWidget::OnSlotAlignedToGeometry()
{
	BoundsVerifiedPointsVersion = PointsVersion;
}

void USCADAPolygonWidget::MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart)
{
	// 规范化：min 位置 + 正尺寸；记录原始符号
	FVector2D CPos = RawPos;
	FVector2D CSize = RawSize;
	const bool bNegX = CSize.X < 0.0;
	const bool bNegY = CSize.Y < 0.0;
	if (bNegX) { CPos.X += CSize.X; CSize.X = -CSize.X; }
	if (bNegY) { CPos.Y += CSize.Y; CSize.Y = -CSize.Y; }

	// 翻转沿检测：符号相对上一帧原始值变化 → 该轴在矩形内镜像一次
	const bool bToggleX = (bNegX != bPrevRawNegX);
	const bool bToggleY = (bNegY != bPrevRawNegY);
	bPrevRawNegX = bNegX;
	bPrevRawNegY = bNegY;

	// 规范空间增量映射（正尺寸，无符号问题），除零保护钳到 1
	FVector2D OldSize = CachedSlotSize;
	if (FMath::IsNearlyZero((float)OldSize.X)) { OldSize.X = 1.0; }
	if (FMath::IsNearlyZero((float)OldSize.Y)) { OldSize.Y = 1.0; }
	const FVector2D Scale(CSize.X / OldSize.X, CSize.Y / OldSize.Y);
	for (FVector2D& P : Points)
	{
		P = CPos + (P - CachedSlotPos) * Scale;
	}

	// 翻转沿：在新规范矩形内镜像
	if (bToggleX)
	{
		for (FVector2D& P : Points)
		{
			P.X = CPos.X + (CSize.X - (P.X - CPos.X));
		}
	}
	if (bToggleY)
	{
		for (FVector2D& P : Points)
		{
			P.Y = CPos.Y + (CSize.Y - (P.Y - CPos.Y));
		}
	}

	// 槽映射改写了点位：递增版本号（稳态失配自愈分支的短路判据）
	++PointsVersion;

	// 槽写回正向规范矩形（设计器下一帧会覆盖；始终以缓存判定新输入）
	{
		TGuardValue<bool> Guard(bSyncingGeometry, true);
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetPosition(CPos);
		CanvasSlot->SetSize(CSize);
	}
	CachedSlotPos = CPos;
	CachedSlotSize = CSize;
	PushGeometryToSlate();
}

void USCADAPolygonWidget::SetPoints(const TArray<FVector2D>& InPoints)
{
	Points = InPoints;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADAPolygonWidget::SetBorderColor(FLinearColor InColor)
{
	BorderColorDelegate.Unbind();
	BorderColor = InColor;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetColor(BorderColor);
	}
}

void USCADAPolygonWidget::SetThickness(float InThickness)
{
	ThicknessDelegate.Unbind();
	Thickness = InThickness;
	SnapPropertiesToPixel();
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetThickness(Thickness);
	}
}

void USCADAPolygonWidget::SetLineStyle(ESCADALineStyle InStyle)
{
	LineStyle = InStyle;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetLineStyle(LineStyle);
	}
}

void USCADAPolygonWidget::SetFillColor(FLinearColor InColor)
{
	FillColorDelegate.Unbind();
	FillColor = InColor;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetFillColor(FillColor);
	}
}

void USCADAPolygonWidget::SetFillPattern(ESCADAFillPattern InPattern)
{
	FillPattern = InPattern;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetFillPattern(FillPattern);
	}
}

void USCADAPolygonWidget::SetFlashEnabled(bool bInEnabled)
{
	bFlashEnabledDelegate.Unbind();
	bFlashEnabled = bInEnabled;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADAPolygonWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColorDelegate.Unbind();
	FlashColor = InColor;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetFlashColor(FlashColor);
	}
}

void USCADAPolygonWidget::SetFlashInverted(bool bInInverted)
{
	bFlashInvertedDelegate.Unbind();
	bFlashInverted = bInInverted;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetFlashInverted(bFlashInverted);
	}
}

void USCADAPolygonWidget::AddPoint(FVector2D InPoint)
{
	Points.Add(InPoint);
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADAPolygonWidget::RemovePointAtIndex(int32 Index)
{
	// 索引非法（负数或越界）时安全忽略
	if (Index < 0 || Index >= Points.Num())
	{
		return;
	}
	Points.RemoveAt(Index);
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

// ———— UMG 属性绑定轮询（“拉”→“推”桥）————
// 引擎语义：手动 Set* 解除绑定（各 Set* 开头的 *Delegate.Unbind()，照 UTextBlock::SetText 先例）；
// 委托取值走每帧轮询而非引擎的一次性 Execute：绑定委托是“拉”模型、本控件数据是“推”模型，
// 中间桥 = 下面的核心 Ticker（注册时机见 EnsurePropertyBindingTicker；设计器实例委托
// 天然未绑定不会注册，运行时 PIE/打包正常工作，无 GIsEditor 守卫）。
// 多边形的 Points 为数组类型不提供绑定，可绑属性均为非几何类：变化只推 Slate，不动槽。
// 边框色在 Slate 层的接口名是 SetColor（数据层属性名是 BorderColor，对照多边形族的既有映射）。

bool USCADAPolygonWidget::HasBoundPropertyDelegates() const
{
	return BorderColorDelegate.IsBound() || ThicknessDelegate.IsBound()
		|| FillColorDelegate.IsBound()
		|| bFlashEnabledDelegate.IsBound() || FlashColorDelegate.IsBound()
		|| bFlashInvertedDelegate.IsBound();
}

void USCADAPolygonWidget::EnsurePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid() || !HasBoundPropertyDelegates())
	{
		return;
	}
	TWeakObjectPtr<USCADAPolygonWidget> WeakThis(this);
	PropertyBindingTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->PollPropertyBindings();
			return true;
		}
		return false;
	}));
}

void USCADAPolygonWidget::RemovePropertyBindingTicker()
{
	if (PropertyBindingTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PropertyBindingTickerHandle);
		PropertyBindingTickerHandle.Reset();
	}
}

void USCADAPolygonWidget::PollPropertyBindings()
{
	// 运行中委托全部解除（蓝图 Set* / 外部手动 Unbind 等）→ 自注销，
	// 等下一次绑定经 OnBindingChanged/SynchronizeProperties 路径重新注册。
	if (!HasBoundPropertyDelegates())
	{
		RemovePropertyBindingTicker();
		return;
	}

	// 非几何属性：变化才写成员并推对应 Slate Set（与 SynchronizeProperties 的单属性子集一致，
	// 但绝不调公开 Set*——那会 Unbind）。float 先按数据层规则取整/钳制再比较
	// （本族边框宽度允许 0：下限 0，与各控件 SnapPropertiesToPixel 规则一致）。
	if (ThicknessDelegate.IsBound())
	{
		const float NewValue = FMath::Max(FMath::RoundToFloat(ThicknessDelegate.Execute()), 0.0f);
		if (!FMath::IsNearlyEqual(NewValue, Thickness))
		{
			Thickness = NewValue;
			if (MyPolygon.IsValid())
			{
				MyPolygon->SetThickness(Thickness);
			}
		}
	}
	if (BorderColorDelegate.IsBound())
	{
		const FLinearColor NewValue = BorderColorDelegate.Execute();
		if (NewValue != BorderColor)
		{
			BorderColor = NewValue;
			if (MyPolygon.IsValid())
			{
				MyPolygon->SetColor(BorderColor);
			}
		}
	}
	if (FillColorDelegate.IsBound())
	{
		const FLinearColor NewValue = FillColorDelegate.Execute();
		if (NewValue != FillColor)
		{
			FillColor = NewValue;
			if (MyPolygon.IsValid())
			{
				MyPolygon->SetFillColor(FillColor);
			}
		}
	}
	if (bFlashEnabledDelegate.IsBound())
	{
		const bool bNewValue = bFlashEnabledDelegate.Execute();
		if (bNewValue != bFlashEnabled)
		{
			bFlashEnabled = bNewValue;
			if (MyPolygon.IsValid())
			{
				MyPolygon->SetFlashEnabled(bFlashEnabled);
			}
		}
	}
	if (FlashColorDelegate.IsBound())
	{
		const FLinearColor NewValue = FlashColorDelegate.Execute();
		if (NewValue != FlashColor)
		{
			FlashColor = NewValue;
			if (MyPolygon.IsValid())
			{
				MyPolygon->SetFlashColor(FlashColor);
			}
		}
	}
	if (bFlashInvertedDelegate.IsBound())
	{
		const bool bNewValue = bFlashInvertedDelegate.Execute();
		if (bNewValue != bFlashInverted)
		{
			bFlashInverted = bNewValue;
			if (MyPolygon.IsValid())
			{
				MyPolygon->SetFlashInverted(bFlashInverted);
			}
		}
	}
}

void USCADAPolygonWidget::OnBindingChanged(const FName& Property)
{
	Super::OnBindingChanged(Property);

	// 绑定生效通道：UWidget::AddBinding 成功（原生 binder/属性路径）后引擎回调此处。
	// “Create Binding”（绑定到 BP 生成函数）路径不回调此处，由其后的 SynchronizeProperties
	// 或已注册 Ticker 覆盖；此处按 IsBound 判定，未绑定即零成本 no-op。
	EnsurePropertyBindingTicker();
}

void USCADAPolygonWidget::BeginDestroy()
{
	// 绑定轮询 Ticker 兜底注销（正常路径：weak 失效时 Ticker 自清理）
	RemovePropertyBindingTicker();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void USCADAPolygonWidget::PreEditChange(FProperty* PropertyAboutToChange)
{
	// 数组增点检测的基准快照必须在变更前取样（细节面板编辑的是模板实例，
	// 它可能从不跑 SynchronizeProperties，不能指望别处初始化）。
	// 任何属性变更都刷新：代价是小数组拷贝，换来 Points 结构变化前必有有效基准。
	PointsSnapshot = Points;
	bPointsSnapshotValid = true;

	Super::PreEditChange(PropertyAboutToChange);
}

void USCADAPolygonWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 注意要查 MemberProperty：编辑数组元素时 Property 是内层的 FVector2D::X/Y，
	// 只有 MemberProperty（所属成员）才是 Points 本身。
	const FName MemberPropName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	const bool bPointsChanged = MemberPropName == GET_MEMBER_NAME_CHECKED(USCADAPolygonWidget, Points);

	// 数组增点（+ 追加 / 元素右键 Insert）：新元素默认落在 (0,0)（离画布太远），
	// 按快照 diff 找出插入位置，把新点移到两邻点中点（落在被插入的边上）。
	// Duplicate（复制元素）保留原位不改。与折线同一套检测逻辑。
	if (bPointsChanged && bPointsSnapshotValid &&
		Points.Num() == PointsSnapshot.Num() + 1 &&
		(PropertyChangedEvent.ChangeType & EPropertyChangeType::Duplicate) == 0)
	{
		// 找插入位置 k：去掉新数组第 k 个元素后与快照逐项相等（取首个可行 k，
		// 新元素与邻点重合的歧义场景任选其一，结果位置几乎相同）
		int32 InsertIndex = INDEX_NONE;
		for (int32 k = 0; k <= PointsSnapshot.Num(); ++k)
		{
			bool bMatches = true;
			for (int32 i = 0; i < PointsSnapshot.Num(); ++i)
			{
				const int32 j = (i < k) ? i : i + 1;
				if (!Points[j].Equals(PointsSnapshot[i]))
				{
					bMatches = false;
					break;
				}
			}
			if (bMatches)
			{
				InsertIndex = k;
				break;
			}
		}
		if (InsertIndex != INDEX_NONE)
		{
			RepositionNewlyInsertedPoint(InsertIndex);
		}
	}
	if (bPointsChanged)
	{
		PointsSnapshot = Points;
	}

	// 编辑器属性面板输入完成后立即取整，面板显示值即为最终绘制值
	// （增点重定位在取整之前，新点一并被取整）
	SnapPropertiesToPixel();

	// 点位坐标被编辑 → 槽矩形（X/Y/宽/高）同步更新（须在 Super 之前，
	// Super 链上的 SynchronizeProperties 以槽为准回写，槽先更新才不会把新值冲掉）。
	if (bPointsChanged)
	{
		SyncSlotFromGeometry();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void USCADAPolygonWidget::RepositionNewlyInsertedPoint(int32 InsertIndex)
{
	const int32 N = Points.Num();
	// 闭合多边形：新点取两邻点中点（环形索引，插入首部时前邻 = 末点），
	// 落在被插入的那条边上，不改变现有轮廓形状
	if (N >= 3 && Points.IsValidIndex(InsertIndex))
	{
		const FVector2D& Prev = Points[(InsertIndex - 1 + N) % N];
		const FVector2D& Next = Points[(InsertIndex + 1) % N];
		Points[InsertIndex] = (Prev + Next) * 0.5;
	}
}

const FText USCADAPolygonWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
