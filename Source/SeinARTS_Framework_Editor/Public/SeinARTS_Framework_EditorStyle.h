#pragma once

#include "Styling/SlateStyle.h"

class FSeinARTS_Framework_EditorStyle {
public:

  static void Initialize();
  static void Shutdown();
  
  /** Access the singleton instance for this style set */
  static const FSlateStyleSet& Get();

private:

  static TSharedPtr<FSlateStyleSet> StyleSet;
};
