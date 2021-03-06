
#include "FlareMeteorite.h"
#include "../Flare.h"
#include "../Game/FlareGame.h"
#include "../Data/FlareMeteoriteCatalog.h"
#include "Components/DestructibleComponent.h"
#include "../Player/FlarePlayerController.h"

#define LOCTEXT_NAMESPACE "FlareMeteorite"


/*----------------------------------------------------
	Constructor
----------------------------------------------------*/

AFlareMeteorite::AFlareMeteorite(const class FObjectInitializer& PCIP) : Super(PCIP)
{
	// Mesh
	Meteorite = PCIP.CreateDefaultSubobject<UDestructibleComponent>(this, TEXT("Meteorite"));
	Meteorite->bTraceComplexOnMove = true;
	Meteorite->SetLinearDamping(0);
	Meteorite->SetAngularDamping(0);
	Meteorite->SetSimulatePhysics(true);
	RootComponent = Meteorite;
	SetActorEnableCollision(true);

	// Physics
	Meteorite->SetMobility(EComponentMobility::Movable);
	Meteorite->SetCollisionProfileName("Destructible");
	Meteorite->GetBodyInstance()->SetUseAsyncScene(false);
	Meteorite->GetBodyInstance()->SetInstanceSimulatePhysics(true);
	Meteorite->SetNotifyRigidBodyCollision(true);

	// Settings
	Meteorite->PrimaryComponentTick.bCanEverTick = true;
	PrimaryActorTick.bCanEverTick = true;
	Paused = false;
}

void AFlareMeteorite::Load(FFlareMeteoriteSave* Data, UFlareSector* ParentSector)
{
	Parent = ParentSector;

	MeteoriteData = Data;
	SetupMeteoriteMesh();
	Meteorite->SetPhysicsLinearVelocity(Data->LinearVelocity);
	Meteorite->SetPhysicsAngularVelocity(Data->AngularVelocity);
}

FFlareMeteoriteSave* AFlareMeteorite::Save()
{
	// Physical data
	MeteoriteData->Location = GetActorLocation();
	MeteoriteData->Rotation = GetActorRotation();
	if (!Paused)
	{
		MeteoriteData->LinearVelocity = Meteorite->GetPhysicsLinearVelocity();
		MeteoriteData->AngularVelocity = Meteorite->GetPhysicsAngularVelocity();
	}

	return MeteoriteData;
}

void AFlareMeteorite::BeginPlay()
{
	Super::BeginPlay();
}

