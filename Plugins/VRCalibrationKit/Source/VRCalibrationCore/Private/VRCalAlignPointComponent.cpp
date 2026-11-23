#include "VRCalAlignPointComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

UVRCalAlignPointComponent::UVRCalAlignPointComponent()
{
	Sphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PointSphere")); Sphere->SetupAttachment(this);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Sphere->SetStaticMesh(Mesh.Object); Sphere->SetRelativeScale3D(FVector(0.025));
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision); Sphere->SetGenerateOverlapEvents(false);
	Sphere->SetCastShadow(false);
	Number = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PointNumber")); Number->SetupAttachment(this);
	Number->SetRelativeLocation(FVector(0,0,3)); Number->SetWorldSize(3); Number->SetHorizontalAlignment(EHTA_Center);
	Number->SetCollisionEnabled(ECollisionEnabled::NoCollision); Number->SetGenerateOverlapEvents(false);
	Number->SetTextRenderColor(FColor::Yellow);
	// Visible in editor (place on CAD mesh). Hidden only in game (K1 §2)
	// AttachChildren empty in constructor. Hide children directly (K1 §2)
	SetHiddenInGame(true); Sphere->SetHiddenInGame(true); Number->SetHiddenInGame(true);
}

void UVRCalAlignPointComponent::OnRegister()
{
	Super::OnRegister();
	// Blueprint duplication can retain a template parent (K1 §3.5)
	if (!Sphere->IsRegistered()) { Sphere->SetupAttachment(this); Sphere->RegisterComponent(); }
	if (!Number->IsRegistered()) { Number->SetupAttachment(this); Number->RegisterComponent(); }
	Number->SetText(FText::AsNumber(Order));
}

void UVRCalAlignPointComponent::ShowPoint(bool bShow)
{
	Number->SetText(FText::AsNumber(Order));
	SetHiddenInGame(!bShow,true);
}
