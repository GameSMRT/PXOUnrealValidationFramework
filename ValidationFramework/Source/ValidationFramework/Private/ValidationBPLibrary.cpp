/**
Copyright 2022 Netflix, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

	http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/


#include "ValidationBPLibrary.h"
#include "DMXProtocolSettings.h"
#include "GeneralEngineSettings.h"
#include "ValidationBase.h"
#include "VFProjectSettingsEditor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/RendererSettings.h"
#include "Interfaces/IPluginManager.h"
#include "EditorLevelLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "FileHelpers.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "TimeManagementBlueprintLibrary.h"
#include "ISettingsEditorModule.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Settings/LevelEditorMiscSettings.h"
#include "Preferences/UnrealEdKeyBindings.h"
#include "Preferences/UnrealEdOptions.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Misc/Paths.h"

#include "Misc/FileHelper.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#if PLATFORM_WINDOWS || PLATFORM_LINUX
#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"
#endif


UValidationBPLibrary::UValidationBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}
TArray<UObject*> UValidationBPLibrary::GetAllValidations()
{
	TArray<UObject*> ValidationObjects;
	const TArray<UObject*> ValidationObjectsCode = GetAllValidationsFromCode();
	const TArray<UObject*> ValidationObjectsBP = GetAllValidationsFromBlueprints();

	ValidationObjects += ValidationObjectsCode;
	ValidationObjects += ValidationObjectsBP;
	return ValidationObjects;
}

TArray<UObject*> UValidationBPLibrary::GetAllValidationsFromCode()
{
	TArray<UObject*> ValidationObjects;
	for(TObjectIterator< UClass > ClassIt; ClassIt; ++ClassIt)
	{
		const UClass* Class = *ClassIt;

		// Only interested in native C++ classes
		if(!Class->IsNative())
		{
			continue;
		}

		// Ignore deprecated
		if(Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}

		// Check this class is a subclass of Base
		if(!Class->IsChildOf(UValidationBase::StaticClass()))
		{
			continue;
		}

		// TODO This is horrible, but the UClass abstract flag check does not work because it also gets inherited so the base class
		// TODO and all the validations which inherit also show as abstract
		if( Class->GetName() == "ValidationBase")
		{
			continue;
		}
		UObject* Object = Class->GetDefaultObject();
		ValidationObjects.Add(Object);
	}
	
	return ValidationObjects;
}

TArray<UObject*> UValidationBPLibrary::GetAllValidationsFromBlueprints()
{
	TArray<UObject*> ValidationObjects;

	// Load the asset registry module
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked< FAssetRegistryModule >(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	// The asset registry is populated asynchronously at startup, so there's no guarantee it has finished.
	// This simple approach just runs a synchronous scan on the entire content directory.
	// Better solutions would be to specify only the path to where the relevant blueprints are,
	// or to register a callback with the asset registry to be notified of when it's finished populating.
	FString PluginContentDir = IPluginManager::Get().FindPlugin(TEXT("ValidationFramework"))->GetContentDir();
	TArray< FString > ContentPaths;
	ContentPaths.Add(TEXT("/Game"));
	//ContentPaths.Add(PluginContentDir);
	AssetRegistry.ScanPathsSynchronous(ContentPaths);

	FTopLevelAssetPath BaseClassName = UValidationBase::StaticClass()->GetClassPathName();

	// Use the asset registry to get the set of all class names deriving from Base
	TSet< FTopLevelAssetPath > DerivedPaths;
	{
		TArray< FTopLevelAssetPath > BaseNames;
		BaseNames.Add(BaseClassName);

		TSet< FTopLevelAssetPath > Excluded;
		AssetRegistry.GetDerivedClassNames(BaseNames, Excluded, DerivedPaths);
	}

	TSet< FName > DerivedNames;
	for(auto const& DerivedPath : DerivedPaths)
	{
		DerivedNames.Add(DerivedPath.GetAssetName());
	}

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray< FAssetData > AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);


	// Iterate over retrieved blueprint assets
	for(auto const& Asset : AssetList)
	{
		// Get the the class this blueprint generates (this is stored as a full path)
		FAssetTagValueRef Result = Asset.TagsAndValues.FindTag(TEXT("GeneratedClass"));
		FAssetRegistryExportPath ExportPath = Result.AsExportPath();
		if(!ExportPath.ToString().IsEmpty())
		{
			// Convert path to just the name part
			const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath(ExportPath.ToString());
			const FString ClassName = FPackageName::ObjectPathToObjectName(ClassObjectPath);
		
			// Check if this class is in the derived set
			if(!DerivedNames.Contains(*ClassName))
			{
				continue;
			}

			FSoftObjectPath SoftPath = FSoftObjectPath(ClassObjectPath);
			TSoftClassPtr<UObject> AssetSubclass = TSoftClassPtr<UObject>(SoftPath);
			UClass* ClassLoaded = AssetSubclass.LoadSynchronous();
			UObject* Object = ClassLoaded->GetDefaultObject();
			ValidationObjects.Add(Object);
		}
	}
	return ValidationObjects;
}

UObject* UValidationBPLibrary::GetValidationFrameworkProjectSettings()
{
	const UVFProjectSettingsEditor* ProjectSettings = GetDefault<UVFProjectSettingsEditor>();
	if (ProjectSettings->ValidationFrameworkSettings != nullptr)
	{
		return ProjectSettings->ValidationFrameworkSettings->GetDefaultObject();
	}

	return nullptr;
}

bool UValidationBPLibrary::GetLensFlareProjectSetting()
{
	const URendererSettings* Settings = GetMutableDefault<URendererSettings>();
	return Settings->bDefaultFeatureLensFlare;
}

void UValidationBPLibrary::SetLensFlareProjectSetting(const bool bValue)
{
	URendererSettings* Settings = GetMutableDefault<URendererSettings>();
	Settings->bDefaultFeatureLensFlare = bValue;
	Settings->SaveConfig();
}

bool UValidationBPLibrary::GetBloomProjectSetting()
{
	const URendererSettings* Settings = GetMutableDefault<URendererSettings>();
	return Settings->bDefaultFeatureBloom;
}

void UValidationBPLibrary::SetBloomProjectSetting(const bool bValue)
{
	URendererSettings* Settings = GetMutableDefault<URendererSettings>();
	Settings->bDefaultFeatureBloom = bValue;
	Settings->SaveConfig();
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessExposureSettings(
		const FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult, const bool IsCamera, const bool PostProcessVolumeInLevel
	)
{
	FixResult = FValidationFixResult();
	FixResult.Result = EValidationFixStatus::Fixed;
	FString Message = "";

	if(!Settings.bOverride_AutoExposureMethod)
	{
		if (!IsCamera)
		{
			FixResult.Result = EValidationFixStatus::Fixed;
			Settings.bOverride_AutoExposureMethod = true;
			Message += "Exposure Method Enabled\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				FixResult.Result = EValidationFixStatus::Fixed;
				Settings.bOverride_AutoExposureMethod = true;
				Message += "Exposure Method Enabled, Needed As No Post Processing Volume In Level\n";
			}
			
		}
	}

	if(Settings.AutoExposureMethod != AEM_Manual)
	{
		if (!IsCamera)
		{
			FixResult.Result = EValidationFixStatus::Fixed;
			Settings.AutoExposureMethod = AEM_Manual;
			Message += "Exposure Method Set To Manual\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				FixResult.Result = EValidationFixStatus::Fixed;
				Settings.AutoExposureMethod = AEM_Manual;
				Message += "Exposure Method Set To Manual, Needed As No Post Processing Volume In Level\n";
			}
		}
	}

	if(!Settings.bOverride_AutoExposureBias)
	{
		if (!IsCamera)
		{
			FixResult.Result = EValidationFixStatus::Fixed;
			Settings.bOverride_AutoExposureBias = true;
			Message += "Exposure Compensation Enabled\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				FixResult.Result = EValidationFixStatus::Fixed;
				Settings.bOverride_AutoExposureBias = true;
				Message += "Exposure Compensation Not Enabled, Needed As No Post Processing Volume In Level\n";
			}
		}
	}

	if(Settings.AutoExposureBias != 0.0)
	{
		if(!IsCamera)
		{
			if (FixResult.Result > EValidationFixStatus::ManualFix)
			{
				FixResult.Result = EValidationFixStatus::ManualFix;
			}
			Message += "Exposure Compensation Not 0.0 This Could Be A Mistake Or Artistic Intent Please Check\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				if (FixResult.Result > EValidationFixStatus::ManualFix)
				{
					FixResult.Result = EValidationFixStatus::ManualFix;
				}
				Message += "Exposure Compensation Not 0.0 This Could Be A Mistake Or Artistic Intent Please Check, Checked As No Post Processing Volume In Level\n";
			}
		}
	}
	
	if (!Settings.bOverride_AutoExposureApplyPhysicalCameraExposure)
	{
		if (!IsCamera)
		{
			FixResult.Result = EValidationFixStatus::Fixed;
			Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			Message += "Apply Physical CameraExposure Enabled\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				FixResult.Result = EValidationFixStatus::Fixed;
				Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
				Message += "Apply Physical CameraExposure Enabled, Needed As No Post Processing Volume In Level\n";
			}
		}
	}

	if (Settings.AutoExposureApplyPhysicalCameraExposure)
	{
		if (!IsCamera)
		{
			FixResult.Result = EValidationFixStatus::Fixed;
			Settings.AutoExposureApplyPhysicalCameraExposure = false;
			Message += "Apply Physical CameraExposure Set To False\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				FixResult.Result = EValidationFixStatus::Fixed;
				Settings.AutoExposureApplyPhysicalCameraExposure = false;
				Message += "Apply Physical CameraExposure Set To False, Needed As No Post Processing Volume In Level\n";
			}
		}
	}
	
	FixResult.Message = ObjectName + "\n" + Message; 

	return Settings;
}

FValidationResult UValidationBPLibrary::ValidatePostProcessExposureSettings(
		const FString ObjectName, const FPostProcessSettings Settings, const bool IsCamera, const bool PostProcessVolumeInLevel
	)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";
	
	if(!Settings.bOverride_AutoExposureMethod)
	{
		if (!IsCamera)
		{
			ValidationResult.Result = EValidationStatus::Fail;
			Message += "Exposure Method Not Enabled\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				ValidationResult.Result = EValidationStatus::Fail;
				Message += "Exposure Method Not Enabled, Needed As No Post Processing Volume In Level\n";
			}
		}
	}

	if(Settings.AutoExposureMethod != AEM_Manual)
	{
		if (!IsCamera)
		{
			ValidationResult.Result = EValidationStatus::Fail;
			Message += "Exposure Method Not Set To Manual\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				ValidationResult.Result = EValidationStatus::Fail;
				Message += "Exposure Method Not Set To Manual, Needed As No Post Processing Volume In Level\n";
			}
		}
	}

	if(!Settings.bOverride_AutoExposureBias)
	{
		if (!IsCamera)
		{
			ValidationResult.Result = EValidationStatus::Fail;
			Message += "Exposure Compensation Not Enabled\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				ValidationResult.Result = EValidationStatus::Fail;
				Message += "Exposure Compensation Not Enabled, Needed As No Post Processing Volume In Level\n";
			}
		}
	}

	if(Settings.AutoExposureBias != 0.0)
	{
		if(!IsCamera)
		{
			if (ValidationResult.Result > EValidationStatus::Warning)
			{
				ValidationResult.Result = EValidationStatus::Warning;
			}
			Message += "Exposure Compensation Not 0.0 This Could Be A Mistake Or Artistic Intent Please Check\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				if (ValidationResult.Result > EValidationStatus::Warning)
				{
					ValidationResult.Result = EValidationStatus::Warning;
				}
				Message += "Exposure Compensation Not 0.0 This Could Be A Mistake Or Artistic Intent Please Check, Checked As No Post Processing Volume In Level\n";
			}
		}
	}
	
	if (!Settings.bOverride_AutoExposureApplyPhysicalCameraExposure)
	{
		if (!IsCamera)
		{
			ValidationResult.Result = EValidationStatus::Fail;
			Message += "Apply Physical CameraExposure Not Enabled\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				ValidationResult.Result = EValidationStatus::Fail;
				Message += "Apply Physical CameraExposure Not Enabled, Needed As No Post Processing Volume In Level\n";
			}
		}
	}

	if (Settings.AutoExposureApplyPhysicalCameraExposure)
	{
		if (!IsCamera)
		{
			ValidationResult.Result = EValidationStatus::Fail;
			Message += "Apply Physical CameraExposure Not Set To False\n";
		}
		else
		{
			if (!PostProcessVolumeInLevel)
			{
				ValidationResult.Result = EValidationStatus::Fail;
				Message += "Apply Physical CameraExposure Not Set To False, Needed As No Post Processing Volume In Level\n";
			}
			
		}
	}

	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	
	return ValidationResult;
}

bool UValidationBPLibrary::MarkCurrentLevelDirty()
{
	bool Success = false;
	UUnrealEditorSubsystem* UnrealEditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();
	const UWorld* World = UnrealEditorSubsystem->GetEditorWorld();
	if (!World)
	{
		
		return Success;
	}
	
	ULevel* Level = World->GetCurrentLevel();
	if (Level)
	{
		Success = Level->MarkPackageDirty();
		
		const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();
		for (ULevelStreaming* LevelStreaming : StreamingLevels)
		{
			if (LevelStreaming != nullptr)
			{
				ULevel* SubLevel = LevelStreaming->GetLoadedLevel();
				if (SubLevel != nullptr)
				{
					Success = SubLevel->MarkPackageDirty();
				}
			}
		}
	}

	
	return Success;
}

bool UValidationBPLibrary::ExcludeMeshFromNDisplayValidation(const UStaticMesh* Mesh)
{
	//TODO: I Do Not Like This, But The Docs For NDisplay say their is a function called GetMeshes(), this does not 
	//TODO: exist in the code, which forces me to use get components which retrieves meshes which are not
	//TODO: part of the nDisplay setup but part of say the camera, or the origin display.
	
	const FString FullName =  Mesh->GetFullName();
	if (FullName.Contains("SM_CineCam"))
	{
		return true;
	}
	
	if (FullName.Contains("sm_nDisplayXform"))
	{
		return true;
	}

	if (FullName.Contains("SM_nDisplayOrigin"))
	{
		return true;
	}

	return false;
}

FValidationResult UValidationBPLibrary::ValidatePostProcessBloomSettings(const FString ObjectName, const FPostProcessSettings Settings)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";

	if (!Settings.bOverride_BloomMethod)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Bloom Method Is Not Enabled\n";
	}

	if (Settings.BloomMethod != BM_SOG)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Bloom Method Is Not Set To Standard\n";
	}

	if (!Settings.bOverride_BloomIntensity)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Bloom Intensity Is Not Enabled\n";
	}

	if (Settings.BloomIntensity != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Bloom Intensity Is Not Set To 0.0\n";
	}

	if (!Settings.bOverride_BloomThreshold)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Bloom Threshold Is Not Enabled\n";
	}

	if (Settings.BloomThreshold != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Bloom Threshold Is Not Set To 0.0\n";
	}

	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	
	return ValidationResult;
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessBloomSettings(FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult)
{
	FixResult.Result = EValidationFixStatus::NotFixed;
	FString Message = "";
	
	if (!Settings.bOverride_BloomMethod)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_BloomMethod = true;
		Message += "Bloom Method Enabled\n";
	}

	if (Settings.BloomMethod != BM_SOG)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.BloomMethod = BM_SOG;
		Message += "Bloom Method Set To Standard\n";
	}

	if (!Settings.bOverride_BloomIntensity)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_BloomIntensity = true;
		Message += "Bloom Intensity Enabled\n";
	}

	if (Settings.BloomIntensity != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.BloomIntensity = 0.0;
		Message += "Bloom Intensity Set To 0.0\n";
	}

	if (!Settings.bOverride_BloomThreshold)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_BloomThreshold = true;
		Message += "Bloom Threshold Is Enabled\n";
	}

	if (Settings.BloomThreshold != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.BloomThreshold = 0.0;
		Message += "Bloom Threshold Set To 0.0\n";
	}

	FixResult.Message = Message;
	
	return Settings;
	
}

FValidationResult UValidationBPLibrary::ValidatePostProcessBlueCorrectionSettings(const FString ObjectName, const FPostProcessSettings Settings)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";

	if (!Settings.bOverride_BlueCorrection)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "BlueCorrection Is Not Enabled\n";
	}

	if (Settings.BlueCorrection != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "BlueCorrection Is Not Set To 0.0\n";
	}

	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	
	return ValidationResult;
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessBlueCorrectionSettings(FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult)
{
	FixResult.Result = EValidationFixStatus::NotFixed;
	FString Message = "";
	
	if (!Settings.bOverride_BlueCorrection)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_BlueCorrection = true;
		Message += "Blue Override Enabled\n";
	}

	if (Settings.BlueCorrection != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.BlueCorrection = 0.0;
		Message += "BlueCorrection Set To 0.0\n";
	}

	FixResult.Message = Message;
	return Settings;
	
}

FValidationResult UValidationBPLibrary::ValidatePostProcessGrainSettings(const FString ObjectName, const FPostProcessSettings Settings)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";

	#if ENGINE_MAJOR_VERSION < 5
	if (!Settings.bOverride_GrainJitter)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Grain Jitter Is Not Enabled\n";
	}

	if (Settings.GrainJitter != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "GrainJitter Is Not Set To 0.0\n";
	}

	if (!Settings.bOverride_GrainIntensity)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Grain Intensity Is Not Enabled\n";
	}

	if (Settings.GrainIntensity != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "GrainIntensity Is Not Set To 0.0\n";
	}
	#else
	if (!Settings.bOverride_FilmGrainIntensity)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Film Grain Intensity Is Not Enabled\n";
	}

	if (Settings.FilmGrainIntensity != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Film Grain Intensity Is Not Set To 0.0\n";
	}
	#endif
	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	return ValidationResult;
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessGrainSettings(FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult)
{
	FixResult.Result = EValidationFixStatus::NotFixed;
	FString Message = "";

	#if ENGINE_MAJOR_VERSION < 5
	if (!Settings.bOverride_GrainIntensity)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_GrainIntensity = true;
		Message += "Grain Intensity Enabled\n";
	}

	if (Settings.GrainIntensity != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.GrainIntensity = 0.0;
		Message += "GrainIntensity Set To 0.0\n";
	}

	if (!Settings.bOverride_GrainJitter)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_GrainJitter = true;
		Message += "Grain Jitter Enabled\n";
	}

	if (Settings.GrainJitter != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.GrainJitter = 0.0;
		Message += "GrainJitter Set To 0.0\n";
	}
	#else
	if (!Settings.bOverride_FilmGrainIntensity)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_FilmGrainIntensity = true;
		Message += "Film Grain Intensity Is Enabled\n";
	}

	if (Settings.FilmGrainIntensity != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.FilmGrainIntensity = 0.0;
		Message += "Film Grain Intensity Set To 0.0\n";
	}
	#endif
	FixResult.Message = Message;
	return Settings;
	
}

FValidationResult UValidationBPLibrary::ValidatePostProcessLensFlareSettings(const FString ObjectName, const FPostProcessSettings Settings)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";

	if (!Settings.bOverride_LensFlareIntensity)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Lens Flare Intensity Is Not Enabled\n";
	}

	if (Settings.LensFlareIntensity != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Lens Flare Intensity Is Not Set To 0.0\n";
	}

	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	
	return ValidationResult;
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessLensFlareSettings(FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult)
{
	FixResult.Result = EValidationFixStatus::NotFixed;
	FString Message = "";

	if (!Settings.bOverride_LensFlareIntensity)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_LensFlareIntensity = true;
		Message += "Lens Flare Intensity Enabled\n";
	}

	if (Settings.LensFlareIntensity != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.LensFlareIntensity = 0.0;
		Message += "Lens Flare Intensity Set To 0.0\n";
	}
	

	FixResult.Message = Message;
	return Settings;
	
}

FValidationResult UValidationBPLibrary::ValidatePostProcessVignetteSettings(const FString ObjectName, const FPostProcessSettings Settings)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";

	if (!Settings.bOverride_VignetteIntensity)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Vignette Intensity Is Not Enabled\n";
	}

	if (Settings.VignetteIntensity != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Vignette Intensity Is Not Set To 0.0\n";
	}

	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	
	return ValidationResult;
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessVignetteSettings(FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult)
{
	FixResult.Result = EValidationFixStatus::NotFixed;
	FString Message = "";

	if (!Settings.bOverride_VignetteIntensity)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_VignetteIntensity = true;
		Message += "Vignette Intensity Enabled\n";
	}

	if (Settings.VignetteIntensity != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.VignetteIntensity = 0.0;
		Message += "Vignette Intensity Set To 0.0\n";
	}
	

	FixResult.Message = Message;
	return Settings;
	
}

FValidationResult UValidationBPLibrary::ValidatePostProcessExpandGamutSettings(const FString ObjectName, const FPostProcessSettings Settings)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";

	if (!Settings.bOverride_ExpandGamut)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Expand Gamut Is Not Enabled\n";
	}
	
	if (Settings.ExpandGamut != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Expand Gamut Is Not Set To 0.0\n";
	}

	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	
	return ValidationResult;
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessExpandGamutSettings(FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult)
{
	FixResult.Result = EValidationFixStatus::NotFixed;
	FString Message = "";

	if (!Settings.bOverride_ExpandGamut)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_ExpandGamut = true;
		Message += "Expand Gamut Enabled\n";
	}

	if (Settings.ExpandGamut != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.ExpandGamut = 0.0;
		Message += "Expand Gamut Set To 0.0\n";
	}
	

	FixResult.Message = Message;
	return Settings;
	
}

FValidationResult UValidationBPLibrary::ValidatePostProcessToneCurveSettings(const FString ObjectName, const FPostProcessSettings Settings)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";

	if (!Settings.bOverride_ToneCurveAmount)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Tone Curve Amount Is Not Enabled\n";
	}

	if (Settings.ToneCurveAmount != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Tone Curve Amount Is Not Set To 0.0\n";
	}

	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	
	return ValidationResult;
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessToneCurveSettings(FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult)
{
	FixResult.Result = EValidationFixStatus::NotFixed;
	FString Message = "";

	if (!Settings.bOverride_ToneCurveAmount)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_ToneCurveAmount = true;
		Message += "Tone Curve Amount Enabled\n";
	}

	if (Settings.ToneCurveAmount != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.ToneCurveAmount = 0.0;
		Message += "Tone Curve Amount Set To 0.0\n";
	}
	

	FixResult.Message = Message;
	return Settings;
}

FValidationResult UValidationBPLibrary::ValidatePostProcessChromaticAberrationSettings(const FString ObjectName, const FPostProcessSettings Settings)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";

	if (Settings.bOverride_SceneFringeIntensity)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Chromatic Aberration Is Enabled\n";
	}

	if (Settings.SceneFringeIntensity != 0.0)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Chromatic Aberration Is Not Set To 0.0\n";
	}

	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	
	return ValidationResult;
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessChromaticAberrationSettings(FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult)
{
	FixResult.Result = EValidationFixStatus::NotFixed;
	FString Message = "";

	if (Settings.bOverride_SceneFringeIntensity)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_SceneFringeIntensity = false;
		Message += "Chromatic Aberration Disabled\n";
	}

	if (Settings.SceneFringeIntensity != 0.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.SceneFringeIntensity = 0.0;
		Message += "Chromatic Aberration Set To 0.0\n";
	}
	
	FixResult.Message = Message;
	return Settings;
}

FValidationResult UValidationBPLibrary::ValidatePostProcessLocalExposureSettings(const FString ObjectName, const FPostProcessSettings Settings)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";
	
	if (Settings.bOverride_LocalExposureHighlightContrastScale || Settings.LocalExposureHighlightContrastScale != 1.0f)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Local Exposure Highlight Contrast Scale Is Enabled Or/& Not Set To 1.0\n";
	}
	
	if (Settings.bOverride_LocalExposureShadowContrastScale || Settings.LocalExposureShadowContrastScale != 1.0f)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Local Exposure Shadow Contrast Scale Is Enabled Or/& Not Set To 1.0\n";
	}
	
	if(Settings.bOverride_LocalExposureDetailStrength || Settings.LocalExposureDetailStrength != 1.0f)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Local Exposure Detail Strength Is Enabled Or/& Not Set To 1.0\n";
	}
	
	if(Settings.bOverride_LocalExposureBlurredLuminanceBlend || Settings.LocalExposureBlurredLuminanceBlend != 0.6f)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Local Exposure Blurred Luminance Blend Is Enabled Or/& Not Set To 0.6\n";
	}
	
	if(Settings.bOverride_LocalExposureBlurredLuminanceKernelSizePercent || Settings.LocalExposureBlurredLuminanceKernelSizePercent != 50.0f)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Local Exposure Blurred Luminance Kernel Size Percent Is Enabled Or/& Not Set To 50.0\n";
	}
	
	if(Settings.bOverride_LocalExposureMiddleGreyBias || Settings.LocalExposureMiddleGreyBias != 0.0f)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		Message += "Local Exposure Middle Grey Bias Is Enabled Or/& Not Set To 0.0\n";
	}

	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationResult.Message = "Valid";
	}
	else
	{
		ValidationResult.Message = ObjectName + "\n" + Message;
	}
	
	return ValidationResult;
}

FPostProcessSettings UValidationBPLibrary::FixPostProcessLocalExposureSettings(FString ObjectName, FPostProcessSettings Settings, FValidationFixResult& FixResult)
{
	FixResult.Result = EValidationFixStatus::NotFixed;
	FString Message = "";

	if (Settings.bOverride_LocalExposureHighlightContrastScale || Settings.LocalExposureHighlightContrastScale != 1.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_LocalExposureHighlightContrastScale = false;
		Settings.LocalExposureHighlightContrastScale = 1.0;
		Message += "Set Exposure Highlight Contrast Scale To Disabled & Set To 1.0\n";
	}
	
	if (Settings.bOverride_LocalExposureShadowContrastScale || Settings.LocalExposureShadowContrastScale != 1.0)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_LocalExposureShadowContrastScale = false;
		Settings.LocalExposureShadowContrastScale = 1.0;
		Message += "Set Local Exposure Shadow Contrast Scale To Disabled & Set To 1.0\n";
	}
	
	if(Settings.bOverride_LocalExposureDetailStrength || Settings.LocalExposureDetailStrength != 1.0f)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_LocalExposureDetailStrength = false;
		Settings.LocalExposureDetailStrength = 1.0;
		Message += "Set Local Exposure Detail Strength To Disabled & Set To 1.0\n";
	}
	
	if(Settings.bOverride_LocalExposureBlurredLuminanceBlend || Settings.LocalExposureBlurredLuminanceBlend != 0.6f)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_LocalExposureBlurredLuminanceBlend = false;
		Settings.LocalExposureBlurredLuminanceBlend = 0.6;
		Message += "Set Local Exposure Blurred Luminance Blend To Disabled & Set To 0.6\n";
	}
	
	if(Settings.bOverride_LocalExposureBlurredLuminanceKernelSizePercent || Settings.LocalExposureBlurredLuminanceKernelSizePercent != 50.0f)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_LocalExposureBlurredLuminanceKernelSizePercent = false;
		Settings.LocalExposureBlurredLuminanceKernelSizePercent = 50.0;
		Message += "Set Local Exposure Blurred Luminance Kernel Size Percent To Disabled & Set To 50.0\n";
	}
	
	if(Settings.bOverride_LocalExposureMiddleGreyBias || Settings.LocalExposureMiddleGreyBias != 0.0f)
	{
		FixResult.Result = EValidationFixStatus::Fixed;
		Settings.bOverride_LocalExposureMiddleGreyBias = false;
		Settings.LocalExposureMiddleGreyBias = 0.0;
		Message += "Set Local Exposure Middle Grey Bias To Disabled & Set To 0.0\n";
	}
	
	FixResult.Message = Message;
	return Settings;
}

FValidationResult UValidationBPLibrary::NDisplayMeshSettingsValidation(
	const UWorld* World,
	const TFunction<EValidationStatus(UStaticMesh* StaticMesh, const int LodIndex, FString& Message)> InputValidationFunction)
{
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	FValidationResult Result = FValidationResult(EValidationStatus::Pass, "Valid");
	FString Message = "";
	
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(World, ADisplayClusterRootActor::StaticClass(), FoundActors);

	for (AActor* FoundActor : FoundActors)
	{
		const ADisplayClusterRootActor* MyActor = Cast<ADisplayClusterRootActor>(FoundActor);
		TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents;
		MyActor->GetComponents(StaticMeshComponents);
		for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
		{
			UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
			const bool ExcludeMesh = ExcludeMeshFromNDisplayValidation(StaticMesh);
			if (ExcludeMesh)
			{
				continue;
			}
			for(int x=0; x<StaticMesh->GetNumLODs(); x++)
			{
				FString ActorErrorMessage;
				const EValidationStatus ActorErrorStatus = InputValidationFunction(StaticMesh, x, ActorErrorMessage);
				if (ActorErrorStatus <= Result.Result)
				{
					Result.Result = ActorErrorStatus;
					Message += ActorErrorMessage;
				}
			}
		}
			
	}

	if (Result.Result != EValidationStatus::Pass)
	{
		Result.Message = "Requires Manual Fix\n" + Message;
	}
	
	return Result;
#endif

#if PLATFORM_MAC
	FValidationResult ValidationResult = FValidationResult();
	ValidationResult.Result = EValidationStatus::Warning;
	ValidationResult.Message = "NDisplay Validations Not Valid On OSX";
	return ValidationResult;
#endif
	
}

bool UValidationBPLibrary::ExportValidationReport(UValidationReportDataTable* ValidationReportDataTable, FString ReportPath, const FString Suffix)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr;
	if (World == nullptr)
	{
		return false;
	}

	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();

	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(World);
	if (ReportPath.IsEmpty())
	{
		
		FString ValidationReportFolder = FPaths::Combine(FPaths::ProjectDir(), TEXT("ValidationReports"));
		FString ValidationLevelReportFolder = FPaths::Combine(ValidationReportFolder, CurrentLevelName);

		
		if (!FileManager.DirectoryExists(*ValidationLevelReportFolder))
		{
			FileManager.CreateDirectory(*ValidationLevelReportFolder);
		}

		ReportPath = FPaths::Combine(ValidationLevelReportFolder, CurrentLevelName);

	}

	if (!Suffix.IsEmpty())
	{
		ReportPath +=  "." + Suffix;
	}
	
	// Export DataTable as json & csv
	FFileHelper::SaveStringToFile(ValidationReportDataTable->GetTableAsJSON(), *(ReportPath + ".json"));
	FFileHelper::SaveStringToFile(ValidationReportDataTable->GetTableAsCSV(), *(ReportPath + ".csv"));
	
	return true;
}

UValidationReportDataTable* UValidationBPLibrary::CreateValidationDataTable()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}

	// Create A New Data Table
	UValidationReportDataTable* ValidationReportDataTable = NewObject<UValidationReportDataTable>(
		World, UValidationReportDataTable::StaticClass(), TEXT("ValidationDataTableReport")
	);

	// Set The Row Structure
	ValidationReportDataTable->RowStruct = Cast<UScriptStruct>(FValidationReportRow::StaticStruct());

	return ValidationReportDataTable;
}

void UValidationBPLibrary::AddValidationResultToReport(UValidationReportDataTable* ValidationReportDataTable, UValidationBase* Validation, FValidationResult ValidationResult)
{
	FValidationReportRow ValidationReportRow = FValidationReportRow();
	ValidationReportRow.Name = Validation->ValidationName;
	if (ValidationResult.Result == EValidationStatus::Pass)
	{
		ValidationReportRow.Result = "Pass";
	}
	if (ValidationResult.Result == EValidationStatus::Warning)
	{
		ValidationReportRow.Result = "Warning";
	}
	if (ValidationResult.Result == EValidationStatus::Fail)
	{
		ValidationReportRow.Result = "Fail";
	}

	ValidationReportRow.Message = ValidationResult.Message;
	ValidationReportRow.Description = Validation->ValidationDescription;
	ValidationReportRow.Fix = Validation->FixDescription;
		
	// Add Report Item As A New Row
	ValidationReportDataTable->AddRow(FName(*ValidationReportRow.Name), ValidationReportRow);
}

bool UValidationBPLibrary::GenerateValidationReport(const FString LevelPath, const EValidationWorkflow Workflow, const FString ReportPath)
{
	bool const LoadResult = FEditorFileUtils::LoadMap(LevelPath, false, false);
	if (!LoadResult)
	{
		return false;
	}

	UValidationReportDataTable* ValidationReportDataTable = CreateValidationDataTable();
	if (!ValidationReportDataTable)
	{
		return false;
	}
	
	TArray<UObject*> Validations = GetAllValidations();
	for (UObject* Item : Validations)
	{
		UValidationBase* Validation = Cast<UValidationBase>(Item);
		if (!Validation->ValidationApplicableWorkflows.Contains(Workflow))
		{
			continue;
		}
		
		const FValidationResult Result = Validation->RunValidation();
		
		AddValidationResultToReport(ValidationReportDataTable, Validation, Result);
	}
	
	return ExportValidationReport(ValidationReportDataTable, ReportPath);
}

FValidationResult UValidationBPLibrary::ValidateSequencesAgainstFrameRate( const UWorld* World, const FFrameRate Rate)
{
	FValidationResult ValidationResult = FValidationResult(EValidationStatus::Pass, "");
	FString Message = "";
	if (!World)
	{
		ValidationResult.Result = EValidationStatus::Fail;
		ValidationResult.Message = "Invalid World Provided";
		return ValidationResult;
	}

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(World, ALevelSequenceActor::StaticClass(), FoundActors);
	for (const auto FoundActor : FoundActors)
	{
		FValidationResult ActorValidationResult = FValidationResult(EValidationStatus::Pass, "");
		const ALevelSequenceActor* LevelSequenceActor = Cast<ALevelSequenceActor>(FoundActor);
		const ULevelSequence* LevelSequence = LevelSequenceActor->GetSequence();
		FFrameRate SequenceRate = LevelSequence->MovieScene->GetDisplayRate();

		const EFrameRateComparisonStatus RateComparison = CompareFrameRateCompatability(SequenceRate, Rate);

		// We check the level sequence display rate vs the project frame rate and if they do not match we fail
		if (RateComparison == EFrameRateComparisonStatus::InValid)
		{
			ActorValidationResult.Result = EValidationStatus::Fail;
			ActorValidationResult.Message = FoundActor->GetName() + " frame rate "
				+ FString::FromInt(SequenceRate.Numerator) + " does not match the project frame rate\n"
				+ FString::FromInt(Rate.Numerator);
			
		}
		if (RateComparison == EFrameRateComparisonStatus::ValidMultiple)
		{
			ActorValidationResult.Result = EValidationStatus::Warning;
			ActorValidationResult.Message = FoundActor->GetName() + " frame rate "
				+ FString::FromInt(SequenceRate.Numerator) + " does not match the project frame rate\n"
				+ FString::FromInt(Rate.Numerator) + " but is a valid multiple, please check this is expected\n";
		}

		if (ActorValidationResult.Result < ValidationResult.Result)
		{
			ValidationResult.Result = ActorValidationResult.Result;
		}
		
		ValidationResult.Message += ActorValidationResult.Message + "\n";
	}

	return ValidationResult;
}

FValidationFixResult UValidationBPLibrary::FixSequencesAgainstFrameRate( const UWorld* World, const FFrameRate Rate)
{

	FValidationFixResult ValidationFixResult = FValidationFixResult(EValidationFixStatus::Fixed, "");
	FString Message = "";
	if (!World)
	{
		ValidationFixResult.Result = EValidationFixStatus::NotFixed;
		ValidationFixResult.Message = "Invalid World Provided\n";
		return ValidationFixResult;
	}

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(World, ALevelSequenceActor::StaticClass(), FoundActors);
	for (const auto FoundActor : FoundActors)
	{
		FValidationFixResult ActorValidationFixResult = FValidationFixResult(EValidationFixStatus::Fixed, "");
		const ALevelSequenceActor* LevelSequenceActor = Cast<ALevelSequenceActor>(FoundActor);
		ULevelSequence* LevelSequence = LevelSequenceActor->GetSequence();
		const FFrameRate SequenceRate = LevelSequence->MovieScene->GetDisplayRate();

		const EFrameRateComparisonStatus RateComparison = CompareFrameRateCompatability(SequenceRate, Rate);
		if (RateComparison == EFrameRateComparisonStatus::InValid)
		{
			ActorValidationFixResult.Result = EValidationFixStatus::NotFixed;
		}

		if (RateComparison == EFrameRateComparisonStatus::ValidMultiple)
		{
			ActorValidationFixResult.Result = EValidationFixStatus::ManualFix;
			ActorValidationFixResult.Message += LevelSequenceActor->GetName() +
				" Frame Rate Should Be Checked Manually";
		}

		// If the sequence is still flagged as not fixed we fix it because we know its not already Fixed and can not be
		// left for manual inspection as its not a valid multiple
		if (ActorValidationFixResult.Result == EValidationFixStatus::NotFixed)
		{
			FString UndoContextName = FString(TEXT(VALIDATION_FRAMEWORK_UNDO_CAT)) + "_FixSequencesAgainstFrameRate";
			GEngine->BeginTransaction(*UndoContextName, FText::FromString("Sets The Display Rate"), LevelSequence);
				LevelSequence->Modify();
				LevelSequence->MovieScene->SetDisplayRate(Rate);
			GEngine->EndTransaction();
			ActorValidationFixResult.Message += LevelSequence->GetName() + " Set Frame Rate To " +
				FString::FromInt(Rate.Numerator);
		}

		if (ActorValidationFixResult.Result < ValidationFixResult.Result)
		{
			ValidationFixResult.Result = ActorValidationFixResult.Result;
		}

		ValidationFixResult.Message += ActorValidationFixResult.Message + "\n";
	}

	return ValidationFixResult;
}

void UValidationBPLibrary::WarnAboutRestart()
{
	ISettingsEditorModule* SettingsEditorModule = FModuleManager::GetModulePtr<ISettingsEditorModule>("SettingsEditor");
	if (SettingsEditorModule)
	{
		SettingsEditorModule->OnApplicationRestartRequired();
	}
}

EFrameRateComparisonStatus UValidationBPLibrary::CompareFrameRateCompatability(FFrameRate FrameRate1, FFrameRate FrameRate2)
{

	if (!FrameRate2.IsValid() || !FrameRate1.IsValid())
	{
		return EFrameRateComparisonStatus::InValid;
	}
	
	if (FrameRate1.Numerator != FrameRate2.Numerator || FrameRate1.Denominator != FrameRate2.Denominator)
	{
		if (UTimeManagementBlueprintLibrary::IsValid_MultipleOf(FrameRate2, FrameRate1))
		{
			return EFrameRateComparisonStatus::ValidMultiple;
		}

		return EFrameRateComparisonStatus::InValid;
	}
	
	return EFrameRateComparisonStatus::Valid;
}

// PXO Additions

bool UValidationBPLibrary::CheckDefaultDMXInputs()
{
#if PLATFORM_WINDOWS
	//const Uw
	//return (Settings->DefaultGraphicsRHI == EDefaultGraphicsRHI::DefaultGraphicsRHI_DX12);
	const UDMXProtocolSettings* Settings = GetMutableDefault<UDMXProtocolSettings>();

	//Settings->InputPortConfigs
	if (Settings->InputPortConfigs.Num() > 0)
	{
		return true;
	}
	else
	{
		return false;
	}
#else
	return false;
#endif
}

bool UValidationBPLibrary::CheckDefaultDMXOutputs()
{
#if PLATFORM_WINDOWS
	//const Uw
	//return (Settings->DefaultGraphicsRHI == EDefaultGraphicsRHI::DefaultGraphicsRHI_DX12);
	const UDMXProtocolSettings* Settings = GetMutableDefault<UDMXProtocolSettings>();

	//Settings->InputPortConfigs
	if (Settings->OutputPortConfigs.Num() > 0)
	{
		return true;
	}
	else
	{
		return false;
	}
#else
	return false;
#endif
}

int32 UValidationBPLibrary::GetAutoSaveEnabled()
{
#if PLATFORM_WINDOWS
	//const Uw
	//return (Settings->DefaultGraphicsRHI == EDefaultGraphicsRHI::DefaultGraphicsRHI_DX12);
	const UEditorLoadingSavingSettings* Settings = GetMutableDefault<UEditorLoadingSavingSettings>();

	return (int32)Settings->bAutoSaveEnable;
#else
	return "none";
#endif
}

int32 UValidationBPLibrary::GetCheckReferences()
{
#if PLATFORM_WINDOWS
	//const Uw
	//return (Settings->DefaultGraphicsRHI == EDefaultGraphicsRHI::DefaultGraphicsRHI_DX12);
	const ULevelEditorMiscSettings* Settings = GetMutableDefault<ULevelEditorMiscSettings >();

	return (int32)Settings->bCheckReferencesOnDelete;
#else
	return "none";
#endif
}

void UValidationBPLibrary::SetAutoSaveEnabled(bool AutoSave)
{
	UEditorLoadingSavingSettings* Settings = GetMutableDefault<UEditorLoadingSavingSettings >();
	Settings->bAutoSaveEnable = (uint32)AutoSave;
	Settings->SaveConfig();
}

void UValidationBPLibrary::SetCheckReferences(bool CheckReferences) 
{
	ULevelEditorMiscSettings* Settings = GetMutableDefault<ULevelEditorMiscSettings >();
	Settings->bCheckReferencesOnDelete = (uint32)CheckReferences;
	Settings->SaveConfig();
}

TArray<FString> UValidationBPLibrary::MyGetStreamingLevels(UObject* WorldContextObject, bool& Success) {
	TArray<FString> ret;
	if (!WorldContextObject) {
		ret.Add(FString(TEXT("Error: No WorldContextObject?")));
		Success = false;
		return ret;
	}
	if (!WorldContextObject->GetWorld()) {
		ret.Add(FString(TEXT("Error: No world object?")));
		Success = false;
		return ret;
	}
	auto streamedLevels = WorldContextObject->GetWorld()->GetStreamingLevels();

	for (ULevelStreaming* lvl : streamedLevels) {
		FString f = lvl->GetWorldAssetPackageName();
		// f is like "/Game/Maps/Notch_Peak_Master", or in the editor "/Game/Maps/UEDPIE_0_Notch_Peak_Master"
		// Chop at last slash in path if any.
		int i;
		if (f.FindLastChar('/', i)) {
			f = f.RightChop(i + 1);
		}
		// Now f is like "UEDPIE_0_Notch_Peak_Master" or "Notch_Peak_Master"
		// In the editor?
		if (f.StartsWith(TEXT("UEDPIE"))) {
			f.RemoveFromStart(TEXT("UEDPIE"));
			// Like "_2_" being the local simulated player number.
			if (f.RemoveFromStart(TEXT("_")) && f.Len() > 2 && f[1] == '_') {
				f.RemoveFromStart(TEXT("0"));
				f.RemoveFromStart(TEXT("1"));
				f.RemoveFromStart(TEXT("2"));
				f.RemoveFromStart(TEXT("3"));
				f.RemoveFromStart(TEXT("4"));
				f.RemoveFromStart(TEXT("5"));
				f.RemoveFromStart(TEXT("6"));
				f.RemoveFromStart(TEXT("7"));
				f.RemoveFromStart(TEXT("8"));
				f.RemoveFromStart(TEXT("_"));
			}
		}

		ret.Add(f);

	}
	Success = true;
	return ret;
}


bool UValidationBPLibrary::GetKeyBindings() {
	/*//const UUnrealEdKeyBindings* Settings = GetMutableDefault<UUnrealEdKeyBindings>();
	//return FString::FromInt(Settings->KeyBindings.Num());
	//return FString::FromInt(UUnrealEdKeyBindings->KeyBindings.Num());
	UUnrealEdOptions* Options = GetMutableDefault<UUnrealEdOptions>();

	if (Options)
	{

		UUnrealEdKeyBindings* KeyBind = Options->EditorKeyBindings;
		//UUnrealEdKeyBindings KeyBind2 = *Options->EditorKeyBindings;


		Options->EditorKeyBindings = NewObject<class UUnrealEdKeyBindings>(Options, UUnrealEdKeyBindings::StaticClass(), NAME_None, RF_Transactional);
		if (KeyBind != nullptr)
		{
			UE_LOG(LogTemp, Log, TEXT("Found Bindings"));
			for (const FEditorKeyBinding Binding : KeyBind->KeyBindings)
			{
				//UE_LOG(LogTemp, Log, TEXT("Command: %s, Key: %s"), *Binding.CommandName.ToString(), *Binding.Key.GetDisplayName().ToString());
				
			}
		}
		else {
			UE_LOG(LogTemp, Log, TEXT("KeyBind Failed"));
		}
	}
	else {
		UE_LOG(LogTemp, Log, TEXT("Options Failed"));
	}*/

	/*
	UUnrealEdOptions* Options = GetMutableDefault<UUnrealEdOptions>();
	Options->SaveConfig();

	FString localPath = FPlatformProcess::UserSettingsDir();
	//string const s = TCHAR_TO_UTF8(*localPath);
	localPath.Append("UnrealEngine/");
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 4
	localPath.Append("5.4");
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 5
	localPath.Append("5.5");
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 6
	localPath.Append("5.6");
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 7
	localPath.Append("5.7");
#endif
*/

	FString localPath = FPaths::EngineDir();
	

	//localPath.Append("5.4");
	//sprintf(localPath, "%d.%d", ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
	localPath.Append("Saved/Config/WindowsEditor/EditorKeyBindings.ini");

	FString file = "";

	FFileHelper::LoadFileToString(file, *localPath);

	if (file != "") {
		UE_LOG(LogTemp, Warning, TEXT("%s loaded"), *FString(localPath));

	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("Could not load %s"), *FString(localPath));
	}

	if (file.Contains("~Quote~LevelEditor~Quote~,~Quote~CommandName~Quote~:~Quote~Save~Quote~,~Quote~ChordIndex~Quote~:0,~Quote~Control~Quote~:false,~Quote~Alt~Quote~:false,~Quote~Shift~Quote~:false,~Quote~Command~Quote~:false,~Quote~Key~Quote~:~Quote~None~Quote~~CloseBracket~")) {
		return true;
	}
	else {
		return false;
	}
}