void AFlareMeteorite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if(!Target)
	{
		Target = Parent->FindSpacecraft(MeteoriteData->TargetStation);
	}

	/*FLOGV("Meteorite %s vel=%s", *GetName(), *Meteorite->GetPhysicsLinearVelocity().ToString());
	FLOGV(" - IsPhysicsCollisionEnabled %d", Meteorite->IsPhysicsCollisionEnabled());
	FLOGV(" - IsPhysicsStateCreated %d", Meteorite->IsPhysicsStateCreated());
	FLOGV(" - IsAnySimulatingPhysics %d", Meteorite->IsAnySimulatingPhysics());
	FLOGV(" - IsAnyRigidBodyAwake %d", Meteorite->IsAnyRigidBodyAwake());
	FLOGV(" - IsCollisionEnabled %d", Meteorite->IsCollisionEnabled());
	FLOGV(" - IsSimulatingPhysics %d", Meteorite->IsSimulatingPhysics());*/



	/*float CollisionSize = Asteroid->GetCollisionShape().GetExtent().Size();
	if (SpawnLocation.Size() <= 0.1)
	{
		SpawnLocation = GetActorLocation();
		DrawDebugSphere(GetWorld(), SpawnLocation, CollisionSize / 2, 16, FColor::Red, true);
	}
	else
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), CollisionSize / 2, 16, FColor::Blue, false);
		DrawDebugLine(GetWorld(), GetActorLocation(), SpawnLocation, FColor::Green, false);
	}*/

	if(!IsBroken()  && !MeteoriteData->HasMissed && Target)
	{
		FVector CurrentVelocity =  Meteorite->GetPhysicsLinearVelocity();
		FVector CurrentDirection = CurrentVelocity.GetUnsafeNormal();

		float Velocity = MeteoriteData->LinearVelocity.Size();

		FVector TargetDirection = (Target->GetActorLocation() - GetActorLocation()).GetUnsafeNormal();
		FVector TargetVelocity = TargetDirection * Velocity;

		float Dot = FVector::DotProduct(CurrentDirection, TargetDirection);

		//UKismetSystemLibrary::DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + TargetDirection * 10000,FColor::Green, 1000.f);
		//UKismetSystemLibrary::DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + CurrentDirection * 10000,FColor::Red, 1000.f);

		if(Dot > 0)
		{
			// Don't fix if miss
			FVector DeltaVelocity = TargetVelocity - CurrentVelocity;

			FVector DeltaVelocityClamped = DeltaVelocity.GetClampedToMaxSize(50.f * DeltaSeconds);


			FVector CorrectedVelocity = CurrentVelocity + DeltaVelocityClamped;
			FVector CorrectedDirection = CorrectedVelocity.GetUnsafeNormal();

			//UKismetSystemLibrary::DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + CorrectedDirection * 10000 ,FColor::Blue, 1000.f);
			Meteorite->SetPhysicsLinearVelocity(CorrectedDirection * Velocity);
		}
		else if(Dot < -0.5f)
		{
			MeteoriteData->HasMissed = true;

			Parent->GetGame()->GetPC()->Notify(LOCTEXT("MeteoriteMiss", "Meteorite miss"),
									FText::Format(LOCTEXT("MeteoriteMissFormat", "A meteorite missed {0}. It's no more a danger."), UFlareGameTools::DisplaySpacecraftName(Target->GetParent())),
									FName("meteorite-miss"),
									EFlareNotification::NT_Military,
									false);


			Parent->GetGame()->GetQuestManager()->OnEvent(FFlareBundle().PutTag("meteorite-miss-station").PutName("sector", Parent->GetSimulatedSector()->GetIdentifier()));
		}
	}
}


void AFlareMeteorite::OnCollision(class AActor* Other, FVector HitLocation)
{
	AFlareAsteroid* Asteroid = Cast<AFlareAsteroid>(Other);
	AFlareSpacecraft* Spacecraft = Cast<AFlareSpacecraft>(Other);

	if(Asteroid)
	{
		ApplyDamage(MeteoriteData->BrokenDamage, 1.f , HitLocation, EFlareDamage::DAM_Collision, NULL, FString());
	}
	else if (Spacecraft && (Spacecraft->IsStation() || Spacecraft->GetSize() == EFlarePartSize::L))
	{

		if(!IsBroken())
		{
			FVector DeltaVelocity = Spacecraft->GetLinearVelocity() - Meteorite->GetPhysicsLinearVelocity();

			float Energy = MeteoriteData->BrokenDamage * DeltaVelocity.SizeSquared() * (Spacecraft->IsStation() ? 0.00001 : 0.00000001);

			Spacecraft->GetDamageSystem()->ApplyDamage(Energy, MeteoriteData->BrokenDamage, HitLocation, EFlareDamage::DAM_Collision, NULL, FString());

			// Notify PC
			Parent->GetGame()->GetPC()->Notify(LOCTEXT("MeteoriteCrash", "Meteorite crashed"),
									FText::Format(LOCTEXT("MeteoriteCrashFormat", "A meteorite crashed on {0}"), UFlareGameTools::DisplaySpacecraftName(Spacecraft->GetParent())),
									FName("meteorite-crash"),
									EFlareNotification::NT_Military,
									false);


			if(Spacecraft->IsStation())
			{
				Parent->GetGame()->GetQuestManager()->OnEvent(FFlareBundle().PutTag("meteorite-hit-station").PutName("sector", Parent->GetSimulatedSector()->GetIdentifier()));
			}

			ApplyDamage(MeteoriteData->BrokenDamage, 1.f , HitLocation, EFlareDamage::DAM_Collision, NULL, FString());
		}
	}
}

