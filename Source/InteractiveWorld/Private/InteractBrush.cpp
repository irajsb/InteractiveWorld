// Copyright 2023 Sun BoHeng

#include "InteractBrush.h"

#include "InteractiveWorldBPLibrary.h"
#include "InteractiveWorldSubsystem.h"
#include "WorldInteractVolume.h"
#include "Engine/World.h"
#include "Kismet/KismetMathLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/KismetMaterialLibrary.h"

#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"


// Sets default values for this component's properties
UInteractBrush::UInteractBrush()
{
}

bool UInteractBrush::PrepareForDrawing(TArray<TSubclassOf<AWorldDrawingBoard>>& NoVolumeDrawingBoardClass)
{
	bSucceededDrawnThisTime = false;
	PreviousT = CurrentT;
	CurrentT = GetComponentTransform();
	//This brush is not in suitable InteractVolume,so it can only draw on DrawingBoards that do not use InteractVolume
	if (!GetBrushActiveInVolume())
	{
		if (NoVolumeDrawingBoardClass.Num() == 0)
		{
			//No DrawingBoards that do not use InteractVolume,so do not draw
			return false;
		}
		if (bUseDrawOnlyDrawingBoardsClassList)
		{
			bool HasSuitableClass = false;
			for (const auto & Class : NoVolumeDrawingBoardClass)
			{
				if (DrawOnlyDrawingBoardsClassList.Find(Class) != -1)
				{
					//Find suitable DrawingBoards that do not use InteractVolume
					HasSuitableClass = true;
					break;
				}
			}
			if (!HasSuitableClass)
			{
				return false;
			}
		}
	}
	//bDrawEveryFrame,bDrawOnMovement and moved,or called draw manually
	if (bDrawEveryFrame || bDrawOnce || (!UKismetMathLibrary::NearlyEqual_TransformTransform(
		CurrentT, PreviousT, MovementTolerance.X, MovementTolerance.Y, MovementTolerance.Z) && bDrawOnMovement))
	{
		//Reset bDrawOnce
		bDrawOnce = false;
		//For Blueprint part
		if (UpdateDrawInfo())
		{
			return true;
		}
	}
	//We didn't succeed to draw
	return false;
}

bool UInteractBrush::ShouldDrawOn(AWorldDrawingBoard* DrawingBoard) const
{
	//If DrawingBoard uses InteractVolume,we should make sure we are in the same volume
	//If DrawingBoard doesn't use InteractVolume,check if we use DrawOnlyDrawingBoardsClassList and find if is suitable
	return DrawOnDrawingBoards.Find(DrawingBoard) != -1
		|| (!DrawingBoard->GetUseInteractVolume()
			&& (!bUseDrawOnlyDrawingBoardsClassList
				|| DrawOnlyDrawingBoardsClassList.Find(DrawingBoard->GetClass()) != -1));
}

void UInteractBrush::PreDrawOnRT(AWorldDrawingBoard* DrawingBoard, UCanvas* CanvasDrawOn, FVector2D CanvasSize)
{
	const float TraveledDistance = UKismetMathLibrary::Distance2D(
		UInteractiveWorldBPLibrary::Vector3ToVector2(CurrentT.GetLocation()),
		UInteractiveWorldBPLibrary::Vector3ToVector2(PreviousT.GetLocation()));
	if (bUseMultiDraw && TraveledDistance > MaxDrawDistance && bSucceededDrawnLastTime)
	{
		//Draw many times between two location
		const int32 DrawTimes = FMath::CeilToInt(TraveledDistance / MaxDrawDistance);
		for (int32 i = 1; i < DrawTimes; i++)
		{
			DrawOnRT(DrawingBoard, CanvasDrawOn, CanvasSize,
			         UKismetMathLibrary::Conv_IntToDouble(i) / UKismetMathLibrary::Conv_IntToDouble(DrawTimes), DrawTimes);
		}
	}
	DrawOnRT(DrawingBoard, CanvasDrawOn, CanvasSize, 1, 1);
    bSucceededDrawnThisTime = true;
}

