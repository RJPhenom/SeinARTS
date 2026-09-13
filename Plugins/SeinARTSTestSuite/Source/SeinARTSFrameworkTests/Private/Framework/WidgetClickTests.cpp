#include "CQTest.h"
#include "TestTypes/SeinWidgetClickTestTypes.h"

#include "InputCoreTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/SBoxPanel.h"

void USeinWidgetClickTestWidget::ProcessEvent(UFunction* Function, void* Parameters)
{
	const FName Name = Function->GetFName();
	if (Name == TEXT("OnMouseButtonDown") || Name == TEXT("OnMouseButtonUp")
		|| Name == TEXT("OnMouseButtonDoubleClick"))
	{
		DispatchedEvents.Add(Name);
		const FStructProperty* ReturnProperty = FindFProperty<FStructProperty>(Function, TEXT("ReturnValue"));
		check(ReturnProperty);
		ReturnProperty->ContainerPtrToValuePtr<FEventReply>(Parameters)->NativeReply = EventReply;
		return;
	}
	Super::ProcessEvent(Function, Parameters);
}

namespace UE::SeinARTSTests
{
	namespace
	{
		FPointerEvent MakeClick(const FKey Key)
		{
			return FPointerEvent(0, FVector2D::ZeroVector, FVector2D::ZeroVector,
				TSet<FKey>(), Key, 0.0f, FModifierKeysState());
		}

		using FMouseHandler = FReply (USeinWidgetClickTestWidget::*)(const FGeometry&, const FPointerEvent&);
		const FMouseHandler MouseHandlers[] = {
			&USeinWidgetClickTestWidget::NativeOnMouseButtonDown,
			&USeinWidgetClickTestWidget::NativeOnMouseButtonUp,
			&USeinWidgetClickTestWidget::NativeOnMouseButtonDoubleClick
		};
	}

	TEST(UnhandledLeftAndRightClicksAreConsumedAfterBlueprintDispatch, "SeinARTS.Unit.UI.WidgetClicks")
	{
		TStrongObjectPtr<USeinWidgetClickTestWidget> Widget(NewObject<USeinWidgetClickTestWidget>());
		ASSERT_THAT(IsTrue(Widget->bConsumeClicks));
		for (const FKey Key : {EKeys::LeftMouseButton, EKeys::RightMouseButton})
		{
			for (const FMouseHandler Handler : MouseHandlers)
			{
				const int32 Before = Widget->DispatchedEvents.Num();
				ASSERT_THAT(IsTrue((Widget.Get()->*Handler)(FGeometry(), MakeClick(Key)).IsEventHandled()));
				ASSERT_THAT(AreEqual(Before + 1, Widget->DispatchedEvents.Num()));
			}
		}
		ASSERT_THAT(IsTrue(Widget->DispatchedEvents[0] == TEXT("OnMouseButtonDown")));
		ASSERT_THAT(IsTrue(Widget->DispatchedEvents[1] == TEXT("OnMouseButtonUp")));
		ASSERT_THAT(IsTrue(Widget->DispatchedEvents[2] == TEXT("OnMouseButtonDoubleClick")));
	}

	TEST(OptOutAndOtherMouseButtonsKeepUnhandledReplies, "SeinARTS.Unit.UI.WidgetClicks")
	{
		TStrongObjectPtr<USeinWidgetClickTestWidget> Widget(NewObject<USeinWidgetClickTestWidget>());
		for (const FMouseHandler Handler : MouseHandlers)
		{
			ASSERT_THAT(IsFalse((Widget.Get()->*Handler)(FGeometry(), MakeClick(EKeys::MiddleMouseButton)).IsEventHandled()));
		}
		Widget->bConsumeClicks = false;
		for (const FKey Key : {EKeys::LeftMouseButton, EKeys::RightMouseButton})
		{
			for (const FMouseHandler Handler : MouseHandlers)
			{
				ASSERT_THAT(IsFalse((Widget.Get()->*Handler)(FGeometry(), MakeClick(Key)).IsEventHandled()));
			}
		}
		ASSERT_THAT(AreEqual(9, Widget->DispatchedEvents.Num()));
	}

	TEST(HandledBlueprintRepliesKeepCaptureAndFocusWithEitherSetting, "SeinARTS.Unit.UI.WidgetClicks")
	{
		TStrongObjectPtr<USeinWidgetClickTestWidget> Widget(NewObject<USeinWidgetClickTestWidget>());
		const TSharedRef<SVerticalBox> CaptureTarget = SNew(SVerticalBox);
		Widget->EventReply = FReply::Handled().CaptureMouse(CaptureTarget).SetUserFocus(CaptureTarget);
		for (const bool bConsume : {false, true})
		{
			Widget->bConsumeClicks = bConsume;
			for (const FKey Key : {EKeys::LeftMouseButton, EKeys::RightMouseButton})
			{
				for (const FMouseHandler Handler : MouseHandlers)
				{
					const FReply Reply = (Widget.Get()->*Handler)(FGeometry(), MakeClick(Key));
					ASSERT_THAT(IsTrue(Reply.IsEventHandled()));
					ASSERT_THAT(IsTrue(Reply.GetMouseCaptor() == CaptureTarget));
					ASSERT_THAT(IsTrue(Reply.GetUserFocusRecepient() == CaptureTarget));
				}
			}
		}
		ASSERT_THAT(AreEqual(12, Widget->DispatchedEvents.Num()));
	}
}
