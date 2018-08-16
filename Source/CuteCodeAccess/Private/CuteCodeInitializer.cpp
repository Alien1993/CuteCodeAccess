#include "CuteCodeInitializer.h"
#include "CuteCodeConstants.h"
#include "CuteCodeEditorSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Runtime/XmlParser/Public/FastXml.h"
#include "Windows/WindowsPlatformMisc.h"

DEFINE_LOG_CATEGORY_STATIC(LogCuteCodeInitializer, Log, All);

#define LOCTEXT_NAMESPACE "FCuteCodeInitializer"

FCuteCodeInitializer::FCuteCodeInitializer(const FString& SolutionPath, const FString& ProjectName)
	: SolutionPath{SolutionPath}
	, ProjectName{ProjectName}
	, VCProjXmlCallback{new FCuteCodeVCProjXmlCallback}
{
	FText OutErrorMessage;
	int32 OutErrorLineNumber = -1;

	FString VcxProjFile = FPaths::Combine(
		SolutionPath, 
		INTERMEDIATE_PROJECTFILES,
		ProjectName + TEXT(".vcxproj")
	);

	FFastXml::ParseXmlFile(
		VCProjXmlCallback,
		*VcxProjFile,
		TEXT(""),
		nullptr,
		false,
		false,
		OutErrorMessage,
		OutErrorLineNumber
	);

	if (!OutErrorMessage.IsEmpty())
	{
		UE_LOG(LogCuteCodeInitializer, Error, TEXT("Error parsing .vcxproj file at line: %d %s"),
			OutErrorLineNumber, *OutErrorMessage.ToString());
	}
}

FCuteCodeInitializer::~FCuteCodeInitializer()
{
	delete VCProjXmlCallback;
}

void FCuteCodeInitializer::Run() const
{
	CreateProFile();
	CreatePriFiles();
	CreateProUserFile();
}

void FCuteCodeInitializer::CreateProFile() const
{
	TArray<FString> ProFileLines{{
		"TEMPLATE = app",
		"",
		"CONFIG += console c++11",
		"CONFIG -= app_bundle qt",
		"",
		"include(defines.pri)",
		"include(includes.pri)",
		""
	}};

	ProFileLines.Add("HEADERS += \\");
	AppendFormattedStrings(ProFileLines, "{0} \\", VCProjXmlCallback->GetHeaders());
	ProFileLines.Pop();
	ProFileLines.Add(FString::Format(TEXT("{0}"), { VCProjXmlCallback->GetHeaders().Last() }));

	ProFileLines.Add("");

	ProFileLines.Add("SOURCES += \\");
	AppendFormattedStrings(ProFileLines, "{0} \\", VCProjXmlCallback->GetSources());
	ProFileLines.Pop();
	ProFileLines.Add(FString::Format(TEXT("{0}"), { VCProjXmlCallback->GetSources().Last() }));

	FString ProFilePath = FPaths::Combine(
		SolutionPath,
		INTERMEDIATE_PROJECTFILES,
		ProjectName + ".pro"
	);

	FFileHelper::SaveStringArrayToFile(ProFileLines, *ProFilePath);
}

