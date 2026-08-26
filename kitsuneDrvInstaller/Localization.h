#pragma once

enum class UiLanguage
{
	English = 0,
	ChineseSimplified = 1,
	ChineseTraditional = 2
};

enum class TextId
{
	AppTitle,
	ViewTitle,
	Subtitle,
	Language,
	English,
	ChineseSimplified,
	ChineseTraditional,
	DataDirectory,
	Browse,
	ScanHardware,
	SelectMissing,
	InstallSelected,
	ColumnDevice,
	ColumnProvider,
	ColumnVersion,
	ColumnDate,
	ColumnStatus,
	OperationLog,
	BrowseTitle,
	MissingDriver,
	InstalledUpdate,
	InstalledSuccess,
	InstalledReboot,
	InstallFailedStatus,
	LoadingIndex,
	EnumeratingDevices,
	IndexLoadFailed,
	DeviceScanFailed,
	InstallingDrivers,
	DataConfigMissing,
	SelectDriverFirst,
	SevenZipMediaMissing,
	UsingExtractor,
	StartInstall,
	InstallSuccessLog,
	InstallFailureLog,
	RebootSuffix,
	ScanSummaryFormat,
	InstallSummaryFormat,
	ConfigInvalid,
	IndexReadFailed,
	EnumerateFailed,
	ReadDeviceListFailed,
	UnknownError,
	SevenZipNotFound,
	ArchiveMissing,
	ProgramDataFailed,
	CacheCreateFailed,
	Extracting,
	SevenZipLaunchFailed,
	SevenZipExtractFailed,
	InfMissing,
	InstallingInf,
	NewDevLoadFailed,
	UpdateApiUnsupported,
	DriverInstallFailed,
	SystemMismatchFormat,
	VistaSp2Required,
	Win7Sp1Required,
	UnsupportedSystemConfig,
	VersionDetectionFailed,
	UnsupportedSystemTitle,
	UseCompatibleDriverPackage,
	UnsupportedSystemLog,
	InstallCompleteTitle,
	InstallCompleteHeading,
	InstallSuccessCountFormat,
	InstallFailureCountFormat,
	InstallWarningCountFormat,
	RestartToApply,
	AiInstallCountdown,
	AiInstallCancelled,
	AiNoDriverMatchFormat,
	Count
};

class Localization
{
public:
	static UiLanguage GetLanguage();
	static void SetLanguage(UiLanguage language);
	static UiLanguage DetectSystemLanguage();
	static const wchar_t* Text(TextId id);
};

inline const wchar_t* Tr(TextId id) { return Localization::Text(id); }
