/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinShowFlagsMenu.cpp
 * @author       RJ Macklem
 * @created      4 Sep 2026
 * @latest       4 Sep 2026
 * @brief        Adds a first-class SeinARTS category to level and PIE Show menus.
 *
 *               Unreal's show-flag group enum is fixed, so SeinARTS flags stay
 *               hidden from its generic Custom bucket and are surfaced here.
 *               Every entry toggles exactly like a native show flag: the editor
 *               path goes through the viewport client's own toggle handler, and
 *               the PIE path sets the game viewport's flag and then lifts Slate's
 *               menu throttle for one short window so the game viewport redraws
 *               while the menu is still open (the engine's private
 *               SLevelViewport::RefreshPIEViewport does the same for its flags).
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Viewport/SeinShowFlagsMenu.h"

#include "Application/ThrottleManager.h"
#include "Containers/Ticker.h"
#include "EditorViewportClient.h"
#include "Engine/GameViewportClient.h"
#include "LevelEditorMenuContext.h"
#include "SLevelViewport.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "SeinShowFlagsMenu"

namespace SeinShowFlagsMenu
{
	namespace
	{
		const FName MenuOwnerName(TEXT("SeinARTSShowFlagsMenu"));
		FDelegateHandle StartupCallbackHandle;

		struct FShowFlagDescriptor
		{
			/** Registered custom show-flag name (the TCustomShowFlag string). */
			const TCHAR* Name;
			FText Label;
			FText ToolTip;
			/** App-style brush key for the entry icon (SeinARTSEditorStyle
			 *  registers `ShowFlagsMenu.<Name>` from the BrandKit SVGs). */
			FName IconName;
			/** Engine brush shown until IconName is registered. NAME_None means
			 *  the entry simply has no icon while its SVG is missing. */
			FName FallbackIconName;
		};

		TConstArrayView<FShowFlagDescriptor> GetShowFlags()
		{
			static const FShowFlagDescriptor ShowFlags[] =
			{
				{
					TEXT("SeinExtents"),
					LOCTEXT("ExtentsLabel", "Extents"),
					LOCTEXT(
						"ExtentsToolTip",
						"Show or hide the SeinARTS entity-extents debug visualization in this viewport."),
					TEXT("ShowFlagsMenu.SeinExtents"),
					NAME_None
				},
				{
					TEXT("FogOfWar"),
					LOCTEXT("FogOfWarLabel", "Fog of War"),
					LOCTEXT(
						"FogOfWarToolTip",
						"Show or hide the SeinARTS fog-of-war debug visualization in this viewport."),
					TEXT("ShowFlagsMenu.FogOfWar"),
					NAME_None
				},
				{
					TEXT("SeinNavigation"),
					LOCTEXT("NavigationLabel", "Navigation"),
					LOCTEXT(
						"NavigationToolTip",
						"Show or hide the SeinARTS navigation debug visualization in this viewport. Independent of Unreal's own Navigation show flag."),
					TEXT("ShowFlagsMenu.SeinNavigation"),
					// Engine navigation glyph until SeinNavigationViewFlag.svg lands.
					TEXT("ShowFlagsMenu.Navigation")
				},
				{
					TEXT("SeinSteering"),
					LOCTEXT("SteeringLabel", "Steering"),
					LOCTEXT(
						"SteeringToolTip",
						"Show or hide the SeinARTS steering debug visualization in this viewport."),
					TEXT("ShowFlagsMenu.SeinSteering"),
					NAME_None
				}
			};

			return MakeArrayView(ShowFlags);
		}

		FSlateIcon ResolveIcon(const FShowFlagDescriptor& Descriptor)
		{
			const FName StyleSetName = FAppStyle::GetAppStyleSetName();
			const bool bHasOwnIcon =
				FAppStyle::Get().GetOptionalBrush(
					Descriptor.IconName, nullptr, nullptr) != nullptr;
			if (!bHasOwnIcon && !Descriptor.FallbackIconName.IsNone())
			{
				return FSlateIcon(StyleSetName, Descriptor.FallbackIconName);
			}
			return FSlateIcon(StyleSetName, Descriptor.IconName);
		}

