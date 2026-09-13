#pragma once

#include "Core/SeinUserWidget.h"
#include "SeinWidgetClickTestTypes.generated.h"

/** Observes the Blueprint event dispatch used by the native UMG mouse handlers. */
UCLASS()
class USeinWidgetClickTestWidget : public USeinUserWidget
{
	GENERATED_BODY()

public:
	using USeinUserWidget::NativeOnMouseButtonDown;
	using USeinUserWidget::NativeOnMouseButtonUp;
	using USeinUserWidget::NativeOnMouseButtonDoubleClick;

	FReply EventReply = FReply::Unhandled();
	TArray<FName> DispatchedEvents;
	virtual void ProcessEvent(UFunction* Function, void* Parameters) override;
};
