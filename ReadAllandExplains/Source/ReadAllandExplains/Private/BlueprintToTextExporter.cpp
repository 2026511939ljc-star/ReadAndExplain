// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintToTextExporter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "EdGraphSchema_K2.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/Blueprint.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "BlueprintToTextExporter"

namespace
{
	/** 是否为 exec 引脚（执行流） */
	bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	/** 获取节点的所有“执行输出”引脚（用于继续往下走），按引脚名排序以保证顺序稳定 */
	void GetExecOutputPins(UEdGraphNode* Node, TArray<UEdGraphPin*>& OutPins)
	{
		OutPins.Reset();
		if (!Node) return;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			if (!IsExecPin(Pin)) continue;
			OutPins.Add(Pin);
		}
		// 排序：Then 0, Then 1, ... True, False 等顺序稳定
		OutPins.Sort([](const UEdGraphPin& A, const UEdGraphPin& B) { return A.PinName.Compare(B.PinName) < 0; });
	}

	/** 获取通过 exec 连接到的下一个节点（可能有多个，如 Branch 的 True/False） */
	void GetNextNodesByExec(UEdGraphPin* ExecOutPin, TArray<UEdGraphNode*>& OutNodes)
	{
		OutNodes.Reset();
		if (!ExecOutPin) return;
		for (UEdGraphPin* Linked : ExecOutPin->LinkedTo)
		{
			if (!Linked) continue;
			UEdGraphNode* Next = Linked->GetOwningNode();
			if (Next) OutNodes.AddUnique(Next);
		}
	}

	/** 判断节点是否为“执行入口”（无 exec 输入或 exec 输入未连接） */
	bool IsExecutionRoot(UEdGraphNode* Node)
	{
		if (!Node) return false;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			if (!IsExecPin(Pin)) continue;
			// 有 exec 输入且已连接 -> 不是根
			if (Pin->LinkedTo.Num() > 0) return false;
		}
		// 没有 exec 输入，或 exec 输入未连接 -> 可作为根（如 Event、FunctionEntry）
		return true;
	}

	/** 判断是否为“事件”或“函数入口”等天然根节点 */
	bool IsNaturalExecutionRoot(UEdGraphNode* Node)
	{
		if (!Node) return false;
		if (Cast<UK2Node_Event>(Node)) return true;
		if (Cast<UK2Node_FunctionEntry>(Node)) return true;
		return false;
	}
}

FString FBlueprintToTextExporter::ExportBlueprintToText(UBlueprint* Blueprint)
{
	if (!Blueprint)
		return FString();

	FString ClassName = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetName() : (Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass->GetName() : TEXT("?"));

	FString Out;
	Out += TEXT("# Blueprint Logic Export (for AI / C++ reference)\n");
	Out += TEXT("# Blueprint: ") + Blueprint->GetName() + TEXT("\n");
	Out += TEXT("# Class: ") + ClassName + TEXT("\n\n");
	Out += TEXT("---\n");
	Out += TEXT("## 使用说明\n");
	Out += TEXT("- 下文按「执行顺序」组织：从事件/函数入口沿 exec 连线展开，便于理解“先做什么、再做什么、在什么条件下分支”\n");
	Out += TEXT("- 每个节点下方 `// 数据:` 列出关键输入的数据来源，便于对照 C++ 时的参数与条件\n");
	Out += TEXT("- 可作为答辩时讲述蓝图逻辑的提纲，或编写等价 C++ 的参考\n\n");

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph)
		{
			FString GraphName = Graph->GetName();
			if (GraphName.IsEmpty()) GraphName = TEXT("EventGraph");
			Out += TEXT("---\n\n## 图: ") + GraphName + TEXT("\n\n");
			Out += ExportGraphToText(Graph, GraphName);
			Out += TEXT("\n");
		}
	}

	for (int32 i = 0; i < Blueprint->FunctionGraphs.Num(); i++)
	{
		UEdGraph* Graph = Blueprint->FunctionGraphs[i];
		if (Graph)
		{
			Out += TEXT("---\n\n## 函数: ") + Graph->GetName() + TEXT("\n\n");
			Out += ExportGraphToText(Graph, Graph->GetName());
			Out += TEXT("\n");
		}
	}

	return Out;
}

