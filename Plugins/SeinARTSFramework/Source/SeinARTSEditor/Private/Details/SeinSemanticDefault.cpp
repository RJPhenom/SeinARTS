/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinSemanticDefault.cpp
 * @author       RJ Macklem
 * @created      4 Sep 2026
 * @latest       4 Sep 2026
 * @brief        Implements semantic reset-to-default behavior for dynamically
 *               constructed SeinARTS authoring values.
 *
 *               Unreal compares array-element fields against an element at
 *               the same index in the class default object. When the default
 *               array is empty, every field is therefore always marked as an
 *               override and the stock reset path falls back to zero. These
 *               helpers instead construct the element's real C++ default.
 *               Synthetic FInstancedStruct children receive the same treatment
 *               using the currently selected script-struct type.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Details/SeinSemanticDefault.h"

#include "Algo/Reverse.h"
#include "PropertyHandle.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace SeinSemanticDefault
{
	namespace Private
	{
		class FOwnedPropertyValue
		{
		public:
			FOwnedPropertyValue() = default;

			FOwnedPropertyValue(FProperty* InProperty, const void* Source)
				: Property(InProperty)
				, Value(InProperty ? InProperty->AllocateAndInitializeValue() : nullptr)
			{
				if (Value && Source)
				{
					Property->CopyCompleteValue(Value, Source);
				}
			}

			~FOwnedPropertyValue()
			{
				Reset();
			}

			FOwnedPropertyValue(const FOwnedPropertyValue&) = delete;
			FOwnedPropertyValue& operator=(const FOwnedPropertyValue&) = delete;

			FOwnedPropertyValue(FOwnedPropertyValue&& Other) noexcept
				: Property(Other.Property)
				, Value(Other.Value)
			{
				Other.Property = nullptr;
				Other.Value = nullptr;
			}

			FOwnedPropertyValue& operator=(FOwnedPropertyValue&& Other) noexcept
			{
				if (this != &Other)
				{
					Reset();
					Property = Other.Property;
					Value = Other.Value;
					Other.Property = nullptr;
					Other.Value = nullptr;
				}
				return *this;
			}

			const void* Get() const { return Value; }

		private:
			void Reset()
			{
				if (Property && Value)
				{
					Property->DestroyValue(Value);
					FMemory::Free(Value);
				}
				Property = nullptr;
				Value = nullptr;
			}

			FProperty* Property = nullptr;
			void* Value = nullptr;
		};

		struct FResolvedDefault
		{
			TArray<FOwnedPropertyValue> PerObjectValues;
			bool bAllValuesMatch = true;
		};

		static bool IsInstancedStructProperty(const FProperty* Property)
		{
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			return StructProperty
				&& StructProperty->Struct == FInstancedStruct::StaticStruct();
		}

		/**
		 * Find the nearest dynamic-array element containing Target and collect
		 * the reflected path from that element to Target in the same walk.
		 * GetParentHandle may return a fresh wrapper for an existing property
		 * node, so handle identity is deliberately never used as the boundary.
		 */
		static TSharedPtr<IPropertyHandle> FindArrayElement(
			const TSharedPtr<IPropertyHandle>& Target,
			TArray<FProperty*>& OutPath)
		{
			OutPath.Reset();
			for (TSharedPtr<IPropertyHandle> Cursor = Target;
				Cursor.IsValid(); Cursor = Cursor->GetParentHandle())
			{
				const TSharedPtr<IPropertyHandle> Parent = Cursor->GetParentHandle();
				if (!Parent.IsValid())
				{
					break;
				}

				if (CastField<FArrayProperty>(Parent->GetProperty())
					&& Cursor->GetIndexInArray() != INDEX_NONE)
				{
					Algo::Reverse(OutPath);
					return Cursor;
				}

				if (FProperty* Property = Cursor->GetProperty())
				{
					OutPath.Add(Property);
				}
			}

			OutPath.Reset();
			return nullptr;
		}

		/**
		 * Collect reflected fields from Ancestor's value to Target's value.
		 * Synthetic structure nodes do not expose a property handle, so a missing
		 * parent is acceptable when StopAtInstancedStruct is supplied: the fields
		 * collected so far still form the complete path inside the selected type.
		 */
		static bool BuildPropertyPath(
			const TSharedPtr<IPropertyHandle>& Target,
			const TSharedPtr<IPropertyHandle>& Ancestor,
			const bool bStopAtInstancedStruct,
			TArray<FProperty*>& OutPath)
		{
			OutPath.Reset();
			TSharedPtr<IPropertyHandle> Cursor = Target;

			while (Cursor.IsValid() && Cursor != Ancestor)
			{
				FProperty* Property = Cursor->GetProperty();
				if (bStopAtInstancedStruct && IsInstancedStructProperty(Property))
				{
					break;
				}
				if (Property)
				{
					OutPath.Add(Property);
				}
				Cursor = Cursor->GetParentHandle();
			}

			if (Ancestor.IsValid() && !bStopAtInstancedStruct && Cursor != Ancestor)
			{
				return false;
			}

			Algo::Reverse(OutPath);
			return true;
		}

		/** Follow Path from RootValue and return the final property's value. */
		static const void* ResolveValuePath(
			const void* RootValue,
			const UStruct* RootType,
			const TArray<FProperty*>& Path)
		{
			const void* Value = RootValue;
			const UStruct* ContainerType = RootType;

			for (int32 Index = 0; Index < Path.Num(); ++Index)
			{
				const FProperty* Property = Path[Index];
				const UStruct* Owner = Property ? Property->GetOwnerStruct() : nullptr;
				if (!Value || !Property || !ContainerType || !Owner
					|| !ContainerType->IsChildOf(Owner))
				{
					return nullptr;
				}

				Value = Property->ContainerPtrToValuePtr<const void>(Value);
				if (Index + 1 < Path.Num())
				{
					const FStructProperty* StructProperty =
						CastField<FStructProperty>(Property);
					if (!StructProperty)
					{
						return nullptr;
					}
					ContainerType = StructProperty->Struct;
				}
			}

			return Value;
		}

		static bool FinishResolvedDefault(
			const TSharedPtr<IPropertyHandle>& Target,
			const TArray<const void*>& CurrentValues,
			const TArray<const void*>& DefaultValues,
			const bool bCaptureValues,
			FResolvedDefault& Out)
		{
			FProperty* TargetProperty = Target.IsValid()
				? Target->GetProperty() : nullptr;
			const int32 ValueCount = CurrentValues.Num();
			if (!TargetProperty || ValueCount <= 0
				|| DefaultValues.Num() != ValueCount)
			{
				return false;
			}

			Out.PerObjectValues.Reset();
			if (bCaptureValues)
			{
				Out.PerObjectValues.Reserve(ValueCount);
			}
			Out.bAllValuesMatch = true;
			for (int32 Index = 0; Index < ValueCount; ++Index)
			{
				const void* Current = CurrentValues[Index];
				const void* Default = DefaultValues[Index];
				if (!Current || !Default)
				{
					return false;
				}

				Out.bAllValuesMatch &= TargetProperty->Identical(Current, Default);
				if (bCaptureValues)
				{
					Out.PerObjectValues.Emplace(TargetProperty, Default);
				}
			}

			return true;
		}

		static bool ResolveArrayElementDefault(
			const TSharedPtr<IPropertyHandle>& Target,
			const TSharedPtr<IPropertyHandle>& Element,
			const TArray<FProperty*>& Path,
			const bool bCaptureValues,
			FResolvedDefault& Out)
		{
			FProperty* ElementProperty = Element.IsValid()
				? Element->GetProperty() : nullptr;
			if (!ElementProperty)
			{
				return false;
			}

			FDefaultConstructedPropertyElement Constructed(ElementProperty);
			const void* DefaultValue = Constructed.GetObjAddress();
			if (!Path.IsEmpty())
			{
				const FStructProperty* ElementStruct =
					CastField<FStructProperty>(ElementProperty);
				if (!ElementStruct)
				{
					return false;
				}
				DefaultValue = ResolveValuePath(
					DefaultValue, ElementStruct->Struct, Path);
			}

			TArray<const void*> CurrentValues;
			Target->AccessRawData(CurrentValues);
			TArray<const void*> DefaultValues;
			DefaultValues.Init(DefaultValue, CurrentValues.Num());
			return FinishResolvedDefault(
				Target, CurrentValues, DefaultValues,
				bCaptureValues, Out);
		}

		static bool ResolveInstancedStructDefault(
			const TSharedPtr<IPropertyHandle>& Target,
			const TSharedPtr<IPropertyHandle>& InstancedRoot,
			const bool bCaptureValues,
			FResolvedDefault& Out)
		{
			if (!Target.IsValid() || !InstancedRoot.IsValid()
				|| !IsInstancedStructProperty(InstancedRoot->GetProperty()))
			{
				return false;
			}

			TArray<const void*> CurrentValues;
			Target->AccessRawData(CurrentValues);
			TArray<const void*> RootValues;
			InstancedRoot->AccessRawData(RootValues);
			if (CurrentValues.Num() == 0 || CurrentValues.Num() != RootValues.Num())
			{
				return false;
			}

			TArray<FProperty*> Path;
			if (Target != InstancedRoot
				&& !BuildPropertyPath(Target, InstancedRoot, true, Path))
			{
				return false;
			}

			TArray<FStructOnScope> ConstructedStructs;
			TArray<FInstancedStruct> ConstructedRoots;
			TArray<const void*> DefaultValues;
			DefaultValues.Reserve(RootValues.Num());

			if (Target == InstancedRoot)
			{
				ConstructedRoots.Reserve(RootValues.Num());
			}
			else
			{
				ConstructedStructs.Reserve(RootValues.Num());
			}

			for (const void* RootValue : RootValues)
			{
				const FInstancedStruct* Current =
					static_cast<const FInstancedStruct*>(RootValue);
				const UScriptStruct* SelectedType = Current
					? Current->GetScriptStruct() : nullptr;

				if (Target == InstancedRoot)
				{
					FInstancedStruct& DefaultRoot = ConstructedRoots.AddDefaulted_GetRef();
					if (SelectedType)
					{
						DefaultRoot.InitializeAs(SelectedType);
					}
					DefaultValues.Add(&DefaultRoot);
					continue;
				}

				if (!SelectedType || Path.IsEmpty())
				{
					return false;
				}

				FStructOnScope& DefaultStruct =
					ConstructedStructs.Emplace_GetRef(SelectedType);
				const void* DefaultValue = ResolveValuePath(
					DefaultStruct.GetStructMemory(), SelectedType, Path);
				if (!DefaultValue)
				{
					return false;
				}
				DefaultValues.Add(DefaultValue);
			}

			return FinishResolvedDefault(
				Target, CurrentValues, DefaultValues,
				bCaptureValues, Out);
		}

		static bool Resolve(
			const TSharedPtr<IPropertyHandle>& Target,
			const TWeakPtr<IPropertyHandle>& InstancedRoot,
			const bool bCaptureValues,
			FResolvedDefault& Out)
		{
			if (!Target.IsValid() || !Target->IsValidHandle()
				|| !Target->GetProperty())
			{
				return false;
			}

			TArray<FProperty*> ArrayPath;
			if (const TSharedPtr<IPropertyHandle> Element =
				FindArrayElement(Target, ArrayPath))
			{
				return ResolveArrayElementDefault(
					Target, Element, ArrayPath,
					bCaptureValues, Out);
			}

			if (IsInstancedStructProperty(Target->GetProperty()))
			{
				return ResolveInstancedStructDefault(
					Target, Target, bCaptureValues, Out);
			}

			if (const TSharedPtr<IPropertyHandle> Root = InstancedRoot.Pin())
			{
				return ResolveInstancedStructDefault(
					Target, Root, bCaptureValues, Out);
			}

			return false;
		}

		static bool IsResetVisible(
			TSharedPtr<IPropertyHandle> Handle,
			const TWeakPtr<IPropertyHandle> InstancedRoot)
		{
			if (!Handle.IsValid() || Handle->IsEditConst())
			{
				return false;
			}

			FResolvedDefault Default;
			if (Resolve(Handle, InstancedRoot,
				/*bCaptureValues=*/false, Default))
			{
				return !Default.bAllValuesMatch;
			}

			return Handle->CanResetToDefault();
		}

		static void Reset(
			TSharedPtr<IPropertyHandle> Handle,
			const TWeakPtr<IPropertyHandle> InstancedRoot)
		{
			if (!Handle.IsValid() || Handle->IsEditConst())
			{
				return;
			}

			FResolvedDefault Default;
			if (Resolve(Handle, InstancedRoot,
				/*bCaptureValues=*/true, Default))
			{
				FProperty* Property = Handle->GetProperty();
				TArray<void*> CurrentValues;
				Handle->AccessRawData(CurrentValues);
				if (Property && CurrentValues.Num() == Default.PerObjectValues.Num())
				{
					for (int32 Index = 0; Index < CurrentValues.Num(); ++Index)
					{
						if (CurrentValues[Index]
							&& Default.PerObjectValues[Index].Get())
						{
							Property->CopyCompleteValue(
								CurrentValues[Index],
								Default.PerObjectValues[Index].Get());
						}
					}
				}
				return;
			}

			Handle->ResetToDefault();
		}
	}

	FResetToDefaultOverride MakeResetOverride(
		TSharedPtr<IPropertyHandle> InstancedStructRoot)
	{
		const TWeakPtr<IPropertyHandle> WeakRoot = InstancedStructRoot;
		return FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateStatic(
				&Private::IsResetVisible, WeakRoot),
			FResetToDefaultHandler::CreateStatic(
				&Private::Reset, WeakRoot),
			/*InPropagateToChildren=*/true);
	}

	void ApplyResetOverride(
		IDetailPropertyRow& Row,
		TSharedPtr<IPropertyHandle> InstancedStructRoot)
	{
		Row.OverrideResetToDefault(
			MakeResetOverride(MoveTemp(InstancedStructRoot)));
	}
}