void UInteractBrush::FinishDraw()
{
	PreviousT = CurrentT;
	bSucceededDrawnLastTime = bSucceededDrawnThisTime;
	if (bSucceededDrawnLastTime)
	{
		PreviousDrawnT = CurrentT;	
	}
}

bool UInteractBrush::UpdateDrawInfo_Implementation()
{
	return true;
}


void UInteractBrush::DrawOnRT_Implementation(AWorldDrawingBoard* DrawingBoard, UCanvas* CanvasDrawOn,
                                             FVector2D CanvasSize, float InterpolateRate, int32 DrawTimes)
{
}


void UInteractBrush::EnterArea(AWorldInteractVolume* InteractVolume)
{
	OverlappingInteractVolumes.Add(InteractVolume);
	UpdateActiveState();
}

void UInteractBrush::LeaveArea(AWorldInteractVolume* InteractVolume)
{
	OverlappingInteractVolumes.Remove(InteractVolume);
	UpdateActiveState();
}

// Called when the game starts
void UInteractBrush::BeginPlay()
{
	Super::BeginPlay();
	GetWorld()->GetSubsystem<UInteractiveWorldSubsystem>()->RegisterBrush(this);
	UpdateActiveState();

	//Check whether the Actor this Interact Brush attach to has collision with World Interact Volume
	auto BrushCollisionWarning = [&]()->void
	{
		FMessageLog("PIE").Warning()
		->AddToken(FTextToken::Create(FText::FromString(FString(TEXT("Interact Brush: ")))))
		->AddToken(FUObjectToken::Create(this))
		->AddToken(FTextToken::Create(FText::FromString(FString(TEXT("ISN'T attached to an Actor with Collisitn. Interact Volume will not work with it.")))))
		->AddToken(FTextToken::Create(FText::FromString(FString(TEXT("Please make Actor: ")))))
		->AddToken(FUObjectToken::Create(this->GetOwner()))
		->AddToken(FTextToken::Create(FText::FromString(FString(TEXT("has query collision to 'WorldDynamic'")))));
	};
	if(!GetOwner()->GetComponentByClass(UPrimitiveComponent::StaticClass()))
	{
		BrushCollisionWarning();
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetOwner()->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	bool bHasCollision = false;
	for (const auto& Comp : PrimitiveComponents)
	{
		if((Comp->GetCollisionEnabled()==ECollisionEnabled::QueryOnly || Comp->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics)
			&& Comp->GetCollisionResponseToChannel(ECollisionChannel::ECC_WorldDynamic)!= ECollisionResponse::ECR_Ignore)
		{
			bHasCollision = true;
			break;
		}
	}
	if (!bHasCollision)
	{
		BrushCollisionWarning();
	}
	GetOwner()->UpdateOverlaps();
}

void UInteractBrush::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	GetWorld()->GetSubsystem<UInteractiveWorldSubsystem>()->UnregisterBrush(this);
	for (const auto InteractVolume : OverlappingInteractVolumes)
	{
		InteractVolume->RemoveBrush(this);
	}
}

void UInteractBrush::UpdateActiveState()
{
	UpdateDrawOnDrawingBoards();
	if (DrawOnDrawingBoards.Num() > 0 && !bBrushActiveInVolume)
	{
		bBrushActiveInVolume = true;
		bSucceededDrawnLastTime = false;
		bSucceededDrawnThisTime = false;
	}
	if (DrawOnDrawingBoards.Num() == 0 && bBrushActiveInVolume)
	{
		bBrushActiveInVolume = false;
		bSucceededDrawnLastTime = false;
		bSucceededDrawnThisTime = false;
	}
}

void UInteractBrush::UpdateDrawOnDrawingBoards()
{
	//Find suitable DrawingBoards bind to InteractVolume
	DrawOnDrawingBoards.Empty();
	if (bUseDrawOnlyDrawingBoardsClassList)
	{
		for (const auto Volume : OverlappingInteractVolumes)
		{
			for (auto DrawingBoard : Volume->GetDrawingBoards())
			{
				if (DrawOnlyDrawingBoardsClassList.Find(DrawingBoard->GetClass()) != -1)
				{
					DrawOnDrawingBoards.AddUnique(DrawingBoard);
				}
			}
		}
	}
	else
	{
		for (const auto Volume : OverlappingInteractVolumes)
		{
			for (auto DrawingBoard : Volume->GetDrawingBoards())
			{
				DrawOnDrawingBoards.AddUnique(DrawingBoard);
			}
		}
	}
}

