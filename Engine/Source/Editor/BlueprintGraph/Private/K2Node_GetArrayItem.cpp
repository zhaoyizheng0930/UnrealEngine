// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "BlueprintGraphPrivatePCH.h"

#include "SlateBasics.h"
#include "../../../Runtime/Engine/Classes/Kismet/KismetArrayLibrary.h"
#include "ScopedTransaction.h"
#include "KismetCompiler.h"
#include "BlueprintNodeSpawner.h"
#include "EditorCategoryUtils.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "EdGraph/EdGraphNodeUtils.h" // for FNodeTextCache

#define LOCTEXT_NAMESPACE "GetArrayItem"

/*******************************************************************************
*  FKCHandler_GetArrayItem
******************************************************************************/
class FKCHandler_GetArrayItem : public FNodeHandlingFunctor
{
public:
	FKCHandler_GetArrayItem(FKismetCompilerContext& InCompilerContext)
		: FNodeHandlingFunctor(InCompilerContext)
	{
	}

	virtual void RegisterNets(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		UK2Node_GetArrayItem* ArrayNode = CastChecked<UK2Node_GetArrayItem>(Node);

		// return inline term
		if (Context.NetMap.Find(Node->Pins[2]))
		{
			Context.MessageLog.Error(*LOCTEXT("Error_ReturnTermAlreadyRegistered", "ICE: Return term is already registered @@").ToString(), Node);
			return;
		}

		{
			FBPTerminal* Term = new (Context.InlineGeneratedValues) FBPTerminal();
			Term->CopyFromPin(Node->Pins[2], Context.NetNameMap->MakeValidName(Node->Pins[2]));
			Context.NetMap.Add(Node->Pins[2], Term);
		}

		FNodeHandlingFunctor::RegisterNets(Context, Node);
	}

	virtual void Compile(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		UK2Node_GetArrayItem* ArrayNode = CastChecked<UK2Node_GetArrayItem>(Node);

		FBlueprintCompiledStatement* SelectStatementPtr = new FBlueprintCompiledStatement();
		FBlueprintCompiledStatement& ArrayGetFunction = *SelectStatementPtr;
		ArrayGetFunction.Type = KCST_ArrayGetByRef;

		UEdGraphPin* ArrayPinNet = FEdGraphUtilities::GetNetFromPin(Node->Pins[0]);
		UEdGraphPin* ReturnValueNet = FEdGraphUtilities::GetNetFromPin(Node->Pins[2]);

		FBPTerminal** ArrayTerm = Context.NetMap.Find(ArrayPinNet);

		UEdGraphPin* IndexPin = ArrayNode->GetIndexPin();
		UEdGraphPin* IndexPinNet = IndexPin ? FEdGraphUtilities::GetNetFromPin(IndexPin) : nullptr;
		FBPTerminal** IndexTermPtr = IndexPinNet ? Context.NetMap.Find(IndexPinNet) : nullptr;

		FBPTerminal** ReturnValue = Context.NetMap.Find(ReturnValueNet);
		FBPTerminal** ReturnValueOrig = Context.NetMap.Find(Node->Pins[2]);
		ArrayGetFunction.RHS.Add(*ArrayTerm);
		ArrayGetFunction.RHS.Add(*IndexTermPtr);

		(*ReturnValue)->InlineGeneratedParameter = &ArrayGetFunction;
	}
};

/*******************************************************************************
*  UK2Node_GetArrayItem
******************************************************************************/
UK2Node_GetArrayItem::UK2Node_GetArrayItem(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UK2Node_GetArrayItem::AllocateDefaultPins()
{
	// Create the output pin
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, TEXT(""), NULL, true, false, TEXT("Array"));
	UEdGraphPin* IndexPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Int, TEXT(""), NULL, false, false, TEXT("Dimension 1"));
	GetDefault<UEdGraphSchema_K2>()->SetPinDefaultValueBasedOnType(IndexPin);

	// Create the input pins to create the arrays from
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, TEXT(""), NULL, false, true, TEXT("Output"));
}

void UK2Node_GetArrayItem::PostReconstructNode()
{
	if (GetTargetArrayPin()->LinkedTo.Num() > 0)
	{
		PropagatePinType(GetTargetArrayPin()->LinkedTo[0]->PinType);
	}
	else if (GetResultPin()->LinkedTo.Num() > 0)
	{
		PropagatePinType(GetResultPin()->LinkedTo[0]->PinType);
	}
}

