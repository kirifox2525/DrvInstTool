#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct PostInstallAction
{
	// Extensible action name. Current executor supports execute, msi, command and install_inf;
	// additional types can be added without changing the index container shape.
	std::wstring type;
	std::wstring file;
	std::wstring arguments;
	std::wstring match;
	std::wstring workingDirectory;
	bool continueOnError = false;
	bool preventReboot = true;
	bool relativeToDriverDirectory = false;
};

struct DriverPackage
{
	std::wstring id;
	std::wstring category;
	std::wstring packageDirectory;
	std::wstring infPath;
	std::wstring archiveFile;
	std::wstring archiveSubdirectory;
	std::wstring driverDate;
	std::wstring driverVersion;
	std::wstring provider;
	std::wstring deviceClass;
	std::vector<PostInstallAction> afterInstallActions;
};

struct DeviceMatch
{
	std::wstring instanceId;
	std::wstring displayName;
	std::wstring hardwareId;
	std::wstring indexedDeviceName;
	DriverPackage driver;
	std::wstring installedDriverProvider;
	std::wstring installedDriverVersion;
	std::wstring installedDriverDate;
	bool needsDriver = false;
	bool usesSystemBuiltInDriver = false;
	bool updateAvailable = false;
};

struct DriverMatchContext
{
	std::wstring currentProvider;
	std::wstring currentDeviceClass;
	std::wstring currentInfName;
	bool compatibleIds = false;
};

class DriverCatalog
{
public:
	struct HardwareRoute
	{
		std::wstring driverId;
		std::wstring deviceName;
		std::vector<std::wstring> candidateDriverIds;
		bool requiresParentBluetoothStackContext = false;
	};

	bool Load(const std::wstring& dataRoot, std::wstring& error);
	bool Match(const std::vector<std::wstring>& hardwareIds, const DriverMatchContext& context,
		DriverPackage& driver,
		std::wstring& matchedHardwareId, std::wstring& indexedDeviceName) const;
	static bool RunMatchingTests(std::wstring& error);
	size_t DriverCount() const { return m_drivers.size(); }
	size_t HardwareIdCount() const { return m_hardwareIds.size(); }
	size_t AfterInstallActionCount() const;

private:
	std::unordered_map<std::wstring, DriverPackage> m_drivers;
	std::unordered_map<std::wstring, HardwareRoute> m_hardwareIds;
};

class DeviceScanner
{
public:
	static bool Scan(const std::wstring& dataRoot, const DriverCatalog& catalog,
		std::vector<DeviceMatch>& matches,
		int& scannedDeviceCount, std::wstring& error);
	static bool RunVersionComparisonTests(std::wstring& error);
};

class SystemCompatibility
{
public:
	static bool GetDriverMediaTarget(const std::wstring& dataRoot, std::wstring& targetSystem,
		std::wstring& targetArchitecture);
	static bool ValidateDriverMedia(const std::wstring& dataRoot, std::wstring& error);
	static bool IsVersionSupported(const std::wstring& targetOs, unsigned long majorVersion,
		unsigned long minorVersion, unsigned long buildNumber, unsigned short servicePackMajor,
		const std::wstring& architecture, std::wstring& requiredVersion, std::wstring& updateHint);
	static bool RunRuleTests(std::wstring& error);
};

class DriverInstaller
{
public:
	using LogCallback = std::function<void(const std::wstring&)>;

	static bool Install(HWND ownerWindow, const std::wstring& dataRoot, const DeviceMatch& match,
		bool& rebootRequired, int& afterInstallWarnings, std::wstring& error,
		const LogCallback& log);
	static std::wstring Find7Zip(const std::wstring& dataRoot);
	static bool RunAfterInstallTests(std::wstring& error);
};