		/** Equivalent of the engine's private SLevelViewport::RefreshPIEViewport.
		 *
		 *  The PIE game viewport only draws while Slate allows expensive tasks,
		 *  and an open menu holds Slate in responsive (throttled) mode. Without
		 *  this the flag flips in memory but nothing repaints until the menu
		 *  closes. Lift the throttle for the same 0.1 s window the engine uses,
		 *  timed on the core ticker so an ending PIE session cannot strand the
		 *  throttle disabled. */
		void RefreshPIEViewport()
		{
			FSlateThrottleManager::Get().DisableThrottle(true);
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([](float /*DeltaTime*/)
				{
					FSlateThrottleManager::Get().DisableThrottle(false);
					return false;
				}),
				0.1f);
		}

		void ToggleShowFlag(
			const FToolMenuContext& Context,
			const TCHAR* Name)
		{
			const int32 FlagIndex = FEngineShowFlags::FindIndexByName(Name);
			if (FlagIndex == INDEX_NONE)
			{
				return;
			}

			const TSharedPtr<SLevelViewport> Viewport =
				ULevelViewportContext::GetLevelViewport(Context);
			if (!Viewport)
			{
				return;
			}

			const FEngineShowFlags::EShowFlag ShowFlag =
				static_cast<FEngineShowFlags::EShowFlag>(FlagIndex);
			if (Viewport->IsPlayInEditorViewportActive())
			{
				// Same sequence as SLevelViewport::TogglePIEShowFlag.
				if (UGameViewportClient* GameViewport =
						Viewport->GetPlayClient())
				{
					FEngineShowFlags* ShowFlags =
						GameViewport->GetEngineShowFlags();
					ShowFlags->SetSingleFlag(
						ShowFlag, !ShowFlags->GetSingleFlag(ShowFlag));
					RefreshPIEViewport();
				}
			}
			else if (FEditorViewportClient* ViewportClient =
					Viewport->GetViewportClient().Get())
			{
				// Sets the flag and invalidates the (possibly non-realtime)
				// editor viewport, exactly like a native Show menu entry.
				ViewportClient->HandleToggleShowFlag(ShowFlag);
			}
		}

		/** Native Show entries grey out while a `ShowFlag.<Name> 0|1` console
		 *  override is pinning the flag; mirror that. */
		bool CanToggleShowFlag(
			const FToolMenuContext& /*Context*/,
			const TCHAR* Name)
		{
			const int32 FlagIndex = FEngineShowFlags::FindIndexByName(Name);
			return FlagIndex != INDEX_NONE
				&& FEngineShowFlags::IsForceFlagSet(
					static_cast<uint32>(FlagIndex));
		}

		ECheckBoxState GetShowFlagState(
			const FToolMenuContext& Context,
			const TCHAR* Name)
		{
			const int32 FlagIndex = FEngineShowFlags::FindIndexByName(Name);
			if (FlagIndex == INDEX_NONE)
			{
				return ECheckBoxState::Unchecked;
			}

			const TSharedPtr<SLevelViewport> Viewport =
				ULevelViewportContext::GetLevelViewport(Context);
			if (!Viewport)
			{
				return ECheckBoxState::Unchecked;
			}

			const FEngineShowFlags::EShowFlag ShowFlag =
				static_cast<FEngineShowFlags::EShowFlag>(FlagIndex);
			const TSharedPtr<FEditorViewportClient> ViewportClient =
				Viewport->GetViewportClient();
			bool bEnabled = false;
			if (Viewport->IsPlayInEditorViewportActive())
			{
				if (UGameViewportClient* GameViewport =
						Viewport->GetPlayClient())
				{
					bEnabled = GameViewport->GetEngineShowFlags()
						->GetSingleFlag(ShowFlag);
				}
			}
			else if (ViewportClient.IsValid())
			{
				bEnabled = ViewportClient->HandleIsShowFlagEnabled(ShowFlag);
			}
			return bEnabled
				? ECheckBoxState::Checked
				: ECheckBoxState::Unchecked;
		}

