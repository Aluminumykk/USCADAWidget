// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADARectangleWidget.h"

#include "Slate/SSCADARectangle.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADARectangleWidget::RebuildWidget()
{
	MyRectangle = SNew(SSCADARectangle);

	// UWidget 没有原生 Tick，借 Slate 层的 Tick 轮询槽矩形变化
	TWeakObjectPtr<USCADARectangleWidget> WeakThis(this);
	MyRectangle->ExternalTickCallback = [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CheckSlotSync();
		}
	};

	return MyRectangle.ToSharedRef();
}

void USCADARectangleWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// 以位置/尺寸为基准：槽矩形始终等于矩形本身。
	// 反方向（设计器拖动/拉伸槽 → 回写位置/尺寸）由 Tick 轮询 CheckSlotSync 闭环，
	// 因此这里无条件把槽对齐到几何数据，保证编译/重建后属性编辑不丢失。
	SyncSlotFromGeometry();

	if (MyRectangle.IsValid())
	{
		PushGeometryToSlate();
		MyRectangle->SetColor(BorderColor);
		MyRectangle->SetThickness(Thickness);
		MyRectangle->SetLineStyle(LineStyle);
		MyRectangle->SetFillColor(FillColor);
		MyRectangle->SetFillPattern(FillPattern);
		MyRectangle->SetFlashEnabled(bFlashEnabled);
		MyRectangle->SetFlashColor(FlashColor);
	}
}

void USCADARectangleWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyRectangle.Reset();
}

void USCADARectangleWidget::PushGeometryToSlate()
{
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetWidth((float)Size.X);
		MyRectangle->SetHeight((float)Size.Y);
		MyRectangle->SetCornerRadiusX(CornerRadiusX);
		MyRectangle->SetCornerRadiusY(CornerRadiusY);
	}
}

void USCADARectangleWidget::SnapPropertiesToPixel()
{
	Position.X = FMath::RoundToFloat(Position.X);
	Position.Y = FMath::RoundToFloat(Position.Y);
	Size.X = FMath::Max(FMath::RoundToFloat(Size.X), 1.0);
	Size.Y = FMath::Max(FMath::RoundToFloat(Size.Y), 1.0);
	const float MaxR = (float)FMath::Min(Size.X, Size.Y) * 0.5f;
	CornerRadiusX = FMath::Clamp(FMath::RoundToFloat(CornerRadiusX), 0.0f, MaxR);
	CornerRadiusY = FMath::Clamp(FMath::RoundToFloat(CornerRadiusY), 0.0f, MaxR);
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 1.0f);
}

void USCADARectangleWidget::SyncSlotFromGeometry()
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

	// 槽矩形 = 矩形本身（位置 + 正尺寸）
	const FVector2D ClampedSize(FMath::Max(Size.X, 1.0), FMath::Max(Size.Y, 1.0));

	CanvasSlot->SetAutoSize(false);
	CanvasSlot->SetPosition(Position);
	CanvasSlot->SetSize(ClampedSize);
	CachedSlotPos = Position;
	CachedSlotSize = ClampedSize;
	bSlotCacheValid = true;
	// 几何数据为真值：任何正向同步都复位交互簿记（符号基线/收尾标记）
	bPrevRawNegX = false;
	bPrevRawNegY = false;
	bPendingFinalize = false;

	PushGeometryToSlate();
}

void USCADARectangleWidget::ForceSyncFromSlotRect()
{
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		CheckSlotSync();
	}
}

