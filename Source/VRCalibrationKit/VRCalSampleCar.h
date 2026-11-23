#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRCalSampleCar.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UVRCalAlignPointComponent;
class UVRCalAlignmentComponent;

UCLASS()
class VRCALIBRATIONKIT_API AVRCalSampleCar : public AActor
{
    GENERATED_BODY()
public:
    AVRCalSampleCar();
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sample") TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sample") TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sample") TObjectPtr<UVRCalAlignmentComponent> Alignment;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sample") TArray<TObjectPtr<UVRCalAlignPointComponent>> AlignPoints;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sample") bool bEnableDebugMovement = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sample", meta=(Units="cm/s", ClampMin="0")) float DebugSpeedCmPerSecond = 50;
protected:
    virtual void BeginPlay() override;
private:
    void Reapply();
};
