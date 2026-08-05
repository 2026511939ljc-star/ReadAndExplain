// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonAssetToTextExporter.h"
#include "AssetTextSnapshot.h"

#include "Curves/RealCurve.h"
#include "Engine/CurveTable.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/UnrealType.h"
#include "Engine/DataAsset.h"

namespace CommonAssetTextImpl
{
	static FString BoolText(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	template <typename TEnum>
	static FString EnumText(const TEnum Value)
	{
		if (const UEnum* Enum = StaticEnum<TEnum>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Value));
		}
		return FString::Printf(TEXT("%d"), static_cast<int32>(Value));
	}

	static FString ExportStaticMesh(UStaticMesh* Mesh)
	{
		FString Out;
		Out += TEXT("# Static Mesh Export\n\n");
		Out += TEXT("- Name: `") + Mesh->GetName() + TEXT("`\n");
		Out += TEXT("- ObjectPath: `") + Mesh->GetPathName() + TEXT("`\n");
		Out += FString::Printf(TEXT("- LODCount: %d\n"), Mesh->GetNumLODs());

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		Out += FString::Printf(TEXT("- BoundsOrigin: (%.3f, %.3f, %.3f)\n"), Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z);
		Out += FString::Printf(TEXT("- BoundsExtent: (%.3f, %.3f, %.3f)\n"), Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z);
		Out += FString::Printf(TEXT("- BoundsSphereRadius: %.3f\n\n"), Bounds.SphereRadius);

		Out += TEXT("## Geometry by LOD\n\n");
		Out += TEXT("| LOD | Vertices | Triangles | Sections | UV Channels |\n");
		Out += TEXT("|----:|---------:|----------:|---------:|------------:|\n");
		for (int32 LODIndex = 0; LODIndex < Mesh->GetNumLODs(); ++LODIndex)
		{
			Out += FString::Printf(
				TEXT("| %d | %d | %d | %d | %d |\n"),
				LODIndex,
				Mesh->GetNumVertices(LODIndex),
				Mesh->GetNumTriangles(LODIndex),
				Mesh->GetNumSections(LODIndex),
				Mesh->GetNumUVChannels(LODIndex));
		}
		Out += TEXT("\n## Material Slots\n\n");
		Out += TEXT("| Slot | Slot Name | Imported Name | Material |\n");
		Out += TEXT("|-----:|-----------|---------------|----------|\n");
		const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			const FStaticMaterial& Slot = Materials[Index];
			Out += FString::Printf(
				TEXT("| %d | %s | %s | %s |\n"),
				Index,
				*FAssetTextSnapshot::MarkdownCell(Slot.MaterialSlotName.ToString()),
				*FAssetTextSnapshot::MarkdownCell(Slot.ImportedMaterialSlotName.ToString()),
				*FAssetTextSnapshot::MarkdownCell(Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : TEXT("<none>")));
		}
		if (Materials.Num() == 0)
		{
			Out += TEXT("| 0 | (none) | | |\n");
		}

		Out += TEXT("\n## Collision\n\n");
		if (const UBodySetup* BodySetup = Mesh->GetBodySetup())
		{
			Out += FString::Printf(TEXT("- CollisionTraceFlag: `%d`\n"), static_cast<int32>(BodySetup->CollisionTraceFlag.GetValue()));
			Out += FString::Printf(TEXT("- Boxes: %d\n"), BodySetup->AggGeom.BoxElems.Num());
			Out += FString::Printf(TEXT("- Spheres: %d\n"), BodySetup->AggGeom.SphereElems.Num());
			Out += FString::Printf(TEXT("- Capsules: %d\n"), BodySetup->AggGeom.SphylElems.Num());
			Out += FString::Printf(TEXT("- Convex Hulls: %d\n"), BodySetup->AggGeom.ConvexElems.Num());
		}
		else
		{
			Out += TEXT("- (no BodySetup)\n");
		}
		return Out;
	}

	static FString ExportTexture(UTexture* Texture)
	{
		FString Out;
		Out += TEXT("# Texture Export\n\n");
		Out += TEXT("- Name: `") + Texture->GetName() + TEXT("`\n");
		Out += TEXT("- ObjectPath: `") + Texture->GetPathName() + TEXT("`\n");
		Out += TEXT("- Class: `") + Texture->GetClass()->GetName() + TEXT("`\n");
		if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
		{
			Out += FString::Printf(TEXT("- Resolution: %d x %d\n"), Texture2D->GetSizeX(), Texture2D->GetSizeY());
			Out += FString::Printf(TEXT("- MipCount: %d\n"), Texture2D->GetNumMips());
		}
		Out += TEXT("- CompressionSettings: `") + EnumText(static_cast<TextureCompressionSettings>(Texture->CompressionSettings.GetValue())) + TEXT("`\n");
		Out += TEXT("- LODGroup: `") + EnumText(static_cast<TextureGroup>(Texture->LODGroup.GetValue())) + TEXT("`\n");
		Out += TEXT("- sRGB: ") + BoolText(Texture->SRGB != 0) + TEXT("\n");
		Out += TEXT("- VirtualTextureStreaming: ") + BoolText(Texture->VirtualTextureStreaming != 0) + TEXT("\n");
		Out += TEXT("\n> 本版本只导出贴图元数据，不读取像素或生成缩略图，因此不会增加大贴图导出的等待时间。\n");
		return Out;
	}

	static FString ExportDataTable(UDataTable* DataTable)
	{
		FString Out;
		Out += TEXT("# Data Table Export\n\n");
		Out += TEXT("- Name: `") + DataTable->GetName() + TEXT("`\n");
		Out += TEXT("- ObjectPath: `") + DataTable->GetPathName() + TEXT("`\n");
		Out += TEXT("- RowStruct: `") + (DataTable->GetRowStruct() ? DataTable->GetRowStruct()->GetPathName() : TEXT("<none>")) + TEXT("`\n");
		Out += FString::Printf(TEXT("- RowCount: %d\n\n"), DataTable->GetRowMap().Num());

		const TArray<TArray<FString>> Cells = DataTable->GetTableData();
		if (Cells.Num() == 0)
		{
			Out += TEXT("- (empty table)\n");
			return Out;
		}

		Out += TEXT("## Complete Table\n\n");
		for (int32 RowIndex = 0; RowIndex < Cells.Num(); ++RowIndex)
		{
			Out += TEXT("|");
			for (const FString& Cell : Cells[RowIndex])
			{
				Out += TEXT(" ") + FAssetTextSnapshot::MarkdownCell(Cell) + TEXT(" |");
			}
			Out += TEXT("\n");
			if (RowIndex == 0)
			{
				Out += TEXT("|");
				for (int32 ColumnIndex = 0; ColumnIndex < Cells[RowIndex].Num(); ++ColumnIndex)
				{
					Out += TEXT("---|");
				}
				Out += TEXT("\n");
			}
		}
		return Out;
	}

	static FString ExportCurveTable(UCurveTable* CurveTable)
	{
		FString Out;
		Out += TEXT("# Curve Table Export\n\n");
		Out += TEXT("- Name: `") + CurveTable->GetName() + TEXT("`\n");
		Out += TEXT("- ObjectPath: `") + CurveTable->GetPathName() + TEXT("`\n");
		Out += TEXT("- Mode: `") + EnumText(CurveTable->GetCurveTableMode()) + TEXT("`\n");
		Out += FString::Printf(TEXT("- CurveCount: %d\n\n"), CurveTable->GetRowMap().Num());
		Out += TEXT("| Curve | Keys | Time Range | Value Range |\n");
		Out += TEXT("|-------|-----:|------------|-------------|\n");

		TArray<FName> RowNames;
		CurveTable->GetRowMap().GetKeys(RowNames);
		RowNames.Sort(FNameLexicalLess());
		for (const FName RowName : RowNames)
		{
			const FRealCurve* const* CurvePtr = CurveTable->GetRowMap().Find(RowName);
			const FRealCurve* Curve = CurvePtr ? *CurvePtr : nullptr;
			if (!Curve) continue;
			float MinTime = 0.0f;
			float MaxTime = 0.0f;
			float MinValue = 0.0f;
			float MaxValue = 0.0f;
			Curve->GetTimeRange(MinTime, MaxTime);
			Curve->GetValueRange(MinValue, MaxValue);
			Out += FString::Printf(
				TEXT("| %s | %d | %.6g .. %.6g | %.6g .. %.6g |\n"),
				*FAssetTextSnapshot::MarkdownCell(RowName.ToString()),
				Curve->GetNumKeys(), MinTime, MaxTime, MinValue, MaxValue);
		}
		if (RowNames.Num() == 0)
		{
			Out += TEXT("| (none) | 0 | | |\n");
		}
		return Out;
	}

	static FString ExportEnum(UEnum* Enum)
	{
		FString Out;
		Out += TEXT("# Enum Export\n\n");
		Out += TEXT("- Name: `") + Enum->GetName() + TEXT("`\n");
		Out += TEXT("- ObjectPath: `") + Enum->GetPathName() + TEXT("`\n");
		Out += FString::Printf(TEXT("- EntryCount: %d\n\n"), Enum->NumEnums());
		Out += TEXT("| Index | Name | Value |\n");
		Out += TEXT("|------:|------|------:|\n");
		for (int32 Idx = 0; Idx < Enum->NumEnums(); ++Idx)
		{
			Out += FString::Printf(
				TEXT("| %d | %s | %lld |\n"),
				Idx,
				*FAssetTextSnapshot::MarkdownCell(Enum->GetDisplayNameTextByIndex(Idx).ToString()),
				static_cast<long long>(Enum->GetValueByIndex(Idx)));
		}
		if (Enum->NumEnums() == 0)
		{
			Out += TEXT("| 0 | (none) | |\n");
		}
		return Out;
	}

	static FString ExportDataAsset(UDataAsset* DataAsset)
	{
		FString Out;
		Out += TEXT("# Data Asset Export\n\n");
		Out += TEXT("- Name: `") + DataAsset->GetName() + TEXT("`\n");
		Out += TEXT("- ObjectPath: `") + DataAsset->GetPathName() + TEXT("`\n");
		Out += TEXT("- Class: `") + DataAsset->GetClass()->GetPathName() + TEXT("`\n\n");
		Out += FAssetTextSnapshot::ExportObjectProperties(DataAsset, TEXT("Properties"));
		return Out;
	}
}

