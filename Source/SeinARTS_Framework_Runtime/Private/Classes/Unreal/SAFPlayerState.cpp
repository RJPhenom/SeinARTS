#include "Classes/Unreal/SAFPlayerState.h"
#include "Structs/SAFResources.h"
#include "Net/UnrealNetwork.h"

ASAFPlayerState::ASAFPlayerState(){
  //CreateDefaultSubobject<USAFTechModifiersComponent>(TEXT("TechModifiers"));
}

// Adds an individual resource, via index of the resource in the standard bundle.
void ASAFPlayerState::AddResource(int32 ResourceNumber, int32 Amount) {
  if (!HasAuthority()) return;
  if (ResourceNumber < 1 || ResourceNumber > FSAFResources::NumSlots) return;

  const int32 current = Resources.Get(ResourceNumber);
  int64 newVal64 = static_cast<int64>(current) + static_cast<int64>(Amount);
  int32 newVal = (newVal64 < 0) ? 0 : (newVal64 > INT32_MAX ? INT32_MAX : static_cast<int32>(newVal64));
  Resources.Set(ResourceNumber, newVal);
  ForceNetUpdate();
}

// Adds a resources bundle to this player's resources.
void ASAFPlayerState::AddResources(const FSAFResources& Delta) {
  if (!HasAuthority()) return;

  FSAFResources TempBundle = Resources;
  TempBundle.Add(Delta);
  TempBundle.ClampNonNegative();
  Resources = TempBundle;
  ForceNetUpdate();
}

// Returns true if current resources cover 'Cost' (no mutation).
bool ASAFPlayerState::CheckResourcesAvailable(const FSAFResources& Cost) const {
  return Resources.AllGreaterOrEqualTo(Cost);
}

// Atomically deducts if affordable; returns true on success.
bool ASAFPlayerState::RequestResources(const FSAFResources& Cost) {
  if (!HasAuthority()) return false;
  if (!Resources.AllGreaterOrEqualTo(Cost)) return false;

  Resources.Sub(Cost);
  Resources.ClampNonNegative();
  ForceNetUpdate();
  return true;
}


void ASAFPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);	
  DOREPLIFETIME(ASAFPlayerState, bIsReady);
  DOREPLIFETIME(ASAFPlayerState, TeamID);
  DOREPLIFETIME(ASAFPlayerState, Resources);
}