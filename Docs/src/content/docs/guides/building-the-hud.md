---
title: Build the HUD
description: Show Minerals, selected-unit health, and ability buttons using the framework's read-only UI models.
---

Complete [Add Mineral Income](/guides/adding-mineral-income/) first. This HUD displays the local balance, selection, health, and actions. It will also show production abilities when you add buildings.

## 1. Create the layout

Create `Content/Demo/Blueprints/UI`. Create a Widget Blueprint with parent **SeinUserWidget**, named `WBP_DemoHUD`.

In its Designer, add a Canvas Panel root. Set its visibility to **Not Hit-Testable (Self Only)** so unused screen space still accepts world clicks. Add:

| Widget | Name | Placement |
| --- | --- | --- |
| Text Block | MineralsText | Top-left, offset `(24, 24)` |
| Vertical Box | SelectionPanel | Bottom-left, offset from the bottom `(24, -200)` |
| Text Block inside SelectionPanel | SelectionName | First child |
| Progress Bar inside SelectionPanel | HealthBar | Second child |
| Wrap Box inside SelectionPanel | ActionButtons | Third child |

Use bottom-left anchors and alignment `(0, 1)` for SelectionPanel. Give it a width around `420` and enable Size To Content vertically. Mark the named widgets **Is Variable**. Compile and save.

## 2. Show the HUD

Open `PC_DemoSkirmishController`. Extend its existing local-controller BeginPlay path after input setup:

1. **Create Widget** of class `WBP_DemoHUD`, Owning Player = self.
2. Store the return value in a `DemoHUD` variable.
3. Call **Add to Viewport** on it.

Keep this inside the Is Local Controller branch. Each player creates their own HUD. Compile and save.

## 3. Read Minerals and selection

In `WBP_DemoHUD`, create a function named `RefreshHUD`. The following functions are supplied by its SeinUserWidget parent.

Call **Get Local Player View Model**. If valid, call its **Get Resource** with `SeinARTS.Resource.Minerals`, format the returned number without fractional digits, and set MineralsText to `Minerals: {value}` using Format Text.

Call **Get Selection Model**, then **Get Primary View Model**. If there is no valid primary model, collapse SelectionPanel and return. Otherwise show the panel and set SelectionName from the model's Display Name.

Call **Has Component** on the primary model with the generated health data struct. If true, read `Health` and `MaxHealth` through the model's **Get Base Attribute** function. Set HealthBar Percent to Health / MaxHealth, clamped to `0..1`; use zero if MaxHealth is not positive. Hide HealthBar for entities without this component.

Call RefreshHUD from the widget's **Event Tick**. This tick only reads presentation data and updates widgets. The combat and income loops continue to run exclusively in ability simulation callbacks.

## 4. Create an ability button

Create another SeinUserWidget Blueprint, `WBP_AbilityButton`. Its Designer contains a Button named `ActionButton` with a Text Block child named `ActionLabel`.

Add a Gameplay Tag variable named `AbilityTag`, marked **Instance Editable** and **Expose on Spawn**. Compile.

Create `RefreshButton`:

1. Get Selection Model and call **Get Selection Ability By Tag**, passing AbilityTag.
2. Break the returned Ability Info. Set ActionLabel from Name.
3. Set ActionButton's Is Enabled from **Is Enabled** in Ability Info.
4. Set its tooltip text from **Format Resource Cost** on Resource Cost. When disabled, append the Disabled Reason converted to text.

Call RefreshButton from Event Tick. On ActionButton **On Clicked**, call **Trigger Ability From Action Slot** with the cached **Sein Player Controller** and AbilityTag. This either submits an untargeted action or starts the appropriate world targeter.

Compile and save.

## 5. Populate the action panel

Back in `WBP_DemoHUD`, add a Gameplay Tag array variable named `DisplayedAbilityTags`.

Extend RefreshHUD after selection handling:

1. Call **Get Selection Abilities** on the selection model.
2. Build a local Gameplay Tag array from the returned entries, skipping entries whose Is Passive is true.
3. Compare this array with DisplayedAbilityTags. If equal, keep the existing buttons.
4. Otherwise, **Clear Children** on ActionButtons and replace DisplayedAbilityTags with the new array.
5. For each tag, Create Widget `WBP_AbilityButton`, with the HUD's owning player and the exposed AbilityTag. **Add Child to Wrap Box** on ActionButtons.

In the no-selection branch, clear ActionButtons and DisplayedAbilityTags as well as collapsing the panel. Rebuilding only when the tag list changes preserves button presses while availability and cooldown values refresh.

Compile and save.

## 6. Test the HUD

Run Play Standalone:

1. Check Minerals starts at `500`.
2. Select the Soldier. Its name, full health bar, and Move, Attack, and Harvest buttons appear.
3. Click Harvest, then confirm the nearby deposit with the world targeter. Each hit grants `3` Minerals, or the remaining stock if less than `3` remains.
4. Repeat the deposit test with Remaining `25`. The final balance should be `525` and the deposit should disappear.
5. Deselect everything. The selection panel should clear while Minerals remains visible.
6. Drag a selection marquee over empty HUD space. The full-screen Canvas should not consume the drag.

## Checkpoint

- A local controller creates its HUD once.
- Resource and health displays read UI models.
- Buttons reflect the selection's current grants and availability.
- Ability clicks go through Trigger Ability From Action Slot.

Continue with [Build Production Buildings](/guides/building-production-buildings/).