		void PopulateSubMenu(UToolMenu* Menu)
		{
			if (!Menu)
			{
				return;
			}

			FToolMenuSection& Section = Menu->FindOrAddSection(
				TEXT("SeinARTSShowFlags"));
			for (const FShowFlagDescriptor& Descriptor : GetShowFlags())
			{
				if (FEngineShowFlags::FindIndexByName(Descriptor.Name) ==
					INDEX_NONE)
				{
					continue;
				}

				FToolUIAction Action;
				Action.ExecuteAction = FToolMenuExecuteAction::CreateStatic(
					&ToggleShowFlag, Descriptor.Name);
				Action.CanExecuteAction =
					FToolMenuCanExecuteAction::CreateStatic(
						&CanToggleShowFlag, Descriptor.Name);
				Action.GetActionCheckState =
					FToolMenuGetActionCheckState::CreateStatic(
						&GetShowFlagState, Descriptor.Name);

				Section.AddMenuEntry(
					Descriptor.Name,
					Descriptor.Label,
					Descriptor.ToolTip,
					ResolveIcon(Descriptor),
					Action,
					EUserInterfaceActionType::ToggleButton);
			}
		}

		void AddSubMenu(FToolMenuSection& Section)
		{
			bool bHasRegisteredFlag = false;
			for (const FShowFlagDescriptor& Descriptor : GetShowFlags())
			{
				bHasRegisteredFlag |=
					FEngineShowFlags::FindIndexByName(Descriptor.Name) !=
					INDEX_NONE;
			}
			if (!bHasRegisteredFlag)
			{
				return;
			}

			FToolMenuEntry& Entry = Section.AddSubMenu(
				TEXT("SeinARTS"),
				LOCTEXT("SeinARTSLabel", "SeinARTS"),
				LOCTEXT("SeinARTSToolTip", "SeinARTS debug visualization show flags"),
				FNewToolMenuDelegate::CreateStatic(&PopulateSubMenu),
				false,
				FSlateIcon(
					FAppStyle::GetAppStyleSetName(),
					TEXT("ShowFlagsMenu.SeinExtents")));
			Entry.InsertPosition = FToolMenuInsert(
				TEXT("SFG_Advanced"), EToolMenuInsertType::After);
		}

		void ExtendShowMenu(const FName MenuName)
		{
			if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuName))
			{
				FToolMenuSection& Section = Menu->FindOrAddSection(
					TEXT("AllShowFlags"));
				Section.AddDynamicEntry(
					TEXT("SeinARTSShowFlags"),
					FNewToolMenuSectionDelegate::CreateStatic(&AddSubMenu));
			}
		}

		void RegisterMenus()
		{
			FToolMenuOwnerScoped OwnerScoped(MenuOwnerName);

			// The level viewport swaps to the PIE toolbar while a session plays
			// in it, and that toolbar owns a separate Show submenu.
			ExtendShowMenu(TEXT("LevelEditor.LevelViewportToolbar.Show"));
			ExtendShowMenu(TEXT("LevelEditor.PIEViewportToolbar.Show"));
		}
	}

	void Register()
	{
		if (!StartupCallbackHandle.IsValid())
		{
			StartupCallbackHandle = UToolMenus::RegisterStartupCallback(
				FSimpleMulticastDelegate::FDelegate::CreateStatic(
					&RegisterMenus));
		}
	}

	void Unregister()
	{
		if (StartupCallbackHandle.IsValid())
		{
			UToolMenus::UnRegisterStartupCallback(StartupCallbackHandle);
			StartupCallbackHandle.Reset();
		}
		UToolMenus::UnregisterOwner(MenuOwnerName);
	}
}

#undef LOCTEXT_NAMESPACE