FString FBlueprintToTextExporter::ExportGraphToText(UEdGraph* Graph, const FString& GraphName)
{
	if (!Graph) return FString();

	// 找出所有“执行入口”节点（事件、函数入口，或 exec 输入未连接的节点）
	TArray<UEdGraphNode*> Roots;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (!IsNaturalExecutionRoot(Node)) continue;
		Roots.Add(Node);
	}
	// 若没有天然根，则用“无 exec 输入连接”的节点作为根（兜底）
	if (Roots.Num() == 0)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (IsExecutionRoot(Node)) Roots.AddUnique(Node);
		}
	}

	TSet<UEdGraphNode*> Visited;
	FString Out;
	int32 IndentLevel = 0;
	const TCHAR* IndentUnit = TEXT("  ");

	auto AppendIndent = [&]() -> FString
	{
		FString s;
		for (int32 i = 0; i < IndentLevel; i++) s += IndentUnit;
		return s;
	};

	// 对每个入口做 DFS，沿 exec 输出遍历
	for (UEdGraphNode* Root : Roots)
	{
		TArray<UEdGraphNode*> Stack;
		TArray<int32> IndentStack;
		Stack.Push(Root);
		IndentStack.Push(0);

		while (Stack.Num() > 0)
		{
			UEdGraphNode* Node = Stack.Pop();
			int32 CurIndent = IndentStack.Pop();
			IndentLevel = CurIndent;

			if (Visited.Contains(Node)) continue;
			Visited.Add(Node);

			FString Line = ExportNodeToText(Node);
			if (!Line.IsEmpty())
			{
				Out += AppendIndent() + Line + TEXT("\n");

				// 数据流：列出有连接且非 exec 的输入引脚及其来源
				TArray<FString> DataLines;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() == 0) continue;
					if (IsExecPin(Pin)) continue;
					UEdGraphPin* FromPin = Pin->LinkedTo[0];
					if (!FromPin) continue;
					UEdGraphNode* FromNode = FromPin->GetOwningNode();
					if (!FromNode) continue;
					FString FromLabel = GetNodeShortLabel(FromNode);
					DataLines.Add(FString::Printf(TEXT("%s = %s.%s"), *Pin->PinName.ToString(), *FromLabel, *FromPin->PinName.ToString()));
				}
				for (const FString& D : DataLines)
					Out += AppendIndent() + TEXT("// 数据: ") + D + TEXT("\n");
			}

			// 根据节点类型决定“下一步”顺序
			TArray<UEdGraphPin*> ExecOuts;
			GetExecOutputPins(Node, ExecOuts);

			UK2Node_IfThenElse* BranchNode = Cast<UK2Node_IfThenElse>(Node);
			UK2Node_ExecutionSequence* SeqNode = Cast<UK2Node_ExecutionSequence>(Node);

			if (BranchNode && ExecOuts.Num() >= 2)
			{
				// Branch: 先 True 再 False，输出成 if/else 结构（UE5.7 无 PN_True/PN_False，用引脚名查找）
				UEdGraphPin* PinTrue = Node->FindPin(FName(TEXT("True")), EGPD_Output);
				UEdGraphPin* PinFalse = Node->FindPin(FName(TEXT("False")), EGPD_Output);
				if (!PinTrue || !PinFalse)
				{
					// 兜底：按引脚顺序（通常 True 在前）
					PinTrue = ExecOuts.Num() > 0 ? ExecOuts[0] : nullptr;
					PinFalse = ExecOuts.Num() > 1 ? ExecOuts[1] : nullptr;
				}
				if (PinTrue)
				{
					Out += AppendIndent() + TEXT("// if (condition above) then:\n");
					TArray<UEdGraphNode*> NextTrue;
					GetNextNodesByExec(PinTrue, NextTrue);
					for (int32 i = NextTrue.Num() - 1; i >= 0; i--)
					{
						Stack.Push(NextTrue[i]);
						IndentStack.Push(CurIndent + 1);
					}
				}
				if (PinFalse)
				{
					Out += AppendIndent() + TEXT("// else:\n");
					TArray<UEdGraphNode*> NextFalse;
					GetNextNodesByExec(PinFalse, NextFalse);
					for (int32 i = NextFalse.Num() - 1; i >= 0; i--)
					{
						Stack.Push(NextFalse[i]);
						IndentStack.Push(CurIndent + 1);
					}
				}
				continue;
			}

			if (SeqNode)
			{
				// Sequence: 按 Then 0, Then 1, ... 顺序压栈（逆序压入以便按顺序弹出）
				TArray<UEdGraphPin*> ThenPins;
				for (UEdGraphPin* P : ExecOuts)
				{
					if (P && P->PinName.ToString().StartsWith(TEXT("Then")))
						ThenPins.Add(P);
				}
				ThenPins.Sort([](const UEdGraphPin& A, const UEdGraphPin& B) { return A.PinName.Compare(B.PinName) < 0; });
				for (int32 i = ThenPins.Num() - 1; i >= 0; i--)
				{
					TArray<UEdGraphNode*> Next;
					GetNextNodesByExec(ThenPins[i], Next);
					for (int32 j = Next.Num() - 1; j >= 0; j--)
					{
						Stack.Push(Next[j]);
						IndentStack.Push(CurIndent + 1);
					}
				}
				continue;
			}

			// 默认：按 exec 输出引脚顺序把下一批节点压栈（逆序以保持显示顺序）
			for (int32 i = ExecOuts.Num() - 1; i >= 0; i--)
			{
				TArray<UEdGraphNode*> Next;
				GetNextNodesByExec(ExecOuts[i], Next);
				for (int32 j = Next.Num() - 1; j >= 0; j--)
				{
					Stack.Push(Next[j]);
					IndentStack.Push(CurIndent + 1);
				}
			}
		}
	}

	// 未出现在任何执行链中的节点（孤立或仅数据连接）：列在末尾作为参考
	TArray<UEdGraphNode*> OrphanNodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || Visited.Contains(Node)) continue;
		OrphanNodes.Add(Node);
	}
	if (OrphanNodes.Num() > 0)
	{
		Out += TEXT("\n// --- 未在 exec 链中出现的节点（可能仅作数据或子图） ---\n");
		for (UEdGraphNode* Node : OrphanNodes)
		{
			FString Line = ExportNodeToText(Node);
			if (!Line.IsEmpty())
				Out += TEXT("// ") + Line + TEXT("\n");
		}
	}

	return Out;
}

