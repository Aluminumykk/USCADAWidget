// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "Binding/PropertyBinding.h"
#include "SCADAVector2DBinding.generated.h"

/**
 * FVector2D 类型的属性绑定支持类（"Bind to property" 路径的原生 binder）。
 *
 * 引擎内置 binder 只覆盖 bool/int32/float/FText/颜色/可见性等（UMG Public/Binding/*.h），
 * 没有 FVector2D；UWidget::FindBinderClassForDestination 经
 * GetDerivedClasses(UPropertyBinding) 自动发现全部子类，故在本模块声明本类即完成注册。
 *
 * 结构照 UFloatBinding：Bind 不必重写——UPropertyBinding::Bind 缺省实现即把
 * ScriptDelegate 绑到自身名为 GetValue 的 UFunction，只要 GetValue 返回值类型
 * 与委托签名一致（FVector2D）即可。"Create Binding"（绑定到 BP 函数）路径
 * 不经过 binder 类（直接 BindUFunction），两类路径产生的委托都由控件侧
 * 属性绑定轮询 Ticker 每帧 Execute() 取值。
 */
UCLASS()
class USCADAWIDGET_API USCADAVector2DBinding : public UPropertyBinding
{
	GENERATED_BODY()

public:

	USCADAVector2DBinding();

	virtual bool IsSupportedSource(FProperty* Property) const override;
	virtual bool IsSupportedDestination(FProperty* Property) const override;

	UFUNCTION()
	FVector2D GetValue() const;
};
