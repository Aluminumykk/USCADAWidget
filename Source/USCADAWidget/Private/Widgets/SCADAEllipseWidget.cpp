// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADAEllipseWidget.h"

#include "Slate/SSCADAEllipse.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADAEllipseWidget::RebuildWidget()
{
	MyEllipse = SNew(SSCADAEllipse);

	// UWidget 没有原生 Tick，借 Slate 层的 Tick 轮询槽矩形变化
	TWeakObjectPtr<USCADAEllipseWidget> WeakThis(this);
	MyEllipse->ExternalTickCallback = [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CheckSlotSync();
		}
	};

	// archetype 实例（Slate 不一定被 Tick）也要能跑 CheckSlotSync，
	// 否则面板拖放的落点（静默写在 archetype 槽上）永远认领不到
	EnsureCoreTicker();

	return MyEllipse.ToSharedRef();
}

void USCADAEllipseWidget::EnsureCoreTicker()
{
	if (CoreTickerHandle.IsValid())
	{
		return;
	}
	TWeakObjectPtr<USCADAEllipseWidget> WeakThis(this);
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

void USCADAEllipseWidget::BeginDestroy()
{
	if (CoreTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CoreTickerHandle);
		CoreTickerHandle.Reset();
	}
	Super::BeginDestroy();
}

void USCADAEllipseWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 以中心/半径为基准：槽矩形始终跟随椭圆包围盒。
	// 反方向（设计器拖动/拉伸槽 → 回写中心/半径）由 Tick 轮询 CheckSlotSync 闭环，
	// 因此这里无条件把槽对齐到几何数据，保证编译/重建后属性编辑不丢失。
	SyncSlotFromGeometry();

	if (MyEllipse.IsValid())
	{
		PushGeometryToSlate();
		MyEllipse->SetColor(BorderColor);
		MyEllipse->SetThickness(Thickness);
		MyEllipse->SetLineStyle(LineStyle);
		MyEllipse->SetFillColor(FillColor);
		MyEllipse->SetFillPattern(FillPattern);
		MyEllipse->SetFlashEnabled(bFlashEnabled);
		MyEllipse->SetFlashColor(FlashColor);
	}
}

void USCADAEllipseWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyEllipse.Reset();
}

void USCADAEllipseWidget::PushGeometryToSlate()
{
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetRadiusX(RadiusX);
		MyEllipse->SetRadiusY(RadiusY);
	}
}

void USCADAEllipseWidget::SnapPropertiesToPixel()
{
	Center.X = FMath::RoundToFloat(Center.X);
	Center.Y = FMath::RoundToFloat(Center.Y);
	RadiusX = FMath::Max(FMath::RoundToFloat(RadiusX), 1.0f);
	RadiusY = FMath::Max(FMath::RoundToFloat(RadiusY), 1.0f);
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 1.0f);
}

FVector2D USCADAEllipseWidget::GetGeometryMin() const
{
	return Center - FVector2D(RadiusX, RadiusY);
}

void USCADAEllipseWidget::TranslateGeometryBy(const FVector2D& Delta)
{
	Center += Delta;
}

void USCADAEllipseWidget::SyncSlotFromGeometry()
{
	if (bSyncingGeometry)
	{
		return;
	}
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!CanvasSlot)
	{
		PushGeometryToSlate();
		return;
	}

	TGuardValue<bool> Guard(bSyncingGeometry, true);

	// 槽矩形 = 椭圆包围盒 (Center - R, 2R)
	const FVector2D Min(Center.X - RadiusX, Center.Y - RadiusY);
	const FVector2D Size(FMath::Max(RadiusX * 2.0f, 1.0), FMath::Max(RadiusY * 2.0f, 1.0));

	CanvasSlot->SetAutoSize(false);
	CanvasSlot->SetPosition(Min);
	CanvasSlot->SetSize(Size);
	CachedSlotPos = Min;
	CachedSlotSize = Size;
	bSlotCacheValid = true;
	// 几何数据为真值：任何正向同步都复位交互簿记（符号基线/收尾标记）
	bPrevRawNegX = false;
	bPrevRawNegY = false;
	bPendingFinalize = false;

	PushGeometryToSlate();
}

void USCADAEllipseWidget::ForceSyncFromSlotRect()
{
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		EnsureCoreTicker();
		CheckSlotSync();
	}
}

