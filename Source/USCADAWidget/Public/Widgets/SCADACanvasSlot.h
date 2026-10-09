// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/CanvasPanelSlot.h"
#include "SCADACanvasSlot.generated.h"

/**
 * SCADA 画布槽：继承 UCanvasPanelSlot（保留设计器拖动/锚点/自动尺寸等全部行为），
 * 额外在槽属性被修改时通知内容控件——让 SCADA 图元（如直线）把端点坐标回写到
 * 真实资产实例（UMG 设计器拖动只改槽对象，不通知内容控件，直接改的话保存后/编译会丢）。
 */
UCLASS()
class USCADAWIDGET_API USCADACanvasSlot : public UCanvasPanelSlot
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

	/** 设计器面板拖放/拖过预览的落点写入通道（引擎在 OnDrop/OnDragOver 时调用，
	 *  直接 SetPosition/SetSize 不走属性通知）——转发给内容控件让其同步认领落点 */
	virtual bool DragDropPreviewByDesigner(const FVector2D& LocalCursorPosition, const TOptional<int32>& XGridSnapSize, const TOptional<int32>& YGridSnapSize) override;
	/** 设计器方向键微调通道（同样不走属性通知）——转发给内容控件 */
	virtual bool NudgeByDesigner(const FVector2D& NudgeDirection, const TOptional<int32>& GridSnapSize) override;
#endif

	/** 槽矩形发生变化后通知内容控件（如 USCADALineWidget 回写端点坐标） */
	void NotifyContentSlotGeometryChanged();
};
