#include "SeinARTSLevelDataModule.h"
#include "SeinARTSLevelDataLog.h"
#include "Modules/ModuleManager.h"

// Module-shared log categories (declared in SeinARTSLevelDataLog.h) — defined
// once here so they register at module load and always show in the Output Log
// category filter.
DEFINE_LOG_CATEGORY(LogSeinLevelData);
DEFINE_LOG_CATEGORY(LogSeinLevelDataSubsystem);

void FSeinARTSLevelDataModule::StartupModule() {}
void FSeinARTSLevelDataModule::ShutdownModule() {}

IMPLEMENT_MODULE(FSeinARTSLevelDataModule, SeinARTSLevelData)
