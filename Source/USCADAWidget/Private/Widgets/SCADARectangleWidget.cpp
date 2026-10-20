// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADARectangleWidget.h"

#include "Slate/SSCADARectangle.h"
#include "Components/CanvasPanelSlot.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADARectangleWidget::RebuildWidget()
{
	MyRectangle = SNew(SSCADARectangle);

	// 挂接 Slate Tick 回调 + 注册核心 Ticker 兜底（编辑器/非游戏世界守卫在基类内部）
	SetupSlotSyncCallbacks(MyRectangle.Get());

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
		MyRectangle->SetFlashInverted(bFlashInverted);
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
		// 批量推送：宽/高/圆角一次写入，Slate 层只重采样一次
		MyRectangle->SetGeometry((float)Size.X, (float)Size.Y, CornerRadiusX, CornerRadiusY);
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
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 0.0f);
}

void USCADARectangleWidget::GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	OutMin = Position;
	OutMax = Position + Size;
}

void USCADARectangleWidget::TranslateGeometryBy(const FVector2D& Delta)
{
	Position += Delta;
}

void USCADARectangleWidget::MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart)
{
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

void USCADARectangleWidget::SetFlashInverted(bool bInInverted)
{
	bFlashInverted = bInInverted;
	if (MyRectangle.IsValid())
	{
		MyRectangle->SetFlashInverted(bFlashInverted);
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
