#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/Engine.h"
#include "SAFDebugTool.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSeinARTS, Log, All);


// Macro Wrappers for simple C++ Debug pritning
// ==========================================================================================
// Shorthand for FString::Printf(TEXT(...), ...)
#define FORMATSTR(FmtLiteral, ...) FString::Printf(TEXT(FmtLiteral), ##__VA_ARGS__)

// Draws a sphere at the actor's location + a forward arrow in the given color
#define SAFDEBUG_DRAWARROWSPHERE(Radius, ColorExpr) \
do { \
	if (AActor* __Owner = GetOwner()) { \
		const FVector __Loc = __Owner->GetActorLocation(); \
		const FRotator __Rot = __Owner->GetActorRotation(); \
		if (UWorld* __World = __Owner->GetWorld()) { \
			DrawDebugSphere(__World, __Loc, Radius, 8, (ColorExpr), false, 0.f, 0, 2.f); \
			DrawDebugDirectionalArrow(__World, __Loc, __Loc + __Rot.Vector() * (Radius * 5.f), Radius * 0.5f, (ColorExpr), false, 0.f, 0, 2.f); \
		} \
	} \
} while (0)

// Internal base macro (manages SeinARTS logging)
#define SAFDEBUG_LOG_AND_SCREEN(ColorExpr, LogVerbosity, MsgExpr) \
do { \
	const FString __SafMsg = FString(MsgExpr); \
	if (GEngine) { \
		GEngine->AddOnScreenDebugMessage(-1, 5.f, (ColorExpr), __SafMsg); \
	} \
	UE_LOG(LogSeinARTS, LogVerbosity, TEXT("%s"), *__SafMsg); \
} while (0)

// Public logging macros
#define SAFDEBUG_INFO(MsgExpr)    SAFDEBUG_LOG_AND_SCREEN(FColor::Cyan,    Log,     MsgExpr)
#define SAFDEBUG_WARNING(MsgExpr) SAFDEBUG_LOG_AND_SCREEN(FColor::Yellow,  Warning, MsgExpr)
#define SAFDEBUG_SUCCESS(MsgExpr) SAFDEBUG_LOG_AND_SCREEN(FColor::Green,   Display, MsgExpr)
#define SAFDEBUG_ERROR(MsgExpr)   SAFDEBUG_LOG_AND_SCREEN(FColor::Red,     Error,   MsgExpr)


/**
 * SAFDebugTool
 * 
 * A BlueprintFunctionLibrary wrapper around SAFDebug inline functions.
 * This provides SeinARTS Framework debugging utilities to Blueprints.
 */
UCLASS(ClassGroup=(SeinARTS), meta=(DisplayName="SeinARTS Debug Tool"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFDebugTool : public UBlueprintFunctionLibrary {
	GENERATED_BODY()

public:

	/** Logs an info message on screen and to output/log */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Debug", meta=(DisplayName="Log Info"))
	static void Info(const FString& Message) { SAFDEBUG_INFO(Message); }

	/** Logs a warning message on screen and to output/log */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Debug", meta=(DisplayName="Log Warning"))
	static void Warning(const FString& Message) { SAFDEBUG_WARNING(Message); }

	/** Logs a success message on screen and to output/log */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Debug", meta=(DisplayName="Log Success"))
	static void Success(const FString& Message) { SAFDEBUG_SUCCESS(Message); }

	/** Logs an error message on screen and to output/log */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Debug", meta=(DisplayName="Log Error"))
	static void Error(const FString& Message) { SAFDEBUG_ERROR(Message); }

};
