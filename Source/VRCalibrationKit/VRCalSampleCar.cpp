#include "VRCalSampleCar.h"

#include "VRCalAlignmentComponent.h"
#include "VRCalAlignPointComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AVRCalSampleCar::AVRCalSampleCar()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("VehicleRoot")));
    GetRootComponent()->SetMobility(EComponentMobility::Movable);
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ApproximateBody"));
    Body->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body->SetStaticMesh(Cube.Object);
    Body->SetRelativeLocation(FVector(0,0,25));
    Body->SetRelativeScale3D(FVector(2,1,0.5));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ApproximateLabel"));
    Label->SetupAttachment(GetRootComponent());
    Label->SetText(FText::FromString(TEXT("APPROXIMATE VEHICLE")));
    Label->SetWorldSize(10);
    Label->SetRelativeLocation(FVector(0,0,70));
    Alignment = CreateDefaultSubobject<UVRCalAlignmentComponent>(TEXT("Alignment"));
    const TArray<FVector> Locations = {FVector(4,-60,50), FVector(4,-14,50), FVector(-44,-14,50), FVector(-44,-60,50)};
    for (int32 I = 0; I < Locations.Num(); ++I)
    {
        auto* Point = CreateDefaultSubobject<UVRCalAlignPointComponent>(*FString::Printf(TEXT("AlignmentPoint%d"), I + 1));
        Point->SetupAttachment(GetRootComponent());
        Point->Order = I + 1;
        Point->Label = FString::Printf(TEXT("Marker %d"), I + 1);
        Point->SetRelativeLocation(Locations[I]);
        AlignPoints.Add(Point);
    }
}

void AVRCalSampleCar::BeginPlay()
{
    Super::BeginPlay();
    if (auto* PC = UGameplayStatics::GetPlayerController(this,0))
    {
        EnableInput(PC);
        InputComponent->BindKey(EKeys::A, IE_Pressed, this, &AVRCalSampleCar::Reapply);
    }
}

void AVRCalSampleCar::Reapply() { Alignment->RequestReapply(); }

void AVRCalSampleCar::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bEnableDebugMovement) { return; }
    if (const auto* PC = UGameplayStatics::GetPlayerController(this,0))
    {
        const float Direction = (PC->IsInputKeyDown(EKeys::Up) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::Down) ? 1.f : 0.f);
        AddActorLocalOffset(FVector(Direction * DebugSpeedCmPerSecond * DeltaSeconds, 0, 0));
    }
}
