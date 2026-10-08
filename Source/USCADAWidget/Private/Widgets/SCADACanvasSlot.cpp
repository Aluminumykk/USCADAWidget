// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADACanvasSlot.h"

#include "Widgets/SCADALineWidget.h"
#include "Widgets/SCADAPolylineWidget.h"

#if WITH_EDITOR
void USCADACanvasSlot::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NotifyContentSlotGeometryChanged();
}

void USCADACanvasSlot::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	NotifyContentSlotGeometryChanged();
}
#endif

void USCADACanvasSlot::NotifyContentSlotGeometryChanged()
{
	if (USCADALineWidget* Line = Cast<USCADALineWidget>(Content))
	{
		Line->ForceSyncFromSlotRect();
	}
	else if (USCADAPolylineWidget* Polyline = Cast<USCADAPolylineWidget>(Content))
	{
		Polyline->ForceSyncFromSlotRect();
	}
}
