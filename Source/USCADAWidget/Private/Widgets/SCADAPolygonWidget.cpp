// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADAPolygonWidget.h"

#include "Slate/SSCADAPolygon.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADAPolygonWidget::RebuildWidget()
{
	MyPolygon = SNew(SSCADAPolygon);

	// UWidget 没有原生 Tick，借 Slate 层的 Tick 轮询槽矩形变化
	TWeakObjectPtr<USCADAPolygonWidget> WeakThis(this);
	MyPolygon->ExternalTickCallback = [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CheckSlotSync();
		}
	};

	// archetype 实例（Slate 不一定被 Tick）也要能跑 CheckSlotSync，
	// 否则面板拖放的落点（静默写在 archetype 槽上）永远认领不到
	EnsureCoreTicker();

	return MyPolygon.ToSharedRef();
}

void USCADAPolygonWidget::EnsureCoreTicker()
{
	if (CoreTickerHandle.IsValid())
	{
		return;
	}
	TWeakObjectPtr<USCADAPolygonWidget> WeakThis(this);
	CoreTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CheckSlotSync();
			return true;
		}
		return false;
	}));
}

void USCADAPolygonWidget::BeginDestroy()
{
	if (CoreTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CoreTickerHandle);
		CoreTickerHandle.Reset();
	}
	Super::BeginDestroy();
}

void USCADAPolygonWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 以点位为基准：槽矩形始终跟随点位包围盒。
	// 反方向（设计器拖动/拉伸槽 → 回写点位）由 Tick 轮询 CheckSlotSync 闭环，
	// 因此这里无条件把槽对齐到点位，保证编译/重建后点位编辑不丢失。
	SyncSlotFromPoints();

	if (MyPolygon.IsValid())
	{
		PushPointsToSlate();
		MyPolygon->SetColor(BorderColor);
		MyPolygon->SetThickness(Thickness);
		MyPolygon->SetLineStyle(LineStyle);
		MyPolygon->SetFillColor(FillColor);
		MyPolygon->SetFillPattern(FillPattern);
		MyPolygon->SetFlashEnabled(bFlashEnabled);
		MyPolygon->SetFlashColor(FlashColor);
	}
}

void USCADAPolygonWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyPolygon.Reset();
}

void USCADAPolygonWidget::PushPointsToSlate()
{
	if (MyPolygon.IsValid())
	{
		// Slate 拿到的是槽内局部坐标：画布设计坐标 - 槽位置。
		FVector2D SlotPos = FVector2D::ZeroVector;
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			SlotPos = CanvasSlot->GetPosition();
		}

		TArray<FVector2D> LocalPoints;
		LocalPoints.Reserve(Points.Num());
		for (const FVector2D& P : Points)
		{
			LocalPoints.Add(P - SlotPos);
		}
		MyPolygon->SetPoints(LocalPoints);
	}
}

void USCADAPolygonWidget::SnapPropertiesToPixel()
{
	for (FVector2D& P : Points)
	{
		P.X = FMath::RoundToFloat(P.X);
		P.Y = FMath::RoundToFloat(P.Y);
	}
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 1.0f);
}

void USCADAPolygonWidget::GetPointsBounds(FVector2D& OutMin, FVector2D& OutMax) const
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

FVector2D USCADAPolygonWidget::GetGeometryMin() const
{
	FVector2D Min, Max;
	GetPointsBounds(Min, Max);
	return Min;
}

void USCADAPolygonWidget::TranslateGeometryBy(const FVector2D& Delta)
{
	for (FVector2D& P : Points)
	{
		P += Delta;
	}
}

void USCADAPolygonWidget::SyncSlotFromPoints()
{
	if (bSyncingGeometry)
	{
		return;
	}
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!CanvasSlot)
	{
		PushPointsToSlate();
		return;
	}

	TGuardValue<bool> Guard(bSyncingGeometry, true);

	FVector2D Min, Max;
	GetPointsBounds(Min, Max);
	const FVector2D Size(FMath::Max(Max.X - Min.X, 1.0), FMath::Max(Max.Y - Min.Y, 1.0));

	CanvasSlot->SetAutoSize(false);
	CanvasSlot->SetPosition(Min);
	CanvasSlot->SetSize(Size);
	CachedSlotPos = Min;
	CachedSlotSize = Size;
	bSlotCacheValid = true;
	// 点位为真值：任何正向同步都复位交互簿记（符号基线/收尾标记）
	bPrevRawNegX = false;
	bPrevRawNegY = false;
	bPendingFinalize = false;

	PushPointsToSlate();
}

void USCADAPolygonWidget::ForceSyncFromSlotRect()
{
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		EnsureCoreTicker();
		CheckSlotSync();
	}
}

