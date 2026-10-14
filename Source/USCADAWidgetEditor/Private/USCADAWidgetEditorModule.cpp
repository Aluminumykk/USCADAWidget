// Copyright Pr_UEDraw. All Rights Reserved.

#include "USCADAWidgetEditorModule.h"

#include "SCADAVertexDragExtension.h"
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

void FUSCADAWidgetEditorModule::StartupModule()
{
	IUMGEditorModule& UMGEditor = FModuleManager::LoadModuleChecked<IUMGEditorModule>("UMGEditor");
	ExtensionFactory = MakeShared<FSCADAVertexDragExtensionFactory>();
	UMGEditor.GetDesignerExtensibilityManager()->AddDesignerExtensionFactory(ExtensionFactory.ToSharedRef());
}

void FUSCADAWidgetEditorModule::ShutdownModule()
{
	if (ExtensionFactory.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("UMGEditor"))
		{
			IUMGEditorModule& UMGEditor = FModuleManager::GetModuleChecked<IUMGEditorModule>("UMGEditor");
			UMGEditor.GetDesignerExtensibilityManager()->RemoveDesignerExtensionFactory(ExtensionFactory.ToSharedRef());
		}
		ExtensionFactory.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUSCADAWidgetEditorModule, USCADAWidgetEditor)
