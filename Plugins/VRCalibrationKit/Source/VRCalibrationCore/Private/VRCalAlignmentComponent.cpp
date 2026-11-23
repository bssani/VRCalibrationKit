#include "VRCalAlignmentComponent.h"

#include "VRCalAlignPointComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IXRTrackingSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"

UVRCalAlignmentComponent::UVRCalAlignmentComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UVRCalAlignmentComponent::BeginPlay()
{
    Super::BeginPlay();
    RecenterHandle = FCoreDelegates::VRHeadsetRecenter.AddUObject(this, &UVRCalAlignmentComponent::HandleRecenter);
    AlignmentPawn = UGameplayStatics::GetPlayerPawn(this, 0);
    LoadSetupAlignment(true);
}

void UVRCalAlignmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bFindingAnchor = false;
    ++AnchorSaveSerial;
    ClearAnchorActors();
    FCoreDelegates::VRHeadsetRecenter.Remove(RecenterHandle);
    RecenterHandle.Reset();
    Super::EndPlay(EndPlayReason);
}

FString UVRCalAlignmentComponent::SavedAlignmentPath() const
{
    return Bucks.IsEmpty() ? FPaths::Combine(CalibrationDirectory(), TEXT("BuckEye.json")) : BuckAlignmentPath(ActiveBuckName());
}

void UVRCalAlignmentComponent::ReadPointEnvironment(int32& Origin, FVector2D& PlayArea) const
{
    Origin = GEngine && GEngine->XRSystem.IsValid() ? static_cast<int32>(GEngine->XRSystem->GetTrackingOrigin()) : INDEX_NONE;
    PlayArea = UHeadMountedDisplayFunctionLibrary::GetPlayAreaBounds(EHMDTrackingOrigin::Stage);
}

void UVRCalAlignmentComponent::LoadSetupAlignment(bool bFindAnchor)
{
    bAnchorApplied = false;
    FString Fallback;
    const FString ProfileFailure = ValidateBuckProfiles();
    if (!ProfileFailure.IsEmpty())
    {
        InitializeSetup(AlignmentPawn, nullptr, INDEX_NONE, FVector2D::ZeroVector, true);
        State = EVRCalAlignmentState::NeedsCheck;
        Reason = TEXT("Buck profiles invalid — ") + ProfileFailure;
        return;
    }
    if (!Bucks.IsEmpty()) { ReadActiveBuck(Fallback); }
    FVRCalSavedAlignment Loaded;
    FString Failure;
    const bool bLoaded = FVRCalAlignmentStore::Load(SavedAlignmentPath(), Loaded, Failure);
    int32 Origin;
    FVector2D Bounds;
    ReadPointEnvironment(Origin, Bounds);
    const bool bUseAnchor = AnchorAlignmentEnabled() && bLoaded && !Loaded.AnchorUuid.IsEmpty();
    bAnchorApplied = bUseAnchor && LocatedAnchorUuid == Loaded.AnchorUuid;
    const bool bWait = bUseAnchor && bFindAnchor && !bAnchorApplied;
    InitializeSetup(AlignmentPawn, bLoaded ? &Loaded : nullptr, Origin, Bounds, true, bWait);
    if (bWait) { BeginAnchorDiscovery(); }
    if (!bLoaded)
    {
        Reason = Bucks.IsEmpty() ? TEXT("No saved value — ") + Failure : TEXT("No saved value for buck ") + ActiveBuckName() + TEXT(" — ") + Failure;
    }
    if (!Fallback.IsEmpty()) { Reason += TEXT(" — ") + Fallback; }
}

void UVRCalAlignmentComponent::InitializeAlignment(APawn* Pawn, const FVRCalSavedAlignment* Value, int32 Origin, const FVector2D& PlayArea)
{
    bAnchorApplied = false;
    InitializeSetup(Pawn, Value, Origin, PlayArea, false);
}

void UVRCalAlignmentComponent::InitializeSetup(APawn* Pawn, const FVRCalSavedAlignment* Value, int32 Origin, const FVector2D& PlayArea, bool bFromFile, bool bWaitForAnchor)
{
    AlignmentPawn = Pawn;
    bHasSaved = Value != nullptr;
    State = EVRCalAlignmentState::NoSavedValue;
    Reason = TEXT("No saved value — use the calibration level");
    if (!Value) { return; }
    Saved = *Value;
    State = EVRCalAlignmentState::NeedsCheck;
    if (bWaitForAnchor) { Reason = TEXT("Finding anchor"); return; }
    auto Decision = FVRCalAlignment::CheckEnvironment(Saved, Origin, PlayArea, PlayAreaToleranceCm);
    if (bAnchorApplied)
    {
        if (Origin != EHMDTrackingOrigin::Stage)
        {
            Decision.bApply = false;
            Decision.bNeedsCheck = true;
            Decision.Reason = TEXT("Stage tracking origin required");
        }
        else
        {
            if (!Decision.Reason.IsEmpty()) { AnchorResult(TEXT("anchor: ") + Decision.Reason + TEXT(" (notice only)")); }
            Decision.bApply = true;
            Decision.bNeedsCheck = false;
            Decision.Reason = TEXT("anchor");
        }
    }
    Reason = Decision.Reason;
    FTransform RelativePawn;
    FString Failure;
    if (!AlignmentPawn || !AlignmentPawn->GetRootComponent() || !GetOwner()->GetRootComponent() || AlignmentPawn->bUseControllerRotationYaw ||
        !FMath::IsNearlyEqual(GetWorld()->GetWorldSettings()->WorldToMeters, 100.0f))
    { Reason = TEXT("Pawn/root/WorldToMeters check required"); }
    else if (!BasePawn(RelativePawn, Failure)) { Reason = Failure; }
    else if (Decision.bApply)
    {
        // Local pose remains valid while the vehicle moves (K1 §3.3)
        if (!AlignmentPawn->AttachToActor(GetOwner(), FAttachmentTransformRules::KeepWorldTransform))
        { Reason = TEXT("Pawn attachment failed"); return; }
        AlignmentPawn->SetActorRelativeTransform(RelativePawn, false, nullptr, ETeleportType::TeleportPhysics);
        State = Decision.bNeedsCheck ? EVRCalAlignmentState::NeedsCheck : EVRCalAlignmentState::Applied;
        OnPawnMoved.Broadcast();
    }
    UE_LOG(LogTemp, Log, TEXT("Alignment %s"), *Describe());
}

