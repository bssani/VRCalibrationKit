#include "VRCalSamplePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"

AVRCalSamplePawn::AVRCalSamplePawn()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("TrackingOrigin")));
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(GetRootComponent());
    Camera->bLockToHmd = true;
    bUseControllerRotationYaw = false;
}

void AVRCalSamplePawn::BeginPlay()
{
    Super::BeginPlay();
    GetWorld()->GetWorldSettings()->WorldToMeters = 100;
    UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Stage);
}