void FCuteCodeInitializer::CreatePriFiles() const
{
	// Creates defines.pri
	TArray<FString> DefinesPriLines{{
		"######################################################################",
		"########## This file has been generated by CuteCodeAccess ############",
		"######################################################################"
	}};

	TArray<FString> Defines;
	VCProjXmlCallback->GetDefines().ParseIntoArray(Defines, TEXT(";"), true);

	DefinesPriLines.Add("DEFINES += \\");
	AppendFormattedStrings(DefinesPriLines, "\"{0}\" \\", Defines);
	DefinesPriLines.Pop();
	DefinesPriLines.Add(FString::Format(TEXT("\"{0}\""), { Defines.Last() }));

	FString DefinesPriFilePath = FPaths::Combine(
		SolutionPath,
		INTERMEDIATE_PROJECTFILES,
		FString{"defines.pri"}
	);

	FFileHelper::SaveStringArrayToFile(DefinesPriLines, *DefinesPriFilePath);

	// Creates includes.pri
	TArray<FString> IncludesPriLines{{
		"######################################################################",
		"########## This file has been generated by CuteCodeAccess ############",
		"######################################################################"
	}};

	TArray<FString> Includes;
	VCProjXmlCallback->GetIncludes().ParseIntoArray(Includes, TEXT(";"), true);

	IncludesPriLines.Add("INCLUDEPATH += \\");
	AppendFormattedStrings(IncludesPriLines, "\"{0}\" \\", Includes);
	IncludesPriLines.Pop();
	IncludesPriLines.Add(FString::Format(TEXT("\"{0}\""), { Includes.Last() }));

	FString IncludePriFilePaths = FPaths::Combine(
		SolutionPath,
		INTERMEDIATE_PROJECTFILES,
		FString{"includes.pri"}
	);

	FFileHelper::SaveStringArrayToFile(IncludesPriLines, *IncludePriFilePaths);
}

void FCuteCodeInitializer::CreateProUserFile() const
{
	const UCuteCodeEditorSettings* Settings = GetDefault<UCuteCodeEditorSettings>();

	if (Settings)
	{
		if (Settings->UnrealKitName.IsEmpty())
		{
			UE_LOG(LogCuteCodeInitializer, Error,
				TEXT("Unreal kit name must be set to create project files correctly"));
			return;
		}

		// Gets current user Roaming folder to find Qt Creator configurations
		int32 EnvVarLen = 512;
		TCHAR* EnvVar = new TCHAR[EnvVarLen];
		FWindowsPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"), EnvVar, EnvVarLen);

		FString RoamingDirectory = FString{ EnvVar };

		FString QtCreatorProfileXmlFile = FPaths::Combine(
			FString{ EnvVar },
			FString{ "QtProject/qtcreator/profiles.xml" }
		);

		FPaths::NormalizeDirectoryName(QtCreatorProfileXmlFile);

		if (FPaths::FileExists(QtCreatorProfileXmlFile))
		{
			// TODO: Here parse xml and find configuration uuid
			FCuteCodeProfilesXmlCallback ProfileXmlCallback{};

			FText OutErrorMessage;
			int32 OutErrorLineNumber = -1;

			// Reads profile.xml
			TArray<FString> QtCreatorProfileXmlLines;
			FFileHelper::LoadFileToStringArray(
				QtCreatorProfileXmlLines,
				*QtCreatorProfileXmlFile
			);

			// Removes first 3 lines from profile.xml, because FastXml doesn't
			// parse them correctly and messes up the rest of the file
			QtCreatorProfileXmlLines.RemoveAt(0, 3);

			FString JoinedLines = FString::Join(QtCreatorProfileXmlLines, TEXT("\n"));

			FFastXml::ParseXmlFile(
				&ProfileXmlCallback,
				TEXT(""),
				&JoinedLines[0],
				nullptr,
				false,
				false,
				OutErrorMessage,
				OutErrorLineNumber
			);

			UE_LOG(LogCuteCodeInitializer, Error, TEXT("uuid: %s"), *ProfileXmlCallback.GetKitUuid());

			if (!OutErrorMessage.IsEmpty()
				&& OutErrorMessage.ToString() != "User aborted the parsing process")
			{
				UE_LOG(LogCuteCodeInitializer, Error, TEXT("Error parsing profiles.xml file at line: %d %s"),
					OutErrorLineNumber, *OutErrorMessage.ToString());
			}
		}
		else
		{
			UE_LOG(LogCuteCodeInitializer, Error, TEXT("\"%s\" not found"), *QtCreatorProfileXmlFile);
		}
	}
}

void FCuteCodeInitializer::AppendFormattedStrings(TArray<FString>& OutArray, const FString& Formatter, const TArray<FString>& ToAppend) const
{
	for (int32 i = 0; i < ToAppend.Num(); i++)
	{
		OutArray.Add(FString::Format(*Formatter, { ToAppend[i] }));
	}
}

#undef LOCTEXT_NAMESPACE