FString UVRCalAlignmentComponent::FindAlignPoints(TArray<UVRCalAlignPointComponent*>& Out, const FString& BuckName) const
{
    GetOwner()->GetComponents(Out);
    if (!Bucks.IsEmpty()) { Out.RemoveAll([&BuckName](const auto* Point) { return Point->Buck != BuckName; }); }
    Out.Sort([](const UVRCalAlignPointComponent& A, const UVRCalAlignPointComponent& B) { return A.Order < B.Order; });
    if (Out.Num() < 2) { return TEXT("Alignment points missing on the car"); }
    for (int32 I = 0; I < Out.Num(); ++I)
    {
        if (Out[I]->Order < 1) { return TEXT("Alignment points missing on the car"); }
        if (I > 0 && Out[I]->Order == Out[I - 1]->Order) { return TEXT("Alignment point order duplicated"); }
    }
    return {};
}

bool UVRCalAlignmentComponent::FitCaptured(const TArray<FVRCalAlignPointSample>& Samples, FVRCalPointFit& Fit, FString& Failure, const FString& BuckName) const
{
    TArray<UVRCalAlignPointComponent*> Points;
    Failure = FindAlignPoints(Points, BuckName);
    if (!Failure.IsEmpty() || Samples.Num() != Points.Num()) { Failure = TEXT("alignment points changed"); return false; }
    TArray<FVector> Stage, Local;
    for (const auto* Point : Points)
    {
        const auto* Sample = Samples.FindByPredicate([Point](const FVRCalAlignPointSample& P) { return P.Order == Point->Order; });
        if (!Sample) { Failure = TEXT("alignment points changed"); return false; }
        Stage.Add(Sample->StageCm);
        Local.Add(GetOwner()->GetActorTransform().InverseTransformPosition(Point->GetComponentLocation()));
    }
    if (!FVRCalAlignment::FitPoints(Stage, Local, FloorLocalZCm, Fit)) { Failure = TEXT("Points too close"); return false; }
    return true;
}

bool UVRCalAlignmentComponent::BasePawn(FTransform& Out, FString& Failure) const
{
    FVRCalPointFit Fit;
    auto Points = Saved.Points;
    if (bAnchorApplied)
    {
        for (auto& Point : Points)
        { Point.StageCm = LocatedAnchorStage.TransformPosition(Saved.AnchorStage.InverseTransformPosition(Point.StageCm)); }
    }
    if (!FitCaptured(Points, Fit, Failure, ActiveBuckName())) { return false; }
    if (Fit.MaxXYCm > PointFitLimitCm) { Failure = TEXT("alignment points moved"); return false; }
    Out = Fit.Pawn;
    return true;
}

FString UVRCalAlignmentComponent::PointFitRejection(const TArray<FVRCalAlignPointSample>& Samples, const FString& BuckName) const
{
    FVRCalPointFit Fit;
    FString Failure;
    if (!FitCaptured(Samples, Fit, Failure, BuckName)) { return Failure; }
    if (Fit.SpanCm < PointMinSpanCm) { return TEXT("Points too close"); }
    if (Fit.MaxXYCm > PointFitLimitCm) { return FString::Printf(TEXT("Fit %.2f cm over limit %.2f cm"), Fit.MaxXYCm, PointFitLimitCm); }
    return {};
}

bool UVRCalAlignmentComponent::RequestReapply()
{
    if (bFindingAnchor) { return false; }
    LoadSetupAlignment();
    return true;
}

void UVRCalAlignmentComponent::HandleRecenter()
{
    bFindingAnchor = false;
    State = EVRCalAlignmentState::NeedsCheck;
    Reason = TEXT("Recenter — installation alignment check required");
    UE_LOG(LogTemp, Log, TEXT("Alignment %s; Pawn unchanged"), *Describe());
}

void UVRCalAlignmentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AdvanceAnchorDiscovery();
    if (bHasSaved && State == EVRCalAlignmentState::Applied && GEngine && GEngine->XRSystem.IsValid())
    {
        const auto Check = FVRCalAlignment::CheckEnvironment(Saved, GEngine->XRSystem->GetTrackingOrigin(),
            bAnchorApplied ? Saved.PlayAreaCm : UHeadMountedDisplayFunctionLibrary::GetPlayAreaBounds(EHMDTrackingOrigin::Stage), PlayAreaToleranceCm);
        if (Check.bNeedsCheck) { State = EVRCalAlignmentState::NeedsCheck; Reason = Check.Reason; }
    }
}

FString UVRCalAlignmentComponent::Describe() const
{
    const TCHAR* Name = State == EVRCalAlignmentState::Applied ? TEXT("Applied") : State == EVRCalAlignmentState::NoSavedValue ? TEXT("No saved value") : TEXT("NeedsCheck");
    return FString::Printf(TEXT("%s %s | saved %s | %s"), Name, *Reason, *Saved.SavedAt, *BuckStatus());
}
