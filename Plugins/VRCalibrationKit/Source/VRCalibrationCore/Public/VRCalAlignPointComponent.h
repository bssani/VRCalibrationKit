#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "VRCalAlignPointComponent.generated.h"
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS(ClassGroup=(VRCalibration), meta=(BlueprintSpawnableComponent))
class VRCALIBRATIONCORE_API UVRCalAlignPointComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UVRCalAlignPointComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alignment points", meta=(ClampMin="1")) int32 Order = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alignment points") FString Label;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alignment points") FString Buck;
	void ShowPoint(bool bShow);
protected:
	virtual void OnRegister() override;
private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Sphere;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Number;
};
