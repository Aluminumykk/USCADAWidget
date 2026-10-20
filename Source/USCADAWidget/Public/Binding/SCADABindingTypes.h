// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "UObject/ObjectMacros.h"

#include "SCADABindingTypes.generated.h"

/*
 * UMG 属性绑定（细节面板 Bind 下拉）的伴随委托类型集合。
 *
 * 命名风格照引擎内置的 FGetText / FGetLinearColor / FGetFloat（UMG Public/Components/Widget.h），
 * 加 SCADA 前缀避免与同工程其它插件的类型歧义（SCADAWidgetTypes.h 改名教训同理）。
 *
 * 控件类内声明与可绑定属性配套的委托属性时（属性名原样 + "Delegate"，bool 属性含 b 前缀），
 * 必须用这里的类型，且返回值类型与目标属性类型完全一致——
 * UMGEditor 的 FindDelegateProperty 以 SignatureFunction 返回值 SameType(属性) 为硬条件；
 * "Bind to property" 路径还要求委托无参数（GenerateBinder 的 NumParms==1 判定，本组均满足）。
 *
 * FVector2D 引擎无内置 binder（内置仅 bool/int32/float/FText/颜色/可见性等），
 * 由本模块 USCADAVector2DBinding（UPropertyBinding 子类，经 GetDerivedClasses 自动发现）补齐。
 *
 * 纯委托声明头必须给每个动态委托加 UDELEGATE() 标记，UHT 才会生成其签名函数
 * （引擎范本 UMG Public/Animation/WidgetAnimationEvents.h；含其它反射类型的头
 * 如 Widget.h 才有免标记的历史行为）。
 */

/** 颜色类属性（FLinearColor）绑定委托（引擎 UColorBinding 支持该目标类型） */
UDELEGATE()
DECLARE_DYNAMIC_DELEGATE_RetVal(FLinearColor, FGetSCADALinearColor);

/** 数值类属性（float）绑定委托（引擎 UFloatBinding 支持该目标类型） */
UDELEGATE()
DECLARE_DYNAMIC_DELEGATE_RetVal(float, FGetSCADAFloat);

/** 布尔属性（bool）绑定委托（引擎 UBoolBinding 支持该目标类型） */
UDELEGATE()
DECLARE_DYNAMIC_DELEGATE_RetVal(bool, FGetSCADABool);

/** 二维坐标属性（FVector2D）绑定委托（配套自定义 binder：USCADAVector2DBinding） */
UDELEGATE()
DECLARE_DYNAMIC_DELEGATE_RetVal(FVector2D, FGetSCADAVector2D);