void USCADAPolygonWidget::CheckSlotSync()
{
	if (bSyncingGeometry)
	{
		return;
	}

	// 设计器拖动/拉伸槽的处理（显示与存储分离：点位是唯一真值，槽只是操作手柄）。
	// 槽永远保持正向规范矩形（min 位置 + 正尺寸），布局不受负尺寸影响，绘制不跳；
	// 点位映射在规范空间增量进行；翻转（WinCC 式拖过对边镜像）= 原始尺寸符号变化沿，
	// 过一次边在新矩形内镜像一次，可来回拖。
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!CanvasSlot)
	{
		return;
	}

	const FVector2D RawPos = CanvasSlot->GetPosition();
	const FVector2D RawSize = CanvasSlot->GetSize();

	// 缓存未初始化（新实例首次 Tick）：只采纳当前槽矩形为基准，不做映射，
	// 否则默认 1x1 缓存会把健康槽矩形误判成外部缩放输入，把点位放大到爆炸。
	if (!bSlotCacheValid)
	{
		bSlotCacheValid = true;
		// 初始放置认领：拖放时设计器可能在首次 Tick 之前就把槽写到落点（SynchronizeProperties
		// 已跑过），若只采纳基准会把落点收下、数据留在默认位置（松手跳回原点）。
		// 认领标记仍在时先平移数据到槽位置并正向对齐，再以对齐后的槽矩形为基准。
		// 守卫限定设计器：UMG 设计器实例的 GetWorld() 返回编辑器世界（非空！），
		// 用 !IsGameWorld() 放行编辑器/预览世界，只屏蔽 PIE/打包游戏。
		if (bPendingInitialPlacement && GIsEditor && (!GetWorld() || !GetWorld()->IsGameWorld()))
		{
			bPendingInitialPlacement = false;
			const FVector2D RawMin = RawPos + FVector2D(FMath::Min((float)RawSize.X, 0.0f), FMath::Min((float)RawSize.Y, 0.0f));
			TranslateGeometryBy(RawMin - GetGeometryMin());
			SyncSlotFromPoints();
			CachedSlotPos = CanvasSlot->GetPosition();
			CachedSlotSize = CanvasSlot->GetSize();
		}
		else
		{
			CachedSlotPos = RawPos;
			CachedSlotSize = RawSize;
		}
		bPrevRawNegX = CachedSlotSize.X < 0.0;
		bPrevRawNegY = CachedSlotSize.Y < 0.0;
		return;
	}

	if (RawPos.Equals(CachedSlotPos) && RawSize.Equals(CachedSlotSize))
	{
		// 设计器无新输入。鼠标左键仍按下 = 拖动中停顿，不收尾（保留符号沿状态）；
		// 已松开 = 交互结束，收尾一次：取整、标脏、符号基线复位。
		if (bPendingFinalize && !FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
		{
			bPendingFinalize = false;
			bInteractionModified = false;
			bPrevRawNegX = false;
			bPrevRawNegY = false;
			SnapPropertiesToPixel();
			SyncSlotFromPoints();
#if WITH_EDITOR
			if (GIsEditor && !HasAnyFlags(RF_Transient))
			{
				MarkPackageDirty();
			}
#endif
		}
		return;
	}

	// 初始放置认领（设计器在首次同步之后才把槽写到落点，未经属性通知）：
	// 平移数据到槽位置并正向对齐，不做映射。守卫限定设计器（编辑器/预览世界放行，
	// PIE/打包游戏屏蔽），防止运行时外部代码改槽位置时误触发平移。
	if (bPendingInitialPlacement && GIsEditor && (!GetWorld() || !GetWorld()->IsGameWorld()))
	{
		bPendingInitialPlacement = false;
		const FVector2D RawMin = RawPos + FVector2D(FMath::Min((float)RawSize.X, 0.0f), FMath::Min((float)RawSize.Y, 0.0f));
		TranslateGeometryBy(RawMin - GetGeometryMin());
		SyncSlotFromPoints();
		return;
	}

	// 设计器写入了新矩形（不变式：读到与缓存不同的值即外部输入）
#if WITH_EDITOR
	if (!bInteractionModified)
	{
		// 每次交互只进一次撤销事务
		bInteractionModified = true;
		if (GIsEditor && !HasAnyFlags(RF_Transient))
		{
			Modify();
		}
	}
#endif
	bPendingFinalize = true;

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

	// 槽写回正向规范矩形（设计器下一帧会覆盖；始终以缓存判定新输入）
	{
		TGuardValue<bool> Guard(bSyncingGeometry, true);
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetPosition(CPos);
		CanvasSlot->SetSize(CSize);
	}
	CachedSlotPos = CPos;
	CachedSlotSize = CSize;
	PushPointsToSlate();
}

void USCADAPolygonWidget::SetPoints(const TArray<FVector2D>& InPoints)
{
	Points = InPoints;
	SnapPropertiesToPixel();
	SyncSlotFromPoints();
}

void USCADAPolygonWidget::SetBorderColor(FLinearColor InColor)
{
	BorderColor = InColor;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetColor(BorderColor);
	}
}

void USCADAPolygonWidget::SetThickness(float InThickness)
{
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
	bFlashEnabled = bInEnabled;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADAPolygonWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColor = InColor;
	if (MyPolygon.IsValid())
	{
		MyPolygon->SetFlashColor(FlashColor);
	}
}

void USCADAPolygonWidget::AddPoint(FVector2D InPoint)
{
	Points.Add(InPoint);
	SnapPropertiesToPixel();
	SyncSlotFromPoints();
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
	SyncSlotFromPoints();
}

#if WITH_EDITOR
void USCADAPolygonWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 编辑器属性面板输入完成后立即取整，面板显示值即为最终绘制值
	SnapPropertiesToPixel();

	// 点位坐标被编辑 → 槽矩形（X/Y/宽/高）同步更新（须在 Super 之前，
	// Super 链上的 SynchronizeProperties 以槽为准回写，槽先更新才不会把新值冲掉）。
	// 注意要查 MemberProperty：编辑数组元素时 Property 是内层的 FVector2D::X/Y，
	// 只有 MemberProperty（所属成员）才是 Points 本身。
	const FName MemberPropName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropName == GET_MEMBER_NAME_CHECKED(USCADAPolygonWidget, Points))
	{
		SyncSlotFromPoints();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

const FText USCADAPolygonWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