bool FCommonAssetToTextExporter::Supports(const UObject* Asset)
{
	return Asset && (Asset->IsA<UStaticMesh>() || Asset->IsA<UTexture>() || Asset->IsA<UDataTable>() || Asset->IsA<UCurveTable>() || Asset->IsA<UEnum>() || Asset->IsA<UDataAsset>());
}

FString FCommonAssetToTextExporter::ExportAssetToText(UObject* Asset)
{
	if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset)) return CommonAssetTextImpl::ExportStaticMesh(Mesh);
	if (UTexture* Texture = Cast<UTexture>(Asset)) return CommonAssetTextImpl::ExportTexture(Texture);
	if (UDataTable* DataTable = Cast<UDataTable>(Asset)) return CommonAssetTextImpl::ExportDataTable(DataTable);
	if (UCurveTable* CurveTable = Cast<UCurveTable>(Asset)) return CommonAssetTextImpl::ExportCurveTable(CurveTable);
	if (UEnum* Enum = Cast<UEnum>(Asset)) return CommonAssetTextImpl::ExportEnum(Enum);
	if (UDataAsset* DataAsset = Cast<UDataAsset>(Asset)) return CommonAssetTextImpl::ExportDataAsset(DataAsset);
	return FString();
}

FString FCommonAssetToTextExporter::GetExportFolderName(const UObject* Asset)
{
	if (Asset && Asset->IsA<UStaticMesh>()) return TEXT("StaticMeshes");
	if (Asset && Asset->IsA<UTexture>()) return TEXT("Textures");
	if (Asset && Asset->IsA<UDataTable>()) return TEXT("DataTables");
	if (Asset && Asset->IsA<UCurveTable>()) return TEXT("CurveTables");
	if (Asset && Asset->IsA<UEnum>()) return TEXT("Enums");
	if (Asset && Asset->IsA<UDataAsset>()) return TEXT("DataAssets");
	return TEXT("OtherAssets");
}

FString FCommonAssetToTextExporter::GetFileSuffix(const UObject* Asset)
{
	if (Asset && Asset->IsA<UStaticMesh>()) return TEXT("_ReadableStaticMesh.md");
	if (Asset && Asset->IsA<UTexture>()) return TEXT("_ReadableTexture.md");
	if (Asset && Asset->IsA<UDataTable>()) return TEXT("_ReadableDataTable.md");
	if (Asset && Asset->IsA<UCurveTable>()) return TEXT("_ReadableCurveTable.md");
	if (Asset && Asset->IsA<UEnum>()) return TEXT("_ReadableEnum.md");
	if (Asset && Asset->IsA<UDataAsset>()) return TEXT("_ReadableDataAsset.md");
	return TEXT("_ReadableAsset.md");
}
