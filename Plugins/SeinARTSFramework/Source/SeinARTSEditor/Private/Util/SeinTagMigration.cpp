/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTagMigration.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Migrates identity subtrees with persistent redirects and loaded reference repair.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Util/SeinAutoTagGenerator.h"
#include "Util/SeinTagAudit.h"
#include "Util/SeinTagSourcePersistence.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsSettings.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"
#include "Editor.h"
#include "StructUtils/InstancedStruct.h"
#include "Containers/Ticker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Settings/PluginSettings.h"
#include "UObject/MetaData.h"

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
	bool GFailAfterMigrationRefresh = false;
#endif
	UBlueprint* OwningBlueprint(UObject* Object)
	{
		if (auto* BP = Cast<UBlueprint>(Object)) return BP;
		if (auto* BP = Object->GetTypedOuter<UBlueprint>()) return BP;
		if (auto* Class = Object->GetTypedOuter<UClass>())
			if (auto* BP = Cast<UBlueprint>(Class->ClassGeneratedBy)) return BP;
		return Cast<UBlueprint>(Object->GetClass()->ClassGeneratedBy);
	}

	bool VerifyDictionary(const FString& Path, const FString& Section, FName Old, FName New,
		const TArray<FName>& Incoming, bool bExplicit)
	{
		FConfigFile File;
		File.Read(Path);
		TArray<FString> Definitions, Redirects;
		File.GetArray(*Section, TEXT("GameplayTagList"), Definitions);
		File.GetArray(*Section, TEXT("GameplayTagRedirects"), Redirects);
		bool bNewFound = false;
		for (const FString& Definition : Definitions)
		{
			FString Name;
			if (!FParse::Value(*Definition, TEXT("Tag="), Name)) continue;
			if (FName(*Name) == Old) return false;
			bNewFound |= FName(*Name) == New;
		}
		if (bNewFound != bExplicit) return false;
		TArray<FName> Required = Incoming;
		Required.AddUnique(Old);
		for (FName Previous : Required)
		{
			bool bFound = false;
			for (const FString& Redirect : Redirects)
			{
				FString From, To;
				if (FParse::Value(*Redirect, TEXT("OldTagName="), From)
					&& FParse::Value(*Redirect, TEXT("NewTagName="), To) && FName(*From) == Previous)
				{
					if (FName(*To) != New) return false;
					bFound = true;
				}
			}
			if (!bFound) return false;
		}
		return true;
	}

	bool ReplaceDefault(FString& Value, const FGameplayTag OldTag, const FGameplayTag NewTag, bool bApply)
	{
		const FString Old = TEXT("TagName=\"") + OldTag.ToString() + TEXT("\"");
		if (!Value.Contains(Old)) return false;
		if (bApply) Value.ReplaceInline(*Old, *(TEXT("TagName=\"") + NewTag.ToString() + TEXT("\"")));
		return true;
	}

	bool ReplaceProperty(FProperty* Property, void* Value, FGameplayTag Old, FGameplayTag New, bool bApply)
	{
		bool bChanged = false;
		if (auto* Struct = CastField<FStructProperty>(Property))
		{
			if (Struct->Struct == FInstancedStruct::StaticStruct())
			{
				auto& Entry = *static_cast<FInstancedStruct*>(Value);
				if (Entry.IsValid()) for (TFieldIterator<FProperty> It(Entry.GetScriptStruct()); It; ++It)
					for (int32 Index = 0; Index < It->ArrayDim; ++Index)
						bChanged |= ReplaceProperty(*It, It->ContainerPtrToValuePtr<void>(Entry.GetMutableMemory(), Index), Old, New, bApply);
				return bChanged;
			}
			if (Struct->Struct == FGameplayTag::StaticStruct())
			{
				auto& Tag = *static_cast<FGameplayTag*>(Value);
				if (Tag != Old) return false;
				if (bApply) Tag = New;
				return true;
			}
			if (Struct->Struct == FGameplayTagContainer::StaticStruct())
			{
				auto& Tags = *static_cast<FGameplayTagContainer*>(Value);
				if (!Tags.HasTagExact(Old)) return false;
				if (bApply) { Tags.RemoveTag(Old); Tags.AddTag(New); }
				return true;
			}
			// Includes query dictionaries: keep their indices and token stream intact.
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
				for (int32 Index = 0; Index < It->ArrayDim; ++Index)
					bChanged |= ReplaceProperty(*It, It->ContainerPtrToValuePtr<void>(Value, Index), Old, New, bApply);
		}
		else if (auto* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(Array, Value);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
				bChanged |= ReplaceProperty(Array->Inner, Helper.GetRawPtr(Index), Old, New, bApply);
		}
		else if (auto* Map = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(Map, Value);
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index) if (Helper.IsValidIndex(Index))
			{
				bChanged |= ReplaceProperty(Map->KeyProp, Helper.GetKeyPtr(Index), Old, New, bApply);
				bChanged |= ReplaceProperty(Map->ValueProp, Helper.GetValuePtr(Index), Old, New, bApply);
			}
			if (bApply && bChanged) Helper.Rehash();
		}
		else if (auto* Set = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(Set, Value);
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index) if (Helper.IsValidIndex(Index))
				bChanged |= ReplaceProperty(Set->ElementProp, Helper.GetElementPtr(Index), Old, New, bApply);
			if (bApply && bChanged) Helper.Rehash();
		}
		else if (auto* String = CastField<FStrProperty>(Property); String && Property->GetFName() == TEXT("DefaultValue"))
		{
			FString Text = String->GetPropertyValue(Value);
			bChanged = ReplaceDefault(Text, Old, New, bApply);
			if (bApply && bChanged) String->SetPropertyValue(Value, Text);
		}
		return bChanged;
	}

	bool HasCollidingKeys(FProperty* Property, void* Value, const TArray<TPair<FGameplayTag, FGameplayTag>>& Mapping)
	{
		auto Collides = [&Mapping](FProperty* Key, void* A, void* B)
		{
			void* CopyA = Key->AllocateAndInitializeValue();
			void* CopyB = Key->AllocateAndInitializeValue();
			Key->CopySingleValue(CopyA, A);
			Key->CopySingleValue(CopyB, B);
			bool bChangedA = false, bChangedB = false;
			for (const auto& Pair : Mapping)
			{
				bChangedA |= ReplaceProperty(Key, CopyA, Pair.Key, Pair.Value, true);
				bChangedB |= ReplaceProperty(Key, CopyB, Pair.Key, Pair.Value, true);
			}
			const bool bCollision = (bChangedA || bChangedB) && Key->Identical(CopyA, CopyB);
			Key->DestroyAndFreeValue(CopyA);
			Key->DestroyAndFreeValue(CopyB);
			return bCollision;
		};
		if (auto* Map = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(Map, Value);
			for (int32 A = 0; A < Helper.GetMaxIndex(); ++A) if (Helper.IsValidIndex(A))
			{
				for (int32 B = A + 1; B < Helper.GetMaxIndex(); ++B) if (Helper.IsValidIndex(B))
					if (Collides(Map->KeyProp, Helper.GetKeyPtr(A), Helper.GetKeyPtr(B))) return true;
				if (HasCollidingKeys(Map->ValueProp, Helper.GetValuePtr(A), Mapping)) return true;
			}
		}
		else if (auto* Set = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(Set, Value);
			for (int32 A = 0; A < Helper.GetMaxIndex(); ++A) if (Helper.IsValidIndex(A))
				for (int32 B = A + 1; B < Helper.GetMaxIndex(); ++B) if (Helper.IsValidIndex(B))
					if (Collides(Set->ElementProp, Helper.GetElementPtr(A), Helper.GetElementPtr(B))) return true;
		}
		else if (auto* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(Array, Value);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
				if (HasCollidingKeys(Array->Inner, Helper.GetRawPtr(Index), Mapping)) return true;
		}
		else if (auto* Struct = CastField<FStructProperty>(Property))
		{
			UScriptStruct* Type = Struct->Struct;
			if (Type == FInstancedStruct::StaticStruct())
			{
				auto& Instance = *static_cast<FInstancedStruct*>(Value);
				if (!Instance.IsValid()) return false;
				Type = const_cast<UScriptStruct*>(Instance.GetScriptStruct());
				Value = Instance.GetMutableMemory();
			}
			for (TFieldIterator<FProperty> It(Type); It; ++It)
				for (int32 Index = 0; Index < It->ArrayDim; ++Index)
					if (HasCollidingKeys(*It, It->ContainerPtrToValuePtr<void>(Value, Index), Mapping)) return true;
		}
		return false;
	}

	bool ReplaceObject(UObject* Object, FGameplayTag Old, FGameplayTag New, bool bApply)
	{
		bool bChanged = false;
		if (auto* Table = Cast<UDataTable>(Object); Table && Table->GetRowStruct())
			for (const auto& Row : Table->GetRowMap())
				for (TFieldIterator<FProperty> It(Table->GetRowStruct()); It; ++It)
					for (int32 Index = 0; Index < It->ArrayDim; ++Index)
						bChanged |= ReplaceProperty(*It, It->ContainerPtrToValuePtr<void>(Row.Value, Index), Old, New, bApply);
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
				bChanged |= ReplaceProperty(*It, It->ContainerPtrToValuePtr<void>(Object, Index), Old, New, bApply);
		if (auto* Node = Cast<UEdGraphNode>(Object))
			for (UEdGraphPin* Pin : Node->Pins) if (Pin)
				bChanged |= ReplaceDefault(Pin->DefaultValue, Old, New, bApply);
		return bChanged;
	}
	TSet<TWeakObjectPtr<UBlueprint>> PendingBlueprints;
	TArray<TPair<FGameplayTag, FGameplayTag>> SessionRedirects;
	FDelegateHandle UndoHandle;
	FTSTicker::FDelegateHandle DeferredHandle;
	bool bRepairUndo = false;

	void CompileBlueprints(const TSet<TWeakObjectPtr<UBlueprint>>& Blueprints, TArray<FString>& Failures)
	{
		auto Ordered = Blueprints.Array();
		Ordered.Sort([](const auto& A, const auto& B)
		{
			auto Depth = [](UBlueprint* BP) { int32 N = 0; for (UClass* C = BP ? BP->GeneratedClass.Get() : nullptr; C; C = C->GetSuperClass()) ++N; return N; };
			const int32 AD = Depth(A.Get()), BD = Depth(B.Get());
			return AD != BD ? AD < BD : A.IsValid() && B.IsValid() && A->GetPathName() < B->GetPathName();
		});
		for (const auto& Weak : Ordered) if (auto* BP = Weak.Get())
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
			if (BP->Status == BS_Error) Failures.Add(BP->GetPathName());
		}
	}

	bool FlushDeferred(float)
	{
		if (GIsTransacting || (GEditor && (GEditor->IsTransactionActive() || GEditor->PlayWorld))) return true;
		DeferredHandle.Reset();
		if (bRepairUndo)
		{
			bRepairUndo = false;
			// Transaction archives do not apply gameplay-tag redirects. Repair restored values
			// without starting another transaction or discarding unrelated undo history.
			for (TObjectIterator<UObject> It(RF_NoFlags); It; ++It)
			{
				if (!IsValid(*It) || It->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) continue;
				bool bChanged = false;
				for (const auto& Pair : SessionRedirects)
				{
					const auto Current = UGameplayTagsManager::Get().RequestGameplayTag(Pair.Value.GetTagName(), false);
					if (Current.IsValid() && Current != Pair.Key) bChanged |= ReplaceObject(*It, Pair.Key, Current, true);
				}
				if (bChanged)
				{
					It->MarkPackageDirty();
					if (auto* BP = OwningBlueprint(*It)) PendingBlueprints.Add(BP);
					if (auto* Table = Cast<UDataTable>(*It)) Table->HandleDataTableChanged();
				}
			}
		}
		auto Work = MoveTemp(PendingBlueprints);
		PendingBlueprints.Reset();
		TArray<FString> Failures;
		CompileBlueprints(Work, Failures);
		for (const FString& Failure : Failures) UE_LOG(LogTemp, Warning, TEXT("Tag migration: resolve Blueprint compilation errors in %s."), *Failure);
		return false;
	}

	void ScheduleDeferred()
	{
		if (!DeferredHandle.IsValid()) DeferredHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FlushDeferred));
		if (!UndoHandle.IsValid()) UndoHandle = FEditorDelegates::PostUndoRedo.AddLambda([]()
		{
			bRepairUndo = true;
			ScheduleDeferred();
		});
	}
}

