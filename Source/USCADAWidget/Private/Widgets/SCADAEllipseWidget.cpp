// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADAEllipseWidget.h"

#include "Slate/SSCADAEllipse.h"
#include "Components/CanvasPanelSlot.h"

#define LOCTEXT_NAMESPACE "USCADAWidget"

TSharedRef<SWidget> USCADAEllipseWidget::RebuildWidget()
{
	MyEllipse = SNew(SSCADAEllipse);

	// 挂接 Slate Tick 回调 + 注册核心 Ticker 兜底（编辑器/非游戏世界守卫在基类内部）
	SetupSlotSyncCallbacks(MyEllipse.Get());

	return MyEllipse.ToSharedRef();
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
		MyEllipse->SetFlashInverted(bFlashInverted);
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
		// 椭圆的槽矩形恒等于包围盒，本地中心恒为 (RadiusX, RadiusY)（即原单 Setter 链
		// !bCenterExplicit 中心跟随分支的终态值），批量接口一次写入、只重采样一次
		MyEllipse->SetGeometry(RadiusX, RadiusY, FVector2D(RadiusX, RadiusY));
	}
}

void USCADAEllipseWidget::SnapPropertiesToPixel()
{
	Center.X = FMath::RoundToFloat(Center.X);
	Center.Y = FMath::RoundToFloat(Center.Y);
	RadiusX = FMath::Max(FMath::RoundToFloat(RadiusX), 1.0f);
	RadiusY = FMath::Max(FMath::RoundToFloat(RadiusY), 1.0f);
	Thickness = FMath::Max(FMath::RoundToFloat(Thickness), 0.0f);
}

void USCADAEllipseWidget::GetGeometryBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	OutMin = Center - FVector2D(RadiusX, RadiusY);
	OutMax = Center + FVector2D(RadiusX, RadiusY);
}

void USCADAEllipseWidget::TranslateGeometryBy(const FVector2D& Delta)
{
	Center += Delta;
}

void USCADAEllipseWidget::MapSlotToGeometry(UCanvasPanelSlot* CanvasSlot, const FVector2D& RawPos, const FVector2D& RawSize, bool bInteractionStart)
{
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

void USCADAEllipseWidget::SetFlashInverted(bool bInInverted)
{
	bFlashInverted = bInInverted;
	if (MyEllipse.IsValid())
	{
		MyEllipse->SetFlashInverted(bFlashInverted);
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
