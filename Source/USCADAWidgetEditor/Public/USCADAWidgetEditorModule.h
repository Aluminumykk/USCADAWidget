// Copyright Pr_UEDraw. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IDesignerExtensionFactory;

/**
 * USCADAWidget 编辑器模块：向 UMG 设计器注册 SCADA 图元的设计器交互扩展
 * （折线折点/线段端点直接拖动手柄，见 FSCADAVertexDragExtension；
 *  画布"适配内容"按钮，见 FSCADACanvasFitExtension）。
 * Editor-only，不参与打包。
 */
class FUSCADAWidgetEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** 设计器扩展工厂（StartupModule 注册到 UMGEditor，ShutdownModule 反注册；
	 *  每个打开的设计器实例化一个扩展） */
	TSharedPtr<IDesignerExtensionFactory> ExtensionFactory;
	/** 画布"适配内容"扩展工厂（与顶点扩展工厂并列注册/注销） */
	TSharedPtr<IDesignerExtensionFactory> CanvasFitExtensionFactory;
};
