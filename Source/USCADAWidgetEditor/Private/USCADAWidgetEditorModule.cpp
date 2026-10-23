// Copyright Pr_UEDraw. All Rights Reserved.

#include "USCADAWidgetEditorModule.h"

#include "SCADAVertexDragExtension.h"
#include "SCADACanvasFitExtension.h"
#include "IHasDesignerExtensibility.h"
#include "Modules/ModuleManager.h"
#include "UMGEditorModule.h"

#define LOCTEXT_NAMESPACE "USCADAWidgetEditor"

/** 顶点拖动扩展工厂：每个 UMG 设计器实例化一个扩展 */
class FSCADAVertexDragExtensionFactory : public IDesignerExtensionFactory
{
public:
	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override
	{
		return MakeShared<FSCADAVertexDragExtension>();
	}
};

/** 画布"适配内容"扩展工厂：每个 UMG 设计器实例化一个扩展 */
class FSCADACanvasFitExtensionFactory : public IDesignerExtensionFactory
{
public:
	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override
	{
		return MakeShared<FSCADACanvasFitExtension>();
	}
};

void FUSCADAWidgetEditorModule::StartupModule()
{
	IUMGEditorModule& UMGEditor = FModuleManager::LoadModuleChecked<IUMGEditorModule>("UMGEditor");
	ExtensionFactory = MakeShared<FSCADAVertexDragExtensionFactory>();
	UMGEditor.GetDesignerExtensibilityManager()->AddDesignerExtensionFactory(ExtensionFactory.ToSharedRef());
	CanvasFitExtensionFactory = MakeShared<FSCADACanvasFitExtensionFactory>();
	UMGEditor.GetDesignerExtensibilityManager()->AddDesignerExtensionFactory(CanvasFitExtensionFactory.ToSharedRef());
}

void FUSCADAWidgetEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("UMGEditor"))
	{
		IUMGEditorModule& UMGEditor = FModuleManager::GetModuleChecked<IUMGEditorModule>("UMGEditor");
		if (ExtensionFactory.IsValid())
		{
			UMGEditor.GetDesignerExtensibilityManager()->RemoveDesignerExtensionFactory(ExtensionFactory.ToSharedRef());
		}
		if (CanvasFitExtensionFactory.IsValid())
		{
			UMGEditor.GetDesignerExtensibilityManager()->RemoveDesignerExtensionFactory(CanvasFitExtensionFactory.ToSharedRef());
		}
	}
	ExtensionFactory.Reset();
	CanvasFitExtensionFactory.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUSCADAWidgetEditorModule, USCADAWidgetEditor)
