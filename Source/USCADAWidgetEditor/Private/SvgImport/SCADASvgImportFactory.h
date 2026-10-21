// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SCADASvgImportFactory.generated.h"

/**
 * SVG 文件导入工厂：把 .svg 导入为 SCADA 图元 Widget Blueprint 资产
 * （内容浏览器拖入 .svg / 右键 Import 触发；一个 SVG = 一个可复用 UMG 元件）。
 * 解析见 SCADASvgDocument，控件树构建见 cpp 的 BuildWidgetTree。
 */
UCLASS()
class USCADASvgImportFactory : public UFactory
{
	GENERATED_BODY()

public:
	USCADASvgImportFactory(const FObjectInitializer& ObjectInitializer);

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
		const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	//~ End UFactory Interface
};