FString FBlueprintToTextExporter::GetNodeShortLabel(UEdGraphNode* Node)
{
	if (!Node) return TEXT("?");
	FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	if (Title.IsEmpty()) Title = Node->GetName();

	if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
		return FString::Printf(TEXT("Event_%s"), *EventNode->GetFunctionName().ToString());
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		UFunction* Func = CallNode->GetTargetFunction();
		return FString::Printf(TEXT("Call_%s"), Func ? *Func->GetName() : TEXT("?"));
	}
	if (Cast<UK2Node_IfThenElse>(Node)) return TEXT("Branch");
	if (Cast<UK2Node_ExecutionSequence>(Node)) return TEXT("Sequence");
	if (UK2Node_VariableGet* GetVar = Cast<UK2Node_VariableGet>(Node))
	{
		FProperty* Prop = static_cast<UK2Node_Variable*>(Node)->GetPropertyForVariable();
		return FString::Printf(TEXT("Get_%s"), Prop ? *Prop->GetName() : *Title);
	}
	if (UK2Node_VariableSet* SetVar = Cast<UK2Node_VariableSet>(Node))
	{
		FProperty* Prop = static_cast<UK2Node_Variable*>(Node)->GetPropertyForVariable();
		return FString::Printf(TEXT("Set_%s"), Prop ? *Prop->GetName() : *Title);
	}
	if (Cast<UK2Node_FunctionEntry>(Node)) return TEXT("Entry");
	if (Cast<UK2Node_FunctionResult>(Node)) return TEXT("Return");
	// 简短化：用类名 + 标题前几个字
	FString ClassName = Node->GetClass()->GetName();
	return ClassName + TEXT("_") + Title.Left(20);
}

FString FBlueprintToTextExporter::ExportNodeToText(UEdGraphNode* Node)
{
	if (!Node) return FString();

	UK2Node* K2Node = Cast<UK2Node>(Node);
	FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	if (Title.IsEmpty()) Title = Node->GetName();

	if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
		return FString::Printf(TEXT("Event: %s"), *EventNode->GetFunctionName().ToString());
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		UFunction* Func = CallNode->GetTargetFunction();
		return FString::Printf(TEXT("Call: %s"), Func ? *Func->GetName() : TEXT("?"));
	}
	if (Cast<UK2Node_IfThenElse>(Node))
		return TEXT("Branch ( condition ) -> True / False");
	if (Cast<UK2Node_VariableGet>(Node))
	{
		FProperty* Prop = static_cast<UK2Node_Variable*>(Node)->GetPropertyForVariable();
		FString VarName = Prop ? Prop->GetName() : Title;
		return FString::Printf(TEXT("Get Variable: %s"), *VarName);
	}
	if (Cast<UK2Node_VariableSet>(Node))
	{
		FProperty* Prop = static_cast<UK2Node_Variable*>(Node)->GetPropertyForVariable();
		FString VarName = Prop ? Prop->GetName() : Title;
		return FString::Printf(TEXT("Set Variable: %s"), *VarName);
	}
	if (Cast<UK2Node_ExecutionSequence>(Node))
		return TEXT("Sequence ( then 0, then 1, ... )");
	if (Cast<UK2Node_FunctionEntry>(Node))
		return FString::Printf(TEXT("FunctionEntry: %s"), *Title);
	if (Cast<UK2Node_FunctionResult>(Node))
		return TEXT("Return");
	if (K2Node)
		return FString::Printf(TEXT("[%s] %s"), *Node->GetClass()->GetName(), *Title);
	return FString::Printf(TEXT("[Node] %s"), *Title);
}

#undef LOCTEXT_NAMESPACE