static FSeinAutoTagRegenerationResult MigrateIdentity(UBlueprint* Blueprint, FName NewName,
	FGameplayTag Previous = FGameplayTag(), bool bAutomatic = false, bool bAllowExisting = false)
{
	using namespace SeinAutoTag;
	FSeinAutoTagRegenerationResult Result;
	FGameplayTag Current;
	bool bGenerated = false;
	if (!ReadAssetIdentity(Blueprint, Current, bGenerated)) return Result;
	const FGameplayTag Old = Previous.IsValid() ? Previous : Current;
	if (!Old.IsValid()) return Result;
	auto Reject = [&Result](const FString& Message)
	{
		Result.Outcome = ESeinAutoTagRegenerationOutcome::MigrationRequired;
		Result.Detail = FText::FromString(Message);
		return Result;
	};
	if (bAutomatic)
	{
		if (GEditor && GEditor->PlayWorld) return Reject(TEXT("Stop Play in Editor before renaming generated identities."));
		if (FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().IsLoadingAssets())
			return Reject(TEXT("Wait for asset discovery before renaming generated identities."));
	}
	else
	{
		const FString Blocker = SeinTagAudit::MutationBlocker();
		if (!Blocker.IsEmpty()) return Reject(Blocker);
	}
	auto& Manager = UGameplayTagsManager::Get();
	if (NewName == Old.GetTagName())
	{
		Result.DerivedTag = Old;
		Result.Outcome = ESeinAutoTagRegenerationOutcome::AlreadyCurrent;
		return Result;
	}
	FText NameError;
	if (NewName.IsNone() || !Manager.IsValidGameplayTagString(NewName.ToString(), &NameError))
		return Reject(TEXT("Enter a valid nonempty gameplay tag name. ") + NameError.ToString());
	const bool bReclaimAlias = bAutomatic && Manager.RequestGameplayTag(NewName, false) == Old;
	if ((!bAllowExisting && !bReclaimAlias && Manager.RequestGameplayTag(NewName, false).IsValid())
		|| (!bReclaimAlias && SeinTagAudit::IsRedirectEndpoint(NewName)))
		return Reject(TEXT("The destination exists or is already redirected. Choose a new name; tags are never merged automatically."));
	if (NewName.ToString().StartsWith(Old.ToString() + TEXT("."), ESearchCase::IgnoreCase))
		return Reject(TEXT("Choose a destination outside the existing subtree."));
	const FString Collision = FindCollidingAssetPath(Old, Blueprint->GetOutermost()->GetName());
	if (!Collision.IsEmpty()) return Reject(TEXT("The current identity is shared with ") + Collision + TEXT(". Give the copied asset a distinct identity instead of redirecting the shared tag."));
	FString Comment;
	TArray<FName> Sources;
	bool bExplicit = false, bRestricted = false, bAllowChildren = false;
	Manager.GetTagEditorData(Old.GetTagName(), Comment, Sources, bExplicit, bRestricted, bAllowChildren);
	if (!bExplicit || bRestricted || Sources.Num() != 1)
		return Reject(TEXT("Only an explicit tag with one editable INI source can be migrated."));
	if (bAutomatic && !Comment.StartsWith(TEXT("Auto-generated by SeinARTS")))
		return Reject(TEXT("This tag has no generated provenance; use an explicit tag migration for manually defined identities."));
	const auto* Source = Manager.FindTagSource(Sources[0]);
	if (!Source || !Source->SourceTagList || Source->SourceTagList->ConfigFileName.IsEmpty())
		return Reject(TEXT("Native and table-owned tags must be changed in their defining source."));
	UGameplayTagsList* SourceList = Source->SourceTagList;
	const FString ConfigPath = SourceList->ConfigFileName;
	if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(ConfigPath), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())))
		return Reject(TEXT("The tag source is outside this project; change it in its owning plugin/project."));
	FString BeforeConfig;
	if (!FFileHelper::LoadFileToString(BeforeConfig, *ConfigPath)
		|| IFileManager::Get().IsReadOnly(*ConfigPath)) return Reject(TEXT("The tag source could not be read or is not writable."));
	if (!SeinTagSourcePersistence::MatchesDisk(ConfigPath, SourceList))
		return Reject(TEXT("The tag source changed outside this editor or contains unsaved dictionary edits. Refresh the Gameplay Tags source and retry; no tags were changed."));
	struct FTagMove
	{
		FGameplayTag Old;
		FName NewName;
		FGameplayTag New;
		bool bExplicit = false;
		TArray<FName> Incoming;
	};
	TArray<FTagMove> Moves;
	FGameplayTagContainer Subtree = Manager.RequestGameplayTagChildren(Old);
	Subtree.AddTag(Old);
	for (FGameplayTag Tag : Subtree)
	{
		FTagMove Move;
		Move.Old = Tag;
		Move.NewName = FName(*(NewName.ToString() + Tag.ToString().Mid(Old.ToString().Len())));
		FString ChildComment;
		TArray<FName> ChildSources;
		bool bChildRestricted = false, bChildAllowsChildren = false;
		Manager.GetTagEditorData(Tag.GetTagName(), ChildComment, ChildSources,
			Move.bExplicit, bChildRestricted, bChildAllowsChildren);
		if (bChildRestricted || (Move.bExplicit && (ChildSources.Num() != 1 || ChildSources[0] != Sources[0])))
			return Reject(TEXT("The entire hierarchy must have one editable INI source. Descendant ")
				+ Tag.ToString() + TEXT(" has another, shared, native or restricted source; no tags were changed."));
		const auto Existing = Manager.RequestGameplayTag(Move.NewName, false);
		const bool bReturning = bReclaimAlias && Existing == Move.Old;
		if (Existing.IsValid() && bAllowExisting && !bReturning)
		{
			FString ExistingComment;
			TArray<FName> ExistingSources;
			bool bExistingExplicit = false, bExistingRestricted = false, bExistingChildren = false;
			Manager.GetTagEditorData(Move.NewName, ExistingComment, ExistingSources, bExistingExplicit, bExistingRestricted, bExistingChildren);
			if (!Move.bExplicit || !bExistingExplicit || bExistingRestricted || ExistingSources != Sources
				|| !ExistingComment.StartsWith(TEXT("Auto-generated by SeinARTS"))
				|| !FindCollidingAssetPath(Existing, Blueprint->GetOutermost()->GetName()).IsEmpty())
				return Reject(TEXT("The existing destination is not exclusively generated for this asset; no tags were changed."));
		}
		if (!Manager.IsValidGameplayTagString(Move.NewName.ToString())
			|| (!bAllowExisting && !bReturning && Existing.IsValid()) || (!bReturning && SeinTagAudit::IsRedirectEndpoint(Move.NewName)))
			return Reject(TEXT("A destination in the renamed hierarchy is invalid, already defined or redirected: ") + Move.NewName.ToString());
		Moves.Add(MoveTemp(Move));
	}
	Moves.Sort([](const FTagMove& A, const FTagMove& B) { return A.Old.ToString() < B.Old.ToString(); });
	// Flatten previous aliases, including aliases of implicit intermediate nodes. Cross-source
	// aliases are blocked before writing: UE only resolves redirect chains within each source.
	TArray<const FGameplayTagSource*> RedirectSources;
	Manager.FindTagSourcesWithType(EGameplayTagSourceType::TagList, RedirectSources);
	TArray<const UGameplayTagsList*> Lists{GetDefault<UGameplayTagsSettings>()};
	for (const auto* RedirectSource : RedirectSources) Lists.AddUnique(RedirectSource->SourceTagList);
	for (const auto* List : Lists) if (List)
		for (const FGameplayTagRedirect& Redirect : List->GameplayTagRedirects)
			for (FTagMove& Move : Moves)
				if (Redirect.NewTagName == Move.Old.GetTagName()
					|| Manager.RequestGameplayTag(Redirect.OldTagName, false) == Move.Old)
				{
					if (List != SourceList)
						return Reject(TEXT("An older alias points into this hierarchy from another tag source. Consolidate those redirects into the identity's source before renaming; no tags were changed."));
					if (Redirect.OldTagName != Move.NewName) Move.Incoming.AddUnique(Redirect.OldTagName);
				}
	bool bDictionaryRefreshed = false;
	auto RestoreSource = [&]() -> FString
	{
		bool bCanRefresh = true;
		if (bDictionaryRefreshed)
		{
			// Clear this source's migrated aliases through a real tree rebuild before restoring
			// their previous destinations; otherwise UE compares them against its stale cache.
			SourceList->GameplayTagRedirects.Reset();
			bCanRefresh = SourceList->TryUpdateDefaultConfigFile(ConfigPath)
				&& SeinTagSourcePersistence::MatchesDisk(ConfigPath, SourceList);
			if (bCanRefresh)
			{
				GConfig->LoadFile(ConfigPath);
				Manager.EditorRefreshGameplayTagTree();
			}
		}
		const bool bSaved = FFileHelper::SaveStringToFile(BeforeConfig, *ConfigPath);
		FString Restored;
		const bool bVerified = bSaved && FFileHelper::LoadFileToString(Restored, *ConfigPath) && Restored == BeforeConfig;
		GConfig->LoadFile(ConfigPath);
		SourceList->ReloadConfig(nullptr, *ConfigPath);
		if (!bVerified) return TEXT(" Source restoration failed. Restore the tag INI from source control and restart the editor before continuing.");
		if (!bCanRefresh) return TEXT(" The original INI was restored, but the editor must restart to restore its redirect cache.");
		Manager.EditorRefreshGameplayTagTree();
		return TEXT(" The original source and redirect lookups were restored.");
	};
	// Recovery merges two previously valid names. Inspect saved consumers before aliases
	// can collapse distinct map keys during their next load.
	if (bAllowExisting)
	{
		auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TSet<FName> Packages;
		for (const auto& Move : Moves)
			for (FName Name : {Move.Old.GetTagName(), Move.NewName})
			{
				TArray<FAssetIdentifier> References;
				Registry.GetReferencers(FAssetIdentifier(FGameplayTag::StaticStruct(), Name), References,
					UE::AssetRegistry::EDependencyCategory::SearchableName);
				for (const auto& Reference : References) if (!Reference.PackageName.IsNone()) Packages.Add(Reference.PackageName);
			}
		for (FName Package : Packages)
			if (!LoadPackage(nullptr, *Package.ToString(), LOAD_None))
				return Reject(TEXT("Could not inspect saved tag references in ") + Package.ToString() + TEXT("; no tags were changed."));
	}

	// Collect loaded consumers before the manager redirects future lookups. Existing FGameplayTag
	// values remain old until explicitly replaced; saving a package alone does not repair them.
	TArray<TWeakObjectPtr<UObject>> Consumers;
	// Identity fields live on class defaults, which TObjectIterator excludes unless requested.
	for (TObjectIterator<UObject> It(RF_NoFlags); It; ++It)
		if (IsValid(*It) && !It->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
			for (const FTagMove& Move : Moves)
				if (ReplaceObject(*It, Move.Old, Move.Old, false)) { Consumers.Add(*It); break; }
	if (bAllowExisting)
	{
		TArray<TPair<FGameplayTag, FGameplayTag>> Mapping;
		for (const FTagMove& Move : Moves)
		{
			const auto Existing = Manager.RequestGameplayTag(Move.NewName, false);
			if (Existing.IsValid()) Mapping.Emplace(Move.Old, Existing);
		}
		for (const auto& Weak : Consumers) if (UObject* Object = Weak.Get())
		{
			auto Conflicts = [&Mapping](const UStruct* Type, void* Value)
			{
				for (TFieldIterator<FProperty> It(Type); It; ++It)
					for (int32 Index = 0; Index < It->ArrayDim; ++Index)
						if (HasCollidingKeys(*It, It->ContainerPtrToValuePtr<void>(Value, Index), Mapping)) return true;
				return false;
			};
			bool bConflict = Conflicts(Object->GetClass(), Object);
			if (auto* Table = Cast<UDataTable>(Object); Table && Table->GetRowStruct())
				for (const auto& Row : Table->GetRowMap()) bConflict |= Conflicts(Table->GetRowStruct(), Row.Value);
			if (bConflict) return Reject(TEXT("Both tag names are used as distinct map keys or set entries in ")
				+ Object->GetPathName() + TEXT(". Resolve that conflict before repair; no tags were changed."));
		}
	}

	FString CurrentConfig;
	if (!FFileHelper::LoadFileToString(CurrentConfig, *ConfigPath) || CurrentConfig != BeforeConfig
		|| !SeinTagSourcePersistence::MatchesDisk(ConfigPath, SourceList))
		return Reject(TEXT("The tag source changed during migration planning. Refresh and retry; no tags were changed."));
	if (bReclaimAlias)
	{
		const auto RedirectsBefore = SourceList->GameplayTagRedirects;
		SourceList->GameplayTagRedirects.Reset();
		if (!SourceList->TryUpdateDefaultConfigFile(ConfigPath) || !SeinTagSourcePersistence::MatchesDisk(ConfigPath, SourceList))
			return Reject(FString(TEXT("Could not reclaim the previous tag name.")) + RestoreSource());
		GConfig->LoadFile(ConfigPath);
		Manager.EditorRefreshGameplayTagTree();
		bDictionaryRefreshed = true;
		SourceList->GameplayTagRedirects = RedirectsBefore;
		SourceList->GameplayTagRedirects.RemoveAll([&Moves](const auto& Redirect)
		{
			return Moves.ContainsByPredicate([&Redirect](const auto& Move) { return Move.NewName == Redirect.OldTagName; });
		});
		for (auto& Redirect : SourceList->GameplayTagRedirects)
			for (const auto& Move : Moves)
				if (Move.Incoming.Contains(Redirect.OldTagName)) { Redirect.NewTagName = Move.NewName; break; }
	}
	// Apply the complete preflighted subtree in memory, then persist its one source once.
	// No partially renamed hierarchy is exposed by per-tag native editor writes.
	for (FGameplayTagTableRow& Entry : SourceList->GameplayTagList)
		for (const FTagMove& Move : Moves)
			if (Entry.Tag == Move.Old.GetTagName()) { Entry.Tag = Move.NewName; break; }
	if (bAllowExisting)
	{
		TSet<FName> Seen;
		SourceList->GameplayTagList.RemoveAll([&Seen, &Moves](const auto& Row)
		{
			if (!Moves.ContainsByPredicate([&Row](const auto& Move) { return Move.NewName == Row.Tag; })) return false;
			if (Seen.Contains(Row.Tag)) return true;
			Seen.Add(Row.Tag);
			return false;
		});
	}
	for (const FTagMove& Move : Moves)
	{
		FGameplayTagRedirect Redirect;
		Redirect.OldTagName = Move.Old.GetTagName();
		Redirect.NewTagName = Move.NewName;
		SourceList->GameplayTagRedirects.AddUnique(Redirect);
	}
	const bool bSaved = SourceList->TryUpdateDefaultConfigFile(ConfigPath);
	const bool bCompleteSourcePersisted = bSaved && SeinTagSourcePersistence::MatchesDisk(ConfigPath, SourceList);
	GConfig->LoadFile(ConfigPath);
	Manager.EditorRefreshGameplayTagTree();
	bDictionaryRefreshed = true;
#if WITH_DEV_AUTOMATION_TESTS
	if (GFailAfterMigrationRefresh)
		return Reject(FString(TEXT("Injected migration failure.")) + RestoreSource());
#endif
	if (!bCompleteSourcePersisted)
		return Reject(FString(TEXT("The complete tag source could not be verified on disk.")) + RestoreSource());
	// UE reloads source redirects before clearing its old redirect cache. Retargeting an
	// existing alias in that first write triggers a conflicting-redirect ensure. Let the new
	// same-source hop resolve first, then flatten aliases to the now-current destination.
	bool bFlattened = false;
	for (FGameplayTagRedirect& Redirect : SourceList->GameplayTagRedirects)
		for (const FTagMove& Move : Moves)
			if (Move.Incoming.Contains(Redirect.OldTagName) && Redirect.NewTagName != Move.NewName)
			{
				Redirect.NewTagName = Move.NewName;
				bFlattened = true;
				break;
			}
	if (bFlattened)
	{
		const bool bAliasesSaved = SourceList->TryUpdateDefaultConfigFile(ConfigPath)
			&& SeinTagSourcePersistence::MatchesDisk(ConfigPath, SourceList);
		if (!bAliasesSaved)
			return Reject(FString(TEXT("Compatibility aliases could not be verified on disk.")) + RestoreSource());
		GConfig->LoadFile(ConfigPath);
		Manager.EditorRefreshGameplayTagTree();
	}
	for (FTagMove& Move : Moves)
	{
		Move.New = Manager.RequestGameplayTag(Move.NewName, false);
		if (!bSaved || !Move.New.IsValid() || Move.New.GetTagName() != Move.NewName
			|| !VerifyDictionary(ConfigPath, SourceList->GetClass()->GetPathName(), Move.Old.GetTagName(),
				Move.NewName, Move.Incoming, Move.bExplicit))
			return Reject(FString(TEXT("The hierarchy migration could not be verified on disk.")) + RestoreSource());
	}
	const FGameplayTag New = Manager.RequestGameplayTag(NewName, false);
	// Repair live values before native asset rename saves the owner. Automatic migration
	// repairs transaction snapshots after Undo; explicit migration clears history below.
	TSet<TWeakObjectPtr<UBlueprint>> Blueprints;
	for (auto& Weak : Consumers) if (UObject* Object = Weak.Get())
	{
		Object->Modify();
		for (const FTagMove& Move : Moves) ReplaceObject(Object, Move.Old, Move.New, true);
		Object->MarkPackageDirty();
		if (UBlueprint* BP = OwningBlueprint(Object)) Blueprints.Add(BP);
		if (auto* Table = Cast<UDataTable>(Object)) Table->HandleDataTableChanged();
	}
	TArray<FString> CompileFailures;
	if (bAutomatic)
	{
		for (const auto& Weak : Blueprints) PendingBlueprints.Add(Weak);
		for (const auto& Move : Moves) SessionRedirects.Emplace(Move.Old, Move.New);
		ScheduleDeferred();
	}
	else
	{
		CompileBlueprints(Blueprints, CompileFailures);
		if (GEditor) GEditor->ResetTransaction(FText::FromString(TEXT("Gameplay tag migration")));
	}
	if (bGenerated || bAutomatic) RememberGeneratedIdentity(Blueprint, New);
	Result.DerivedTag = New;
	Result.Outcome = ESeinAutoTagRegenerationOutcome::Updated;
	Result.Detail = FText::FromString(FString::Printf(TEXT("Renamed %s to %s, including its full hierarchy (%d tags), and updated %d loaded objects. Save the changed assets. A redirect preserves older serialized references; runtime replay/save compatibility still follows the project's content version policy."), *Old.ToString(), *New.ToString(), Moves.Num(), Consumers.Num()));
	if (!CompileFailures.IsEmpty())
		Result.Detail = FText::FromString(Result.Detail.ToString() + TEXT(" Compilation errors remain in: ")
			+ FString::Join(CompileFailures, TEXT(", ")) + TEXT(". Resolve them before using the changed assets."));
	return Result;
}

