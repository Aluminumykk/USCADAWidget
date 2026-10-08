// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADALineWidget.h"

#include "Slate/SSCADALine.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADALineWidget::RebuildWidget()
{
	MyLine = SNew(SSCADALine);

	// UWidget 没有原生 Tick，借 Slate 层的 Tick 轮询槽矩形变化
	TWeakObjectPtr<USCADALineWidget> WeakThis(this);
	MyLine->ExternalTickCallback = [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CheckSlotSync();
		}
	};

	return MyLine.ToSharedRef();
}

void USCADALineWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 以点位为基准：槽矩形始终跟随点位包围盒。
	// 反方向（设计器拖动/拉伸槽 → 回写点位）由 Tick 轮询 CheckSlotSync 闭环，
	// 因此这里无条件把槽对齐到点位，保证编译/重建后点位编辑不丢失。
	SyncSlotFromPoints();

	if (MyLine.IsValid())
	{
		PushPointsToSlate();
		MyLine->SetColor(Color);
		MyLine->SetThickness(Thickness);
		MyLine->SetLineStyle(LineStyle);
		MyLine->SetLineStartStyle(LineStartStyle);
		MyLine->SetLineEndStyle(LineEndStyle);
		MyLine->SetLineCap(LineCap);
		MyLine->SetFlashEnabled(bFlashEnabled);
		MyLine->SetFlashColor(FlashColor);
	}
}

void USCADALineWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyLine.Reset();
}

void USCADALineWidget::PushPointsToSlate()
{
	if (MyLine.IsValid())
	{
		// 直线 = 两点折线；Slate 层保持折线能力，供后续折线控件复用。
		// Slate 拿到的是槽内局部坐标：画布设计坐标 - 槽位置。
		FVector2D SlotPos = FVector2D::ZeroVector;
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			SlotPos = CanvasSlot->GetPosition();
		}

		TArray<FVector2D> TwoPoints = { StartPoint - SlotPos, EndPoint - SlotPos };
		MyLine->SetPoints(TwoPoints);
	}
}

void USCADALineWidget::SnapPropertiesToPixel()
{
	StartPoint.X = FMath::RoundToFloat(StartPoint.X);
	StartPoint.Y = FMath::RoundToFloat(StartPoint.Y);
	EndPoint.X = FMath::RoundToFloat(EndPoint.X);
	EndPoint.Y = FMath::RoundToFloat(EndPoint.Y);
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 1.0f);
}

void USCADALineWidget::GetPointsBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	OutMin = FVector2D(FMath::Min(StartPoint.X, EndPoint.X), FMath::Min(StartPoint.Y, EndPoint.Y));
	OutMax = FVector2D(FMath::Max(StartPoint.X, EndPoint.X), FMath::Max(StartPoint.Y, EndPoint.Y));
}

void USCADALineWidget::SyncSlotFromPoints()
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

void USCADALineWidget::ForceSyncFromSlotRect()
{
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		CheckSlotSync();
	}
}

void USCADALineWidget::CheckSlotSync()
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
		CachedSlotPos = RawPos;
		CachedSlotSize = RawSize;
		bPrevRawNegX = RawSize.X < 0.0;
		bPrevRawNegY = RawSize.Y < 0.0;
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
	StartPoint = CPos + (StartPoint - CachedSlotPos) * Scale;
	EndPoint = CPos + (EndPoint - CachedSlotPos) * Scale;

	// 翻转沿：在新规范矩形内镜像
	if (bToggleX)
	{
		StartPoint.X = CPos.X + (CSize.X - (StartPoint.X - CPos.X));
		EndPoint.X = CPos.X + (CSize.X - (EndPoint.X - CPos.X));
	}
	if (bToggleY)
	{
		StartPoint.Y = CPos.Y + (CSize.Y - (StartPoint.Y - CPos.Y));
		EndPoint.Y = CPos.Y + (CSize.Y - (EndPoint.Y - CPos.Y));
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

void USCADALineWidget::SetStartPoint(FVector2D InPoint)
{
	StartPoint = InPoint;
	SnapPropertiesToPixel();
	SyncSlotFromPoints();
}

void USCADALineWidget::SetEndPoint(FVector2D InPoint)
{
	EndPoint = InPoint;
	SnapPropertiesToPixel();
	SyncSlotFromPoints();
}

void USCADALineWidget::SetColor(FLinearColor InColor)
{
	Color = InColor;
	if (MyLine.IsValid())
	{
		MyLine->SetColor(Color);
	}
}

void USCADALineWidget::SetThickness(float InThickness)
{
	Thickness = InThickness;
	SnapPropertiesToPixel();
	if (MyLine.IsValid())
	{
		MyLine->SetThickness(Thickness);
	}
}

void USCADALineWidget::SetLineStyle(ESCADALineStyle InStyle)
{
	LineStyle = InStyle;
	if (MyLine.IsValid())
	{
		MyLine->SetLineStyle(LineStyle);
	}
}

void USCADALineWidget::SetLineStartStyle(ESCADALineEndStyle InStyle)
{
	LineStartStyle = InStyle;
	if (MyLine.IsValid())
	{
		MyLine->SetLineStartStyle(LineStartStyle);
	}
}

void USCADALineWidget::SetLineEndStyle(ESCADALineEndStyle InStyle)
{
	LineEndStyle = InStyle;
	if (MyLine.IsValid())
	{
		MyLine->SetLineEndStyle(LineEndStyle);
	}
}

void USCADALineWidget::SetLineCap(ESCADALineCap InCap)
{
	LineCap = InCap;
	if (MyLine.IsValid())
	{
		MyLine->SetLineCap(LineCap);
	}
}

void USCADALineWidget::SetFlashEnabled(bool bInEnabled)
{
	bFlashEnabled = bInEnabled;
	if (MyLine.IsValid())
	{
		MyLine->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADALineWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColor = InColor;
	if (MyLine.IsValid())
	{
		MyLine->SetFlashColor(FlashColor);
	}
}

#if WITH_EDITOR
void USCADALineWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 编辑器属性面板输入完成后立即取整，面板显示值即为最终绘制值
	SnapPropertiesToPixel();

	// 端点坐标被编辑 → 槽矩形（X/Y/宽/高）同步更新（须在 Super 之前，
	// Super 链上的 SynchronizeProperties 以槽为准回写，槽先更新才不会把新值冲掉）。
	// 注意要查 MemberProperty：编辑 StartPoint.X 这类子字段时 Property 是内层的 X，
	// 只有 MemberProperty（所属成员）才是 StartPoint/EndPoint 本身。
	const FName MemberPropName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropName == GET_MEMBER_NAME_CHECKED(USCADALineWidget, StartPoint) ||
		MemberPropName == GET_MEMBER_NAME_CHECKED(USCADALineWidget, EndPoint))
	{
		SyncSlotFromPoints();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

const FText USCADALineWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
