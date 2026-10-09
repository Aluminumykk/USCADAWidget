// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADACanvasSlot.h"

#include "Widgets/SCADALineWidget.h"
#include "Widgets/SCADAPolylineWidget.h"
#include "Widgets/SCADAPolygonWidget.h"
#include "Widgets/SCADAEllipseWidget.h"
#include "Widgets/SCADACircleWidget.h"
#include "Widgets/SCADARectangleWidget.h"

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
	else if (USCADAPolygonWidget* Polygon = Cast<USCADAPolygonWidget>(Content))
	{
		Polygon->ForceSyncFromSlotRect();
	}
	else if (USCADAEllipseWidget* Ellipse = Cast<USCADAEllipseWidget>(Content))
	{
		Ellipse->ForceSyncFromSlotRect();
	}
	else if (USCADACircleWidget* Circle = Cast<USCADACircleWidget>(Content))
	{
		Circle->ForceSyncFromSlotRect();
	}
	else if (USCADARectangleWidget* Rectangle = Cast<USCADARectangleWidget>(Content))
	{
		Rectangle->ForceSyncFromSlotRect();
	}
}
