#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VRCalAlignment.h"
#include "VRCalAlignmentComponent.generated.h"

class APawn;
class UVRCalAlignPointComponent;
class UOculusXRAnchorComponent;
DECLARE_MULTICAST_DELEGATE(FVRCalPawnMoved);
DECLARE_MULTICAST_DELEGATE_OneParam(FVRCalPointRecord, const FString&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRCalRecordEvent, const FString&, Message);

UENUM(BlueprintType)
enum class EVRCalAlignmentState : uint8 { Applied, NoSavedValue, NeedsCheck };

USTRUCT(BlueprintType)
struct VRCALIBRATIONCORE_API FVRCalBuckProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alignment") FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alignment", meta=(ClampMin="0", ClampMax="49")) int32 FirstMarkerId = 1;
};

UCLASS(ClassGroup=(VRCalibration), meta=(BlueprintSpawnableComponent))
class VRCALIBRATIONCORE_API UVRCalAlignmentComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVRCalAlignmentComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    UFUNCTION(BlueprintCallable, Category="Alignment") bool RequestReapply();
    UFUNCTION(BlueprintPure, Category="Alignment") bool IsFindingAnchor() const { return bFindingAnchor; }
    UFUNCTION(BlueprintPure, Category="Alignment") FString Describe() const;
    UFUNCTION(BlueprintPure, Category="Alignment") FString ActiveBuckName() const;
    UFUNCTION(BlueprintPure, Category="Alignment") FString BuckStatus() const;
    UFUNCTION(BlueprintCallable, Category="Alignment") bool SelectNextBuck(FString& Name, FString& Failure) const;
    FString NextBuckRejection() const;
    FString ValidateBuckProfiles() const;
    FString FindAlignPoints(TArray<UVRCalAlignPointComponent*>& Out, const FString& BuckName) const;
    bool SaveMarkerPoints(const FString& BuckName, const TArray<FVRCalAlignPointSample>& Samples, FString& Failure);
    void InitializeAlignment(APawn* Pawn, const FVRCalSavedAlignment* Value, int32 Origin, const FVector2D& PlayArea);
    FVRCalPawnMoved OnPawnMoved;
    FVRCalPointRecord OnPointRecord;
    UPROPERTY(BlueprintAssignable, Category="Alignment") FVRCalRecordEvent OnRecord;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alignment") TArray<FVRCalBuckProfile> Bucks;
    UPROPERTY(EditAnywhere, Category="Point alignment", meta=(Units="cm", ClampMin="0")) double PointMinSpanCm = 20;
    UPROPERTY(EditAnywhere, Category="Point alignment", meta=(Units="cm", ClampMin="0")) double PointFitLimitCm = 2;
    UPROPERTY(EditAnywhere, Category="Alignment", meta=(Units="cm", ToolTip="Floor height in the owner's local space (K1 §3.3)")) float FloorLocalZCm = 0;
    UPROPERTY(EditAnywhere, Category="Alignment", meta=(Units="cm", ClampMin="0")) float PlayAreaToleranceCm = 1;
    UPROPERTY(EditAnywhere, Category="Anchor alignment", meta=(Units="s", ClampMin="0")) double AnchorStableSeconds = 1;
    UPROPERTY(EditAnywhere, Category="Anchor alignment", meta=(Units="cm", ClampMin="0")) double AnchorStableCm = 0.5;
    UPROPERTY(EditAnywhere, Category="Anchor alignment", meta=(Units="deg", ClampMin="0")) double AnchorStableDeg = 0.5;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Alignment") EVRCalAlignmentState State = EVRCalAlignmentState::NoSavedValue;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Alignment") FString Reason;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual FString SavedAlignmentPath() const;
    virtual FString CalibrationDirectory() const;
    virtual void ReadPointEnvironment(int32& Origin, FVector2D& PlayArea) const;
private:
    UPROPERTY() TObjectPtr<APawn> AlignmentPawn;
    FVRCalSavedAlignment Saved;
    int32 ActiveBuckIndex = 0;
    bool bHasSaved = false;
    FDelegateHandle RecenterHandle;
    void HandleRecenter();
    void LoadSetupAlignment(bool bFindAnchor = false);
    void InitializeSetup(APawn* Pawn, const FVRCalSavedAlignment* Value, int32 Origin, const FVector2D& PlayArea, bool bFromFile, bool bWaitForAnchor = false);
    bool BasePawn(FTransform& Out, FString& Failure) const;
    bool FitCaptured(const TArray<FVRCalAlignPointSample>& Samples, FVRCalPointFit& Fit, FString& Failure, const FString& BuckName) const;
    FString PointFitRejection(const TArray<FVRCalAlignPointSample>& Samples, const FString& BuckName) const;
    void ReadActiveBuck(FString& FallbackReason);
    bool WriteActiveBuck(const FString& Name, FString& Failure) const;
    FString BuckAlignmentPath(const FString& Name) const;
    static bool AnchorAlignmentEnabled();
    void SaveAlignmentAnchor(const FString& Path, const FVRCalSavedAlignment& Candidate);
    void BeginAnchorDiscovery();
    void UseLocatedAnchor(UOculusXRAnchorComponent* Anchor);
    void AdvanceAnchorDiscovery();
    void FinishAnchorDiscovery(const FTransform* Stage, int32 Result);
    void ClearAnchorActors();
    void AnchorResult(const FString& Message);
    bool bFindingAnchor = false, bAnchorApplied = false;
    FString LocatedAnchorUuid, AnchorNotice;
    FTransform LocatedAnchorStage;
    double AnchorStarted = 0;
    int32 AnchorDiscoveryResult = 0;
    uint32 AnchorSaveSerial = 0;
    uint64 AnchorActorCreatedFrame = 0;
    bool bAnchorCandidate = false;
    FTransform AnchorCandidateStage = FTransform::Identity;
    double AnchorStableSince = 0;
    int32 AnchorPoseChanges = 0;
    FTransform AnchorActorInitialWorld = FTransform::Identity;
    TWeakObjectPtr<AActor> PendingAnchorActor;
    TWeakObjectPtr<UOculusXRAnchorComponent> LocatedAnchor;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FAnchorTestAccess;
    friend struct FProfileFixture;
    TFunction<void()> AnchorRequestForTest;
    bool bAnchorLocationValidForTest = false;
#endif
};
