/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldWidgetPool.cpp
 * @brief   World widget pool implementation.
 */

#include "Utility/SeinWorldWidgetPool.h"
#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"

void USeinWorldWidgetPool::Initialize(UPanelWidget* InParent, TSubclassOf<UUserWidget> InWidgetClass, int32 InitialPoolSize)
{
	ParentWidget = InParent;
	WidgetClass = InWidgetClass;

	if (!InParent || !InWidgetClass)
	{
		return;
	}

	// Pre-create widgets
	Pool.Reserve(InitialPoolSize);
	for (int32 i = 0; i < InitialPoolSize; ++i)
	{
		UUserWidget* Widget = CreatePooledWidget();
		if (Widget)
		{
			Pool.Add(Widget);
		}
	}
}

UUserWidget* USeinWorldWidgetPool::Acquire(FSeinEntityHandle Entity)
{
	if (!Entity.IsValid())
	{
		return nullptr;
	}

	// Return existing active widget if already acquired
	TObjectPtr<UUserWidget>* Found = ActiveWidgets.Find(Entity);
	if (Found && *Found)
	{
		return *Found;
	}

	// Take from pool or create new
	UUserWidget* Widget = nullptr;
	if (Pool.Num() > 0)
	{
		Widget = Pool.Pop();
	}
	else
	{
		Widget = CreatePooledWidget();
	}

	if (Widget)
	{
		Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
		ActiveWidgets.Add(Entity, Widget);
	}

	return Widget;
}

void USeinWorldWidgetPool::Release(FSeinEntityHandle Entity)
{
	TObjectPtr<UUserWidget> Widget;
	if (ActiveWidgets.RemoveAndCopyValue(Entity, Widget) && Widget)
	{
		Widget->SetVisibility(ESlateVisibility::Collapsed);
		Pool.Add(Widget);
	}
}

void USeinWorldWidgetPool::ReleaseAll()
{
	for (auto& Pair : ActiveWidgets)
	{
		if (Pair.Value)
		{
			Pair.Value->SetVisibility(ESlateVisibility::Collapsed);
			Pool.Add(Pair.Value);
		}
	}
	ActiveWidgets.Empty();
}

UUserWidget* USeinWorldWidgetPool::GetActiveWidget(FSeinEntityHandle Entity) const
{
	const TObjectPtr<UUserWidget>* Found = ActiveWidgets.Find(Entity);
	return (Found && *Found) ? *Found : nullptr;
}

UUserWidget* USeinWorldWidgetPool::CreatePooledWidget()
{
	if (!ParentWidget.IsValid() || !WidgetClass)
	{
		return nullptr;
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(ParentWidget->GetOwningPlayer(), WidgetClass);
	if (Widget)
	{
		ParentWidget->AddChild(Widget);
		Widget->SetVisibility(ESlateVisibility::Collapsed);
	}

	return Widget;
}