void USCADARectangleWidget::CheckSlotSync()
{
	if (bSyncingGeometry)
	{
		return;
	}

	// 设计器拖动/拉伸槽的处理（显示与存储分离：位置/尺寸是唯一真值，槽只是操作手柄）。
	// 槽永远保持正向规范矩形（min 位置 + 正尺寸），布局不受负尺寸影响，绘制不跳；
	// 位置/尺寸映射在规范空间增量进行；矩形在自身内镜像 = 自身，翻转沿为 no-op。
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!CanvasSlot)
	{
		return;
	}

	const FVector2D RawPos = CanvasSlot->GetPosition();
	const FVector2D RawSize = CanvasSlot->GetSize();

	// 缓存未初始化（新实例首次 Tick）：只采纳当前槽矩形为基准，不做映射，
	// 否则默认 1x1 缓存会把健康槽矩形误判成外部缩放输入，把尺寸放大到爆炸。
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

	// 规范化：min 位置 + 正尺寸；记录原始符号（矩形镜像 = 自身，符号沿只做基线跟踪）
	FVector2D CPos = RawPos;
	FVector2D CSize = RawSize;
	const bool bNegX = CSize.X < 0.0;
	const bool bNegY = CSize.Y < 0.0;
	if (bNegX) { CPos.X += CSize.X; CSize.X = -CSize.X; }
	if (bNegY) { CPos.Y += CSize.Y; CSize.Y = -CSize.Y; }
	bPrevRawNegX = bNegX;
	bPrevRawNegY = bNegY;

	// 规范空间增量映射（正尺寸，无符号问题），除零保护钳到 1。
	// 注意：交互过程中尺寸不能钳下限——拖过对边翻转时槽尺寸会穿过 ~0，
	// 钳位会把尺寸钉死、之后增量缩放从钳位值起算，比例永久失真、无法恢复。
	// 下限钳位只在做收尾的 SnapPropertiesToPixel 里进行。
	FVector2D OldSize = CachedSlotSize;
	if (FMath::IsNearlyZero((float)OldSize.X)) { OldSize.X = 1.0; }
	if (FMath::IsNearlyZero((float)OldSize.Y)) { OldSize.Y = 1.0; }
	const FVector2D Scale(CSize.X / OldSize.X, CSize.Y / OldSize.Y);
	Position = CPos + (Position - CachedSlotPos) * Scale;
	Size = Size * Scale;

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

void USCADARectangleWidget::SetPosition(FVector2D InPosition)
{
	Position = InPosition;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADARectangleWidget::SetSize(FVector2D InSize)
{
	Size = InSize;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADARectangleWidget::SetCornerRadiusX(float InRadiusX)
{
	CornerRadiusX = InRadiusX;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADARectangleWidget::SetCornerRadiusY(float InRadiusY)
{
	CornerRadiusY = InRadiusY;
	SnapPropertiesToPixel();
	SyncSlotFromGeometry();
}

void USCADARectangleWidget::SetBorderColor(FLinearColor InColor)
{
	BorderColor = InColor;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetColor(BorderColor);
	}
}

void USCADARectangleWidget::SetThickness(float InThickness)
{
	Thickness = InThickness;
	SnapPropertiesToPixel();
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetThickness(Thickness);
	}
}

void USCADARectangleWidget::SetLineStyle(ESCADALineStyle InStyle)
{
	LineStyle = InStyle;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetLineStyle(LineStyle);
	}
}

void USCADARectangleWidget::SetFillColor(FLinearColor InColor)
{
	FillColor = InColor;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFillColor(FillColor);
	}
}

void USCADARectangleWidget::SetFillPattern(ESCADAFillPattern InPattern)
{
	FillPattern = InPattern;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFillPattern(FillPattern);
	}
}

void USCADARectangleWidget::SetFlashEnabled(bool bInEnabled)
{
	bFlashEnabled = bInEnabled;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFlashEnabled(bFlashEnabled);
	}
}

void USCADARectangleWidget::SetFlashColor(FLinearColor InColor)
{
	FlashColor = InColor;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFlashColor(FlashColor);
	}
}

#if WITH_EDITOR
void USCADARectangleWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 编辑器属性面板输入完成后立即取整，面板显示值即为最终绘制值
	SnapPropertiesToPixel();

	// 位置/尺寸/圆角被编辑 → 槽矩形（X/Y/宽/高）同步更新（须在 Super 之前，
	// Super 链上的 SynchronizeProperties 以槽为准回写，槽先更新才不会把新值冲掉）。
	const FName MemberPropName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberPropName == GET_MEMBER_NAME_CHECKED(USCADARectangleWidget, Position)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADARectangleWidget, Size)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADARectangleWidget, CornerRadiusX)
		|| MemberPropName == GET_MEMBER_NAME_CHECKED(USCADARectangleWidget, CornerRadiusY))
	{
		SyncSlotFromGeometry();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

const FText USCADARectangleWidget::GetPaletteCategory()
{
	return LOCTEXT("SCADA", "SCADA");
}
#endif

#undef LOCTEXT_NAMESPACE