void UValidationBPLibrary::GetKeyBindings2() {

	
	/*//const UUnrealEdKeyBindings* Settings = GetMutableDefault<UUnrealEdKeyBindings>();
	//return FString::FromInt(Settings->KeyBindings.Num());
	//return FString::FromInt(UUnrealEdKeyBindings->KeyBindings.Num());
	UUnrealEdOptions* Options = GetMutableDefault<UUnrealEdOptions>();

	if (!Options->EditorKeyBindings)
	{
		Options->EditorKeyBindings = NewObject<class UUnrealEdKeyBindings>(Options, UUnrealEdKeyBindings::StaticClass(), NAME_None, RF_Transactional);
	}

	if (Options)
	{

		UUnrealEdKeyBindings* KeyBind = Options->EditorKeyBindings;
		//UUnrealEdKeyBindings KeyBind2 = *Options->EditorKeyBindings;




		if (KeyBind != nullptr)
		{
			UE_LOG(LogTemp, Log, TEXT("Found Bindings"));
			for (const FEditorKeyBinding Binding : KeyBind->KeyBindings)
			{
				//UE_LOG(LogTemp, Log, TEXT("Command: %s, Key: %s"), *Binding.CommandName.ToString(), *Binding.Key.GetDisplayName().ToString());

			}
		}
		else {
			UE_LOG(LogTemp, Log, TEXT("KeyBind Failed"));
		}
	}
	else {
		UE_LOG(LogTemp, Log, TEXT("Options Failed"));
	}*/
}




 

