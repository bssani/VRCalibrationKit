#include "VRCalSampleGameMode.h"

#include "VRCalSamplePawn.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"

AVRCalSampleGameMode::AVRCalSampleGameMode() { DefaultPawnClass = AVRCalSamplePawn::StaticClass(); }

void AVRCalSampleGameMode::StartPlay()
{
    // Set Stage before alignment components receive BeginPlay (K1 §3.1)
    GetWorld()->GetWorldSettings()->WorldToMeters = 100;
    UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Stage);
    Super::StartPlay();
}
