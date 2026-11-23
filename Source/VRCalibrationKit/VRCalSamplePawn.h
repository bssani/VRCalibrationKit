#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "VRCalSamplePawn.generated.h"

class UCameraComponent;

UCLASS()
class VRCALIBRATIONKIT_API AVRCalSamplePawn : public APawn
{
    GENERATED_BODY()
public:
    AVRCalSamplePawn();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VR") TObjectPtr<UCameraComponent> Camera;
protected:
    virtual void BeginPlay() override;
};