void AFlareMeteorite::SetPause(bool Pause)
{
	FLOGV("AFlareMeteorite::SetPause Pause=%d", Pause);
	
	if (Paused == Pause)
	{
		return;
	}

	CustomTimeDilation = (Pause ? 0.f : 1.0);
	if (Pause)
	{
		Save(); // Save must be performed with the old pause state
	}

	Meteorite->SetSimulatePhysics(!Pause);

	Paused = Pause;
	SetActorHiddenInGame(Pause);

	if (!Pause)
	{
		Meteorite->SetPhysicsLinearVelocity(MeteoriteData->LinearVelocity);
		Meteorite->SetPhysicsAngularVelocity(MeteoriteData->AngularVelocity);
	}
}

void AFlareMeteorite::SetupMeteoriteMesh()
{
	AFlareGame* Game = Cast<AFlareGame>(GetWorld()->GetAuthGameMode());
	
	if (Game->GetMeteoriteCatalog())
	{
		const TArray<UDestructibleMesh*>& MeteoriteList = MeteoriteData->IsMetal ? Game->GetMeteoriteCatalog()->MetalMeteorites : Game->GetMeteoriteCatalog()->RockMeteorites;
		FCHECK(MeteoriteData->MeteoriteMeshID >= 0 && MeteoriteData->MeteoriteMeshID < MeteoriteList.Num());

		Meteorite->SetDestructibleMesh(MeteoriteList[MeteoriteData->MeteoriteMeshID]);
		Meteorite->GetBodyInstance()->SetMassScale(10000);
		Meteorite->GetBodyInstance()->UpdateMassProperties();
	}
	else
	{
		return;
	}
}

bool AFlareMeteorite::IsBroken()
{
	return MeteoriteData->Damage >= MeteoriteData->BrokenDamage;
}

void AFlareMeteorite::ApplyDamage(float Energy, float Radius, FVector Location, EFlareDamage::Type DamageType, UFlareSimulatedSpacecraft* DamageSource, FString DamageCauser)
{

	AFlarePlayerController* PC = Parent->GetGame()->GetPC();
	if (DamageSource == PC->GetShipPawn()->GetParent())
	{
		PC->SignalHit(NULL, DamageType);
	}

	if(!IsBroken())
	{
		MeteoriteData->Damage+= Energy;

		if (IsBroken())
		{

			if(DamageType != EFlareDamage::DAM_Collision)
			{
				// Notify PC
				Parent->GetGame()->GetPC()->Notify(LOCTEXT("MeteoriteDestroyed", "Meteorite destroyed"),
										LOCTEXT("MeteoriteDestroyedFormat", "A meteorite has been destroyed"),
										FName("meteorite-destroyed"),
										EFlareNotification::NT_Military,
										false);
			}

			if(!MeteoriteData->HasMissed)
			{
				Parent->GetGame()->GetQuestManager()->OnEvent(FFlareBundle().PutTag("meteorite-destroyed").PutName("sector", Parent->GetSimulatedSector()->GetIdentifier()));
			}


			FLOGV("Energy %f", Energy);


			Meteorite->ApplyRadiusDamage(FMath::Max(200.f, Energy), Location, 1.f, FMath::Max(200.f, Energy )* 200 , false);

		}
	}
	else
	{
		FLOGV("Bonus Energy %f", Energy);

		if(DamageType != EFlareDamage::DAM_Collision)
		{
			Meteorite->ApplyRadiusDamage(FMath::Max(180.f, Energy/ 100 ) , Location, 1.f, FMath::Max(180.f, Energy ) * 500 , false);
		}
	}
}

#undef LOCTEXT_NAMESPACE

