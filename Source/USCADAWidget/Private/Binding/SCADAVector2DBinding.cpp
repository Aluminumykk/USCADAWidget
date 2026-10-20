// Copyright Pr_UEDraw. All Rights Reserved.

#include "Binding/SCADAVector2DBinding.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SCADAVector2DBinding)

USCADAVector2DBinding::USCADAVector2DBinding()
{
}

bool USCADAVector2DBinding::IsSupportedDestination(FProperty* Property) const
{
	return IsSupportedSource(Property);
}

bool USCADAVector2DBinding::IsSupportedSource(FProperty* Property) const
{
	return IsConcreteTypeCompatibleWithReflectedType<FVector2D>(Property);
}

FVector2D USCADAVector2DBinding::GetValue() const
{
	if (UObject* Source = SourceObject.Get())
	{
		FVector2D VectorValue = FVector2D::ZeroVector;
		if (SourcePath.Resolve(Source) && SourcePath.GetValue<FVector2D>(Source, VectorValue))
		{
			return VectorValue;
		}
	}

	return FVector2D::ZeroVector;
}
