#pragma once

#include "Styling/SlateStyle.h"

class FSeinARTS_Framework_EditorStyle {
public:

  static void Initialize();
  static void Shutdown();

private:

  static TSharedPtr<FSlateStyleSet> StyleSet;
};
