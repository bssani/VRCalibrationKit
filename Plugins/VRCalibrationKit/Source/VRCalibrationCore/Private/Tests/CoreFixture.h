#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "VRCalCoreTestAlignment.h"
#include "VRCalAlignPointComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "Misc/Paths.h"

struct FCoreFixture
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    APawn* Pawn;
    AActor* Car;
    UVRCalCoreTestAlignment* A;
    TArray<UVRCalAlignPointComponent*> Points;
    IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("VRCal.AnchorAlignment"));
    int32 Previous = Switch->GetInt();
    FString Path = FPaths::ProjectSavedDir() / TEXT("Automation") / (FGuid::NewGuid().ToString() + TEXT("-Core.json"));
    FCoreFixture()
    {
        Switch->Set(0, ECVF_SetByCode);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        auto* PC = World->SpawnActor<APlayerController>();
        Pawn = World->SpawnActor<APawn>();
        Pawn->SetRootComponent(NewObject<USceneComponent>(Pawn));
        Pawn->GetRootComponent()->RegisterComponent();
        PC->Possess(Pawn);
        Car = World->SpawnActor<AActor>();
        Car->SetRootComponent(NewObject<USceneComponent>(Car));
        Car->GetRootComponent()->RegisterComponent();
        A = NewObject<UVRCalCoreTestAlignment>(Car);
        Car->AddInstanceComponent(A);
        A->TestPath = Path;
        A->RegisterComponent();
        const TArray<FVector> Local = {FVector(4,-60,50), FVector(4,-14,50), FVector(-44,-14,50), FVector(-44,-60,50)};
        for (int32 I = 0; I < Local.Num(); ++I)
        {
            auto* P = NewObject<UVRCalAlignPointComponent>(Car);
            Car->AddInstanceComponent(P);
            P->Order = I + 1;
            P->SetupAttachment(Car->GetRootComponent());
            P->RegisterComponent();
            P->SetRelativeLocation(Local[I]);
            Points.Add(P);
        }
    }
    ~FCoreFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        IFileManager::Get().Delete(*Path);
        Switch->Set(Previous, ECVF_SetByCode);
    }
    FVRCalSavedAlignment Value(bool bAnchor = true) const
    {
        FVRCalSavedAlignment S;
        S.SavedAt = TEXT("2026-11-18T00:00:00Z");
        S.TrackingOrigin = EHMDTrackingOrigin::Stage;
        S.PlayAreaCm = A->Area;
        for (const auto* P : Points)
        {
            FVRCalAlignPointSample Point;
            Point.Order = P->Order;
            Point.StageCm = P->GetRelativeLocation();
            Point.Frames = 15;
            S.Points.Add(Point);
        }
        if (bAnchor) { S.AnchorUuid = TEXT("00112233445566778899aabbccddeeff"); S.AnchorStage = FTransform(FVector(10,20,50)); }
        return S;
    }
    bool Start(const FVRCalSavedAlignment& S, bool bEnabled)
    {
        FString Why;
        if (!FVRCalAlignmentStore::Save(Path, S, Why)) { return false; }
        Switch->Set(bEnabled ? 1 : 0, ECVF_SetByCode);
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        return true;
    }
};
#endif