FText UK2Node_GetArrayItem::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::FullTitle)
	{
		return LOCTEXT("GetArrayItemByRef_FullTitle", "GET");
	}
	return LOCTEXT("GetArrayItemByRef", "Get");
}

class FNodeHandlingFunctor* UK2Node_GetArrayItem::CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const
{
	return new FKCHandler_GetArrayItem(CompilerContext);
}

void UK2Node_GetArrayItem::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// actions get registered under specific object-keys; the idea is that 
	// actions might have to be updated (or deleted) if their object-key is  
	// mutated (or removed)... here we use the node's class (so if the node 
	// type disappears, then the action should go with it)
	// actions get registered under specific object-keys; the idea is that 
	// actions might have to be updated (or deleted) if their object-key is  
	// mutated (or removed)... here we use the node's class (so if the node 
	// type disappears, then the action should go with it)
	UClass* ActionKey = GetClass();
	// to keep from needlessly instantiating a UBlueprintNodeSpawner, first   
	// check to make sure that the registrar is looking for actions of this type
	// (could be regenerating actions for a specific asset, and therefore the 
	// registrar would only accept actions corresponding to that asset)
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UK2Node_GetArrayItem::NotifyPinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::NotifyPinConnectionListChanged(Pin);

	if (Pin != GetIndexPin() && Pin->ParentPin == nullptr)
	{
		UEdGraphPin* ArrayPin = Pins[0];
		UEdGraphPin* ResultPin = Pins[2];

		// If a pin connection is made, then we need to propagate the change to the other pins and validate all connections, otherwise reset them to Wildcard pins
		if (Pin->LinkedTo.Num() > 0)
		{
			PropagatePinType(Pin->LinkedTo[0]->PinType);
		}
		else if (ArrayPin->LinkedTo.Num() == 0 && ResultPin->LinkedTo.Num() == 0)
		{
			ArrayPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			ArrayPin->PinType.PinSubCategory = TEXT("");
			ArrayPin->PinType.PinSubCategoryObject = NULL;

			ResultPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			ResultPin->PinType.PinSubCategory = TEXT("");
			ResultPin->PinType.PinSubCategoryObject = NULL;
			ResultPin->PinType.bIsReference = true;

			ArrayPin->BreakAllPinLinks();
			ResultPin->BreakAllPinLinks();
		}
	}
}

void UK2Node_GetArrayItem::PropagatePinType(FEdGraphPinType& InType)
{
	UClass const* CallingContext = NULL;
	if (UBlueprint const* Blueprint = GetBlueprint())
	{
		CallingContext = Blueprint->GeneratedClass;
		if (CallingContext == NULL)
		{
			CallingContext = Blueprint->ParentClass;
		}
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UEdGraphPin* ArrayPin = Pins[0];
	UEdGraphPin* ResultPin = Pins[2];

	ArrayPin->PinType = InType;
	ArrayPin->PinType.bIsArray = true;
	ArrayPin->PinType.bIsReference = false;

	ResultPin->PinType = InType;
	ResultPin->PinType.bIsArray = false;
	ResultPin->PinType.bIsReference = !(ResultPin->PinType.PinCategory == Schema->PC_Object || ResultPin->PinType.PinCategory == Schema->PC_Class || ResultPin->PinType.PinCategory == Schema->PC_Asset || ResultPin->PinType.PinCategory == Schema->PC_AssetClass || ResultPin->PinType.PinCategory == Schema->PC_Interface);
	ResultPin->PinType.bIsConst = false;


	// Verify that all previous connections to this pin are still valid with the new type
	for (TArray<UEdGraphPin*>::TIterator ConnectionIt(ArrayPin->LinkedTo); ConnectionIt; ++ConnectionIt)
	{
		UEdGraphPin* ConnectedPin = *ConnectionIt;
		if (!Schema->ArePinsCompatible(ArrayPin, ConnectedPin, CallingContext))
		{
			ArrayPin->BreakLinkTo(ConnectedPin);
		}
	}

	// Verify that all previous connections to this pin are still valid with the new type
	for (TArray<UEdGraphPin*>::TIterator ConnectionIt(ResultPin->LinkedTo); ConnectionIt; ++ConnectionIt)
	{
		UEdGraphPin* ConnectedPin = *ConnectionIt;
		if (!Schema->ArePinsCompatible(ResultPin, ConnectedPin, CallingContext))
		{
			ResultPin->BreakLinkTo(ConnectedPin);
		}
	}
}

#undef LOCTEXT_NAMESPACE