FSeinAutoTagRegenerationResult SeinAutoTag::RenameAssetTag(UBlueprint* Blueprint, FName NewName)
{
	return MigrateIdentity(Blueprint, NewName);
}

FSeinAutoTagRegenerationResult SeinAutoTag::ReconcileGeneratedIdentity(UBlueprint* Blueprint, FName PreviousTag)
{
	FSeinAutoTagRegenerationResult Result;
	FGameplayTag Current;
	bool bGenerated = false;
	if (!ReadAssetIdentity(Blueprint, Current, bGenerated)) return Result;
	const FName Proposed = ProposeTagName(Blueprint->GetFName(), GetDefault<USeinARTSCoreSettings>());
	const auto Previous = UGameplayTagsManager::Get().RequestGameplayTag(PreviousTag, false);
	if (Previous.IsValid() && Previous.GetTagName() != Proposed)
	{
		Result = MigrateIdentity(Blueprint, Proposed, Previous, true, Current.IsValid() && Current.GetTagName() == Proposed);
		if (Result.WasUpdated() && (!Current.IsValid() || !bGenerated)) return InitializeNewAssetTag(Blueprint);
		return Result;
	}
	return InitializeNewAssetTag(Blueprint);
}

void SeinAutoTag::RememberGeneratedIdentity(UBlueprint* Blueprint, FGameplayTag Tag)
{
	if (Blueprint && Tag.IsValid()) Blueprint->GetOutermost()->GetMetaData().SetValue(Blueprint, TEXT("Sein.LastGeneratedIdentity"), *Tag.ToString());
}

FName SeinAutoTag::LastGeneratedIdentity(const UBlueprint* Blueprint)
{
	return Blueprint ? FName(*Blueprint->GetOutermost()->GetMetaData().GetValue(Blueprint, TEXT("Sein.LastGeneratedIdentity"))) : NAME_None;
}

void SeinAutoTag::ShutdownMigrationSupport()
{
	if (DeferredHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(DeferredHandle);
	DeferredHandle.Reset();
	FEditorDelegates::PostUndoRedo.Remove(UndoHandle);
	UndoHandle.Reset();
	PendingBlueprints.Reset();
	SessionRedirects.Reset();
}

void SeinAutoTag::FinishPendingTagMigrations()
{
	if (DeferredHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(DeferredHandle);
	DeferredHandle.Reset();
	if (FlushDeferred(0)) ScheduleDeferred();
}

#if WITH_DEV_AUTOMATION_TESTS
FSeinAutoTagRegenerationResult SeinAutoTag::RenameAssetTagWithRefreshFailureForAutomation(UBlueprint* Blueprint, FName NewName)
{
	TGuardValue<bool> Guard(GFailAfterMigrationRefresh, true);
	return RenameAssetTag(Blueprint, NewName);
}
#endif
