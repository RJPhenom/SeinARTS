// Copyright
#include "Classes/SAFPlayerStart.h"
#include "Debug/SAFDebugTool.h"

ASAFPlayerStart::ASAFPlayerStart(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

void ASAFPlayerStart::BeginPlay() {
    SAFDEBUG_INFO(FORMATSTR("SAFPlayerStart created. Transform = %s", *GetActorTransform().ToString()));
}