#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "VRCalSampleGameMode.generated.h"

UCLASS()
class VRCALIBRATIONKIT_API AVRCalSampleGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AVRCalSampleGameMode();
protected:
    virtual void StartPlay() override;
};