FBrushWheelData UInteractBrush::CalculateWheelInfo(FTransform CurrentTransform, FTransform PreviousTransform, AWorldDrawingBoard* Board, float CurrentHeight, FVector2D CanvasSize, float
                                               PreviousHeight, float WheelRadius)
{

	FBrushWheelData WheelData;
	
	 WheelData.UVRange=WheelUV(PreviousTransform.Rotator(),CurrentTransform.Rotator());

	
	WheelData. LastLoc=Board->WorldToCanvasUV(FVector2D(PreviousTransform.GetLocation()));
	WheelData. CurrentLoc=Board->WorldToCanvasUV(FVector2D(CurrentTransform.GetLocation()));
	WheelData.CurrentRotation =Board->WorldToCanvasRotation(FRotationMatrix::MakeFromZY(FVector(0,0,1), CurrentTransform.GetRotation().GetForwardVector()).Rotator().Yaw);
	WheelData.LastRotation=Board->WorldToCanvasRotation( FRotationMatrix::MakeFromZY(FVector(0,0,1), PreviousTransform.GetRotation().GetForwardVector()).Rotator().Yaw);
	WheelData.Width=Board->WorldToCanvasSize(Size).X/CanvasSize.X;

	WheelData.LastHeight=(PreviousHeight-WheelRadius)/Board->GetInteractHeight();
	WheelData.CurrentHeight=(CurrentHeight-WheelRadius)/Board->GetInteractHeight();
	
	
	/*const  float CurrentAngleRatio=GetAngleRatio(FVector2D(0,1).GetRotated(CurrentRot));
	const float LastAngleRatio=GetAngleRatio(FVector2D(0,1).GetRotated(LastRot));

	const float X=(LastLoc.X*LastAngleRatio+(CurrentLoc.Y- LastLoc.Y)-CurrentLoc.X*CurrentAngleRatio)/(LastAngleRatio-CurrentAngleRatio);

	const float Y=((X-LastLoc.X)*LastAngleRatio)+LastLoc.Y;
	FVector2D Center=FVector2D(X,Y);


	FVector2D Dir;
	double Len;
	UKismetMathLibrary::ToDirectionAndLength2D(CurrentLoc-LastLoc,Dir,Len);
	const float MoveRotation=FMath::Atan2(Dir.Y,Dir.X)-90;
	const bool Forward=FMath::Fmod( (MoveRotation-CurrentRot+360+180),360.f)-180>0;

	if(!Forward)
	{	const float TempY=UVRange.Y;
		UVRange.Y=UVRange.X;
		UVRange.X=TempY;
	}


	

	UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(),MaterialParameterCollection,"UVDepth",FLinearColor(UVRange.X,UVRange.Y,0,1));


	const FVector2D ScreenPos=(((LastLoc+CurrentRot)/2)-FVector2D(Width,Len)/2)*CanvasSize;
	const FVector2D ScreenSize=FVector2D(Width,Len)*CanvasSize;
	Canvas->K2_DrawMaterial(MaterialInterface,ScreenPos,ScreenSize,FVector2D(0,0),FVector2D(1.f,1.f),(LastRot+CurrentRot/2)+90);#1#*/

	return  WheelData;
}



float UInteractBrush::GetAngleRatio(FVector2D Input)
{
	return Input.Y/Input.X;
}

FVector2D UInteractBrush::WheelUV(FRotator A, FRotator B)
{

	FVector2D Result=FVector2D(A.Roll,B.Roll);
	const float RollDiff=A.Roll-B.Roll;
	const float RollSign=FMath::Sign(RollDiff);
	const bool NeedsInverse=FMath::Abs(RollDiff)>180;
	if(NeedsInverse)
	{
		Result=FVector2D(A.Roll,RollSign*(360-FMath::Abs(RollDiff))+A.Roll);
	}
	return Result/360.f;
}