void USCADAEllipseWidget::CheckSlotSync()
{
	if (bSyncingGeometry)
	{
		return;
	}

	// 设计器拖动/拉伸槽的处理（显示与存储分离：中心/半径是唯一真值，槽只是操作手柄）。
	// 槽永远保持正向规范矩形（min 位置 + 正尺寸），布局不受负尺寸影响，绘制不跳；
	// 中心/半径映射在规范空间增量进行；椭圆在自身包围盒内镜像 = 自身，翻转沿为 no-op。
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!CanvasSlot)
	{
		return;
	}

	const FVector2D RawPos = CanvasSlot->GetPosition();
	const FVector2D RawSize = CanvasSlot->GetSize();

	// 缓存未初始化（新实例首次 Tick）：只采纳当前槽矩形为基准，不做映射，
	// 否则默认 1x1 缓存会把健康槽矩形误判成外部缩放输入，把半径放大到爆炸。
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
			SyncSlotFromGeometry();
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
			SyncSlotFromGeometry();
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
		SyncSlotFromGeometry();
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

	// 规范化：min 位置 + 正尺寸；记录原始符号（椭圆镜像 = 自身，符号沿只做基线跟踪）
	FVector2D CPos = RawPos;
	FVector2D CSize = RawSize;
	const bool bNegX = CSize.X < 0.0;
	const bool bNegY = CSize.Y < 0.0;
	if (bNegX) { CPos.X += CSize.X; CSize.X = -CSize.X; }
	if (bNegY) { CPos.Y += CSize.Y; CSize.Y = -CSize.Y; }
	bPrevRawNegX = bNegX;
	bPrevRawNegY = bNegY;

	// 规范空间增量映射（正尺寸，无符号问题），除零保护钳到 1。
	// 注意：交互过程中半径不能钳下限（≥1）——拖过对边翻转时槽尺寸会穿过 ~0，
	// 钳位会把半径钉死在 1，之后继续拖动时增量缩放从钳位值起算，比例永久失真、无法恢复。
	// Scale 恒为正，半径天然保持为正；下限钳位只在做收尾的 SnapPropertiesToPixel 里进行。
	FVector2D OldSize = CachedSlotSize;
	if (FMath::IsNearlyZero((float)OldSize.X)) { OldSize.X = 1.0; }
	if (FMath::IsNearlyZero((float)OldSize.Y)) { OldSize.Y = 1.0; }
	const FVector2D Scale(CSize.X / OldSize.X, CSize.Y / OldSize.Y);
	Center = CPos + (Center - CachedSlotPos) * Scale;
	RadiusX = RadiusX * (float)Scale.X;
	RadiusY = RadiusY * (float)Scale.Y;

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

void USCADAEllipseWidget::SetCenter(FVector2D InCenter)
{
	Center = InCenter;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADAEllipseWidget::SetRadiusX(float InRadiusX)
{
	RadiusX = InRadiusX;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADAEllipseWidget::SetRadiusY(float InRadiusY)
{
	RadiusY = InRadiusY;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADAEllipseWidget::SetBorderColor(FLinearColor InColor)
{
	BorderColor = InColor;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetColor(BorderColor);
	}
}

void USCADAEllipseWidget::SetThickness(float InThickness)
{
	Thickness = InThickness;
	SnapPropertiesToPixel();
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetThickness(Thickness);
	}
}

void USCADAEllipseWidget::SetLineStyle(ESCADALineStyle InStyle)
{
	LineStyle = InStyle;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetLineStyle(LineStyle);
	}
}

void USCADAEllipseWidget::SetFillColor(FLinearColor InColor)
{
	FillColor = InColor;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFillColor(FillColor);
	}
}

void USCADAEllipseWidget::SetFillPattern(ESCADAFillPattern InPattern)
{
	FillPattern = InPattern;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFillPattern(FillPattern);
	}
}

void USCADAEllipseWidget::SetFlashEnabled(bool bInEnabled)
{
	bFlashEnabled = bInEnabled;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADAEllipseWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColor = InColor;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFlashColor(FlashColor);
	}
}

#if WITH_EDITOR
void USCADAEllipseWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 编辑器属性面板输入完成后立即取整，面板显示值即为最终绘制值
	SnapPropertiesToPixel();

	// 中心/半径被编辑 → 槽矩形（X/Y/宽/高）同步更新（须在 Super 之前，
	// Super 链上的 SynchronizeProperties 以槽为准回写，槽先更新才不会把新值冲掉）。
	const FName MemberPropName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropName == GET_MEMBER_NAME_CHECKED(USCADAEllipseWidget, Center)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADAEllipseWidget, RadiusX)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADAEllipseWidget, RadiusY))
	{
		SyncSlotFromGeometry();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

const FText USCADAEllipseWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
