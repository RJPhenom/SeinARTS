#include "SeinARTS_Framework_EditorModule.h"
#include "SAFCameraPawn.h"
#include "SAFCameraPawnCustomization.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"

IMPLEMENT_MODULE(FSeinARTS_Framework_Editor, SeinARTS_Framework_Editor);

void FSeinARTS_Framework_Editor::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(ASAFCameraPawn::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FSAFCameraPawnCustomization::MakeInstance));
}

void FSeinARTS_Framework_Editor::ShutdownModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout(ASAFCameraPawn::StaticClass()->GetFName());
}
