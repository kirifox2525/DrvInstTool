#include "pch.h"
#include "DriverEngine.h"
#include "Localization.h"

#include <SetupAPI.h>
#include <newdev.h>
#include <ShlObj.h>
#include <algorithm>
#include <climits>
#include <cwctype>
#include <fstream>
#include <sstream>

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Shell32.lib")

namespace
{
	std::wstring Utf8ToWide(const std::string& value)
	{
		if (value.empty())
			return {};
		const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (count <= 0)
			return {};
		std::wstring result(static_cast<size_t>(count), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &result[0], count);
		return result;
	}

	void AppendUtf8(std::string& output, unsigned int codePoint)
	{
		if (codePoint <= 0x7f)
			output.push_back(static_cast<char>(codePoint));
		else if (codePoint <= 0x7ff)
		{
			output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
			output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
		}
		else if (codePoint <= 0xffff)
		{
			output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
			output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
		}
		else
		{
			output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
			output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
		}
	}

	class JsonReader
	{
	public:
		explicit JsonReader(const std::string& text) : m_begin(text.data()), m_current(text.data()), m_end(text.data() + text.size()) {}

		void SkipSpace()
		{
			while (m_current < m_end && (*m_current == ' ' || *m_current == '\t' || *m_current == '\r' || *m_current == '\n'))
				++m_current;
		}

		bool Consume(char ch)
		{
			SkipSpace();
			if (m_current >= m_end || *m_current != ch)
				return false;
			++m_current;
			return true;
		}

		bool ParseString(std::string& output)
		{
			SkipSpace();
			if (m_current >= m_end || *m_current != '"')
				return false;
			++m_current;
			output.clear();
			while (m_current < m_end)
			{
				const unsigned char ch = static_cast<unsigned char>(*m_current++);
				if (ch == '"')
					return true;
				if (ch != '\\')
				{
					output.push_back(static_cast<char>(ch));
					continue;
				}
				if (m_current >= m_end)
					return false;
				switch (*m_current++)
				{
				case '"': output.push_back('"'); break;
				case '\\': output.push_back('\\'); break;
				case '/': output.push_back('/'); break;
				case 'b': output.push_back('\b'); break;
				case 'f': output.push_back('\f'); break;
				case 'n': output.push_back('\n'); break;
				case 'r': output.push_back('\r'); break;
				case 't': output.push_back('\t'); break;
				case 'u':
				{
					unsigned int codePoint = 0;
					if (!ParseHex4(codePoint))
						return false;
					if (codePoint >= 0xd800 && codePoint <= 0xdbff && m_end - m_current >= 6 && m_current[0] == '\\' && m_current[1] == 'u')
					{
						m_current += 2;
						unsigned int low = 0;
						if (!ParseHex4(low) || low < 0xdc00 || low > 0xdfff)
							return false;
						codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
					}
					AppendUtf8(output, codePoint);
					break;
				}
				default: return false;
				}
			}
			return false;
		}

		bool ParseBoolean(bool& output)
		{
			SkipSpace();
			if (m_end - m_current >= 4 && strncmp(m_current, "true", 4) == 0)
			{
				m_current += 4;
				output = true;
				return true;
			}
			if (m_end - m_current >= 5 && strncmp(m_current, "false", 5) == 0)
			{
				m_current += 5;
				output = false;
				return true;
			}
			return false;
		}

		bool SkipValue()
		{
			SkipSpace();
			if (m_current >= m_end)
				return false;
			if (*m_current == '"')
			{
				std::string ignored;
				return ParseString(ignored);
			}
			if (*m_current == '{')
			{
				++m_current;
				SkipSpace();
				if (Consume('}')) return true;
				for (;;)
				{
					std::string key;
					if (!ParseString(key) || !Consume(':') || !SkipValue()) return false;
					if (Consume('}')) return true;
					if (!Consume(',')) return false;
				}
			}
			if (*m_current == '[')
			{
				++m_current;
				SkipSpace();
				if (Consume(']')) return true;
				for (;;)
				{
					if (!SkipValue()) return false;
					if (Consume(']')) return true;
					if (!Consume(',')) return false;
				}
			}
			while (m_current < m_end && *m_current != ',' && *m_current != '}' && *m_current != ']')
				++m_current;
			return true;
		}

	private:
		bool ParseHex4(unsigned int& value)
		{
			if (m_end - m_current < 4) return false;
			value = 0;
			for (int i = 0; i < 4; ++i)
			{
				const char ch = *m_current++;
				value <<= 4;
				if (ch >= '0' && ch <= '9') value += ch - '0';
				else if (ch >= 'a' && ch <= 'f') value += ch - 'a' + 10;
				else if (ch >= 'A' && ch <= 'F') value += ch - 'A' + 10;
				else return false;
			}
			return true;
		}

		const char* m_begin;
		const char* m_current;
		const char* m_end;
	};

	bool ReadAllBytes(const std::wstring& path, std::string& data)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream) return false;
		stream.seekg(0, std::ios::end);
		const std::streamoff length = stream.tellg();
		if (length < 0) return false;
		stream.seekg(0, std::ios::beg);
		data.resize(static_cast<size_t>(length));
		if (length > 0) stream.read(&data[0], length);
		return stream.good() || stream.eof();
	}

	std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
	{
		if (left.empty()) return right;
		if (right.empty()) return left;
		if (left.back() == L'\\' || left.back() == L'/') return left + right;
		return left + L"\\" + right;
	}

	std::wstring Upper(const std::wstring& value)
	{
		std::wstring result = value;
		if (!result.empty()) CharUpperBuffW(&result[0], static_cast<DWORD>(result.size()));
		return result;
	}

	struct PackageConfig
	{
		std::wstring category;
		std::wstring directory;
		std::wstring index;
	};

	struct DriverMediaConfig
	{
		std::wstring os;
		std::wstring osArchitecture;
		std::vector<PackageConfig> packages;
	};

	bool ParseStringField(JsonReader& reader, std::wstring& output)
	{
		std::string value;
		if (!reader.ParseString(value)) return false;
		output = Utf8ToWide(value);
		return true;
	}

	bool ParseConfigObject(JsonReader& reader, PackageConfig& config)
	{
		if (!reader.Consume('{')) return false;
		if (reader.Consume('}')) return true;
		for (;;)
		{
			std::string key;
			if (!reader.ParseString(key) || !reader.Consume(':')) return false;
			if (key == "Category") { if (!ParseStringField(reader, config.category)) return false; }
			else if (key == "DrvPkgDir") { if (!ParseStringField(reader, config.directory)) return false; }
			else if (key == "DrvIndex") { if (!ParseStringField(reader, config.index)) return false; }
			else if (!reader.SkipValue()) return false;
			if (reader.Consume('}')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool LoadConfig(const std::wstring& path, DriverMediaConfig& config)
	{
		std::string data;
		if (!ReadAllBytes(path, data)) return false;
		JsonReader reader(data);
		if (!reader.Consume('{')) return false;
		if (reader.Consume('}')) return true;
		for (;;)
		{
			std::string key;
			if (!reader.ParseString(key) || !reader.Consume(':')) return false;
			if (key == "OS")
			{
				if (!ParseStringField(reader, config.os)) return false;
			}
			else if (key == "OSArch")
			{
				if (!ParseStringField(reader, config.osArchitecture)) return false;
			}
			else if (key == "DriverPackages")
			{
				if (!reader.Consume('[')) return false;
				if (!reader.Consume(']'))
				{
					for (;;)
					{
						PackageConfig package;
						if (!ParseConfigObject(reader, package)) return false;
						if (!package.category.empty() && !package.directory.empty() && !package.index.empty())
							config.packages.push_back(package);
						if (reader.Consume(']')) break;
						if (!reader.Consume(',')) return false;
					}
				}
			}
			else if (!reader.SkipValue()) return false;
			if (reader.Consume('}')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParseArchive(JsonReader& reader, DriverPackage& package)
	{
		if (!reader.Consume('{')) return false;
		if (reader.Consume('}')) return true;
		for (;;)
		{
			std::string key;
			if (!reader.ParseString(key) || !reader.Consume(':')) return false;
			if (key == "file") { if (!ParseStringField(reader, package.archiveFile)) return false; }
			else if (key == "subdirectory") { if (!ParseStringField(reader, package.archiveSubdirectory)) return false; }
			else if (!reader.SkipValue()) return false;
			if (reader.Consume('}')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParseLegacyAfterInstall(JsonReader& reader, DriverPackage& package)
	{
		if (!reader.Consume('{')) return false;
		if (reader.Consume('}')) return true;
		for (;;)
		{
			std::string order;
			std::wstring configuredCommand;
			if (!reader.ParseString(order) || !reader.Consume(':') ||
				!ParseStringField(reader, configuredCommand)) return false;
			PostInstallAction action;
			action.type = L"execute";
			const size_t separator = configuredCommand.find(L',');
			action.file = separator == std::wstring::npos
				? configuredCommand : configuredCommand.substr(0, separator);
			action.arguments = separator == std::wstring::npos
				? std::wstring() : configuredCommand.substr(separator + 1);
			package.afterInstallActions.push_back(action);
			if (reader.Consume('}')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParsePostInstallAction(JsonReader& reader, PostInstallAction& action)
	{
		if (!reader.Consume('{')) return false;
		if (reader.Consume('}')) return true;
		for (;;)
		{
			std::string key;
			if (!reader.ParseString(key) || !reader.Consume(':')) return false;
			if (key == "type") { if (!ParseStringField(reader, action.type)) return false; }
			else if (key == "file") { if (!ParseStringField(reader, action.file)) return false; }
			else if (key == "arguments" || key == "args") { if (!ParseStringField(reader, action.arguments)) return false; }
			else if (key == "working_directory") { if (!ParseStringField(reader, action.workingDirectory)) return false; }
			else if (key == "continue_on_error") { if (!reader.ParseBoolean(action.continueOnError)) return false; }
			else if (key == "prevent_reboot") { if (!reader.ParseBoolean(action.preventReboot)) return false; }
			else if (!reader.SkipValue()) return false;
			if (reader.Consume('}')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParseSpecializedAfterInstall(JsonReader& reader, DriverPackage& package)
	{
		if (!reader.Consume('[')) return false;
		if (reader.Consume(']')) return true;
		for (;;)
		{
			if (!reader.Consume('{')) return false;
			std::wstring match;
			PostInstallAction action;
			if (!reader.Consume('}'))
			{
				for (;;)
				{
					std::string key;
					if (!reader.ParseString(key) || !reader.Consume(':')) return false;
					if (key == "match") { if (!ParseStringField(reader, match)) return false; }
					else if (key == "action") { if (!ParsePostInstallAction(reader, action)) return false; }
					else if (!reader.SkipValue()) return false;
					if (reader.Consume('}')) break;
					if (!reader.Consume(',')) return false;
				}
			}
			if (_wcsicmp(action.type.c_str(), L"embedded_config") == 0)
			{
				for (auto& configured : package.afterInstallActions)
					if (configured.match.empty())
					{
						configured.match = match;
						configured.relativeToDriverDirectory = true;
					}
			}
			else if (!action.type.empty())
			{
				action.match = match;
				package.afterInstallActions.push_back(action);
			}
			if (reader.Consume(']')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParseAdditionalInfRules(JsonReader& reader, DriverPackage& package)
	{
		if (!reader.Consume('[')) return false;
		if (reader.Consume(']')) return true;
		for (;;)
		{
			if (!reader.Consume('{')) return false;
			if (!reader.Consume('}'))
			{
				for (;;)
				{
					std::string key;
					if (!reader.ParseString(key) || !reader.Consume(':')) return false;
					if (key == "additional_infs")
					{
						if (!reader.Consume('[')) return false;
						if (!reader.Consume(']'))
						{
							for (;;)
							{
								PostInstallAction action;
								action.type = L"install_inf";
								if (!ParseStringField(reader, action.file)) return false;
								package.afterInstallActions.push_back(action);
								if (reader.Consume(']')) break;
								if (!reader.Consume(',')) return false;
							}
						}
					}
					else if (!reader.SkipValue()) return false;
					if (reader.Consume('}')) break;
					if (!reader.Consume(',')) return false;
				}
			}
			if (reader.Consume(']')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParseSpecialize(JsonReader& reader, DriverPackage& package)
	{
		if (!reader.Consume('{')) return false;
		if (reader.Consume('}')) return true;
		for (;;)
		{
			std::string key;
			if (!reader.ParseString(key) || !reader.Consume(':')) return false;
			if (key == "after_install")
			{
				if (!ParseSpecializedAfterInstall(reader, package)) return false;
			}
			else if (key == "install_pe")
			{
				if (!ParseAdditionalInfRules(reader, package)) return false;
			}
			else if (!reader.SkipValue()) return false;
			if (reader.Consume('}')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParseDriver(JsonReader& reader, DriverPackage& package)
	{
		if (!reader.Consume('{')) return false;
		if (reader.Consume('}')) return true;
		for (;;)
		{
			std::string key;
			if (!reader.ParseString(key) || !reader.Consume(':')) return false;
			if (key == "driver_id") { if (!ParseStringField(reader, package.id)) return false; }
			else if (key == "category") { if (!ParseStringField(reader, package.category)) return false; }
			else if (key == "inf_path") { if (!ParseStringField(reader, package.infPath)) return false; }
			else if (key == "driver_date") { if (!ParseStringField(reader, package.driverDate)) return false; }
			else if (key == "driver_version") { if (!ParseStringField(reader, package.driverVersion)) return false; }
			else if (key == "provider") { if (!ParseStringField(reader, package.provider)) return false; }
			else if (key == "device_class") { if (!ParseStringField(reader, package.deviceClass)) return false; }
			else if (key == "archive") { if (!ParseArchive(reader, package)) return false; }
			else if (key == "after_install") { if (!ParseLegacyAfterInstall(reader, package)) return false; }
			else if (key == "specialize") { if (!ParseSpecialize(reader, package)) return false; }
			else if (!reader.SkipValue()) return false;
			if (reader.Consume('}')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParseStringArray(JsonReader& reader, std::vector<std::wstring>& values)
	{
		if (!reader.Consume('[')) return false;
		if (reader.Consume(']')) return true;
		for (;;)
		{
			std::wstring value;
			if (!ParseStringField(reader, value)) return false;
			values.push_back(value);
			if (reader.Consume(']')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParseHardwareRoute(JsonReader& reader, DriverCatalog::HardwareRoute& route)
	{
		if (!reader.Consume('{')) return false;
		if (reader.Consume('}')) return true;
		for (;;)
		{
			std::string key;
			if (!reader.ParseString(key) || !reader.Consume(':')) return false;
			if (key == "selected_driver_id")
			{
				if (!ParseStringField(reader, route.driverId)) return false;
			}
			else if (key == "candidate_driver_ids")
			{
				if (!ParseStringArray(reader, route.candidateDriverIds)) return false;
			}
			else if (key == "device_names")
			{
				if (!reader.Consume('[')) return false;
				if (!reader.Consume(']'))
				{
					if (!ParseStringField(reader, route.deviceName)) return false;
					while (!reader.Consume(']'))
					{
						if (!reader.Consume(',') || !reader.SkipValue()) return false;
					}
				}
			}
			else if (key == "selection")
			{
				if (!reader.Consume('{')) return false;
				if (!reader.Consume('}'))
				{
					for (;;)
					{
						std::string selectionKey;
						if (!reader.ParseString(selectionKey) || !reader.Consume(':')) return false;
						if (selectionKey == "requires_parent_bluetooth_stack_context")
						{
							if (!reader.ParseBoolean(route.requiresParentBluetoothStackContext)) return false;
						}
						else if (!reader.SkipValue()) return false;
						if (reader.Consume('}')) break;
						if (!reader.Consume(',')) return false;
					}
				}
			}
			else if (!reader.SkipValue()) return false;
			if (reader.Consume('}')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	bool ParseIndex(const std::wstring& path, const std::wstring& packageDirectory,
		std::unordered_map<std::wstring, DriverPackage>& drivers,
		std::unordered_map<std::wstring, DriverCatalog::HardwareRoute>& routes)
	{
		std::string data;
		if (!ReadAllBytes(path, data)) return false;
		JsonReader reader(data);
		if (!reader.Consume('{')) return false;
		if (reader.Consume('}')) return true;
		for (;;)
		{
			std::string key;
			if (!reader.ParseString(key) || !reader.Consume(':')) return false;
			if (key == "drivers")
			{
				if (!reader.Consume('{')) return false;
				if (!reader.Consume('}'))
				{
					for (;;)
					{
						std::string objectKey;
						DriverPackage package;
						if (!reader.ParseString(objectKey) || !reader.Consume(':') || !ParseDriver(reader, package)) return false;
						if (package.id.empty()) package.id = Utf8ToWide(objectKey);
						package.packageDirectory = packageDirectory;
						drivers[package.id] = package;
						if (reader.Consume('}')) break;
						if (!reader.Consume(',')) return false;
					}
				}
			}
			else if (key == "hardware_id_map")
			{
				if (!reader.Consume('{')) return false;
				if (!reader.Consume('}'))
				{
					for (;;)
					{
						std::string hardwareId;
						DriverCatalog::HardwareRoute route;
						if (!reader.ParseString(hardwareId) || !reader.Consume(':') || !ParseHardwareRoute(reader, route)) return false;
						if (!route.driverId.empty()) routes[Upper(Utf8ToWide(hardwareId))] = route;
						if (reader.Consume('}')) break;
						if (!reader.Consume(',')) return false;
					}
				}
			}
			else if (!reader.SkipValue()) return false;
			if (reader.Consume('}')) return true;
			if (!reader.Consume(',')) return false;
		}
	}

	std::wstring FormatWin32Error(DWORD code)
	{
		wchar_t* buffer = nullptr;
		FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
		std::wstring message = buffer ? buffer : Tr(TextId::UnknownError);
		if (buffer) LocalFree(buffer);
		while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
		return message;
	}

	bool ReadRegistryProperty(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfo, DWORD property, std::vector<BYTE>& buffer)
	{
		DWORD required = 0, type = 0;
		SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfo, property, &type, nullptr, 0, &required);
		if (required == 0) return false;
		buffer.resize(required + sizeof(wchar_t));
		return SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfo, property, &type, buffer.data(), static_cast<DWORD>(buffer.size()), &required) != FALSE;
	}

	std::wstring GetDeviceText(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property)
	{
		std::vector<BYTE> data;
		if (!ReadRegistryProperty(set, device, property, data)) return {};
		return reinterpret_cast<const wchar_t*>(data.data());
	}

	std::vector<std::wstring> GetMultiString(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property)
	{
		std::vector<BYTE> data;
		std::vector<std::wstring> result;
		if (!ReadRegistryProperty(set, device, property, data)) return result;
		const wchar_t* current = reinterpret_cast<const wchar_t*>(data.data());
		while (*current)
		{
			result.emplace_back(current);
			current += wcslen(current) + 1;
		}
		return result;
	}

	std::wstring FileNamePart(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L"\\/");
		return Upper(slash == std::wstring::npos ? path : path.substr(slash + 1));
	}

	bool SameText(const std::wstring& left, const std::wstring& right)
	{
		return !left.empty() && !right.empty() && _wcsicmp(left.c_str(), right.c_str()) == 0;
	}

	bool IsVendorSpecificCompatibleId(const std::wstring& hardwareId)
	{
		const std::wstring value = Upper(hardwareId);
		return value.find(L"VEN_") != std::wstring::npos ||
			value.find(L"VID_") != std::wstring::npos ||
			value.find(L"DEV_") != std::wstring::npos ||
			value.find(L"PID_") != std::wstring::npos ||
			value.find(L"SUBSYS_") != std::wstring::npos;
	}

	bool IsGenericHardwareId(const std::wstring& hardwareId)
	{
		const std::wstring value = Upper(hardwareId);
		return value.find(L"HID_DEVICE_") == 0 ||
			value.find(L"USB\\CLASS_") == 0 ||
			value.find(L"PCI\\CC_") == 0;
	}

	bool IsSystemBuiltInDriver(const std::wstring& provider, const std::wstring& infName)
	{
		if (SameText(provider, L"Microsoft")) return true;
		const std::wstring fileName = FileNamePart(infName);
		return !fileName.empty() && fileName.find(L"OEM") != 0;
	}

	std::wstring ReadDriverRegistryString(HDEVINFO set, SP_DEVINFO_DATA& device, const wchar_t* name)
	{
		HKEY key = SetupDiOpenDevRegKey(set, &device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
		if (key == INVALID_HANDLE_VALUE) return {};
		DWORD type = 0, bytes = 0;
		LONG status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
		std::wstring value;
		if (status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t))
		{
			std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
			if (RegQueryValueExW(key, name, nullptr, &type,
				reinterpret_cast<BYTE*>(buffer.data()), &bytes) == ERROR_SUCCESS)
				value.assign(buffer.data());
		}
		RegCloseKey(key);
		return value;
	}

	std::vector<unsigned long long> NumericParts(const std::wstring& value)
	{
		std::vector<unsigned long long> parts;
		for (size_t index = 0; index < value.size();)
		{
			if (!iswdigit(value[index])) { ++index; continue; }
			unsigned long long part = 0;
			while (index < value.size() && iswdigit(value[index]))
			{
				const unsigned int digit = value[index++] - L'0';
				part = part > (ULLONG_MAX - digit) / 10 ? ULLONG_MAX : part * 10 + digit;
			}
			parts.push_back(part);
		}
		return parts;
	}

	int CompareNumericParts(const std::wstring& left, const std::wstring& right)
	{
		const auto a = NumericParts(left);
		const auto b = NumericParts(right);
		const size_t count = max(a.size(), b.size());
		for (size_t index = 0; index < count; ++index)
		{
			const unsigned long long av = index < a.size() ? a[index] : 0;
			const unsigned long long bv = index < b.size() ? b[index] : 0;
			if (av != bv) return av > bv ? 1 : -1;
		}
		return 0;
	}

	unsigned long long DateKey(const std::wstring& value)
	{
		const auto parts = NumericParts(value);
		if (parts.size() < 3) return 0;
		// Driver dates use month/day/year in both the index and Windows driver registry.
		return parts[2] * 10000 + parts[0] * 100 + parts[1];
	}

	bool IsPackageNewer(const DriverPackage& package, const std::wstring& installedVersion,
		const std::wstring& installedDate)
	{
		const unsigned long long packageDate = DateKey(package.driverDate);
		const unsigned long long currentDate = DateKey(installedDate);
		// A later installed-driver date represents a newer OEM release line even
		// when its vendor-specific version sequence is numerically lower.
		if (packageDate != 0 && currentDate != 0 && currentDate > packageDate)
			return false;
		if (!package.driverVersion.empty() && !installedVersion.empty())
		{
			const int versionOrder = CompareNumericParts(package.driverVersion, installedVersion);
			if (versionOrder != 0) return versionOrder > 0;
		}
		return packageDate != 0 && currentDate != 0 && packageDate > currentDate;
	}

	std::wstring Quote(const std::wstring& value)
	{
		return L"\"" + value + L"\"";
	}

	bool EnsureDirectory(const std::wstring& path)
	{
		const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
		return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	bool RunProcess(const std::wstring& commandLine, DWORD& exitCode, std::wstring& error,
		const std::wstring& workingDirectory = {})
	{
		std::vector<wchar_t> command(commandLine.begin(), commandLine.end());
		command.push_back(L'\0');
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESHOWWINDOW;
		startup.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION process = {};
		if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
			nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process))
		{
			error = FormatWin32Error(GetLastError());
			return false;
		}
		WaitForSingleObject(process.hProcess, INFINITE);
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return true;
	}

	std::wstring ModuleDirectory()
	{
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, _countof(path));
		wchar_t* slash = wcsrchr(path, L'\\');
		if (slash) *slash = L'\0';
		return path;
	}

	std::wstring ParentDirectory(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
	}

	bool FileExists(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	bool ArchiveContainsFile(const std::wstring& sevenZip, const std::wstring& archive,
		const std::wstring& filePath)
	{
		if (!FileExists(sevenZip) || !FileExists(archive) || filePath.empty()) return false;
		const std::wstring command = Quote(sevenZip) + L" l -ba -slt " + Quote(archive) +
			L" " + Quote(filePath);
		SECURITY_ATTRIBUTES security = {};
		security.nLength = sizeof(security);
		security.bInheritHandle = TRUE;
		HANDLE readPipe = nullptr, writePipe = nullptr;
		if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return false;
		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
		std::vector<wchar_t> mutableCommand(command.begin(), command.end());
		mutableCommand.push_back(L'\0');
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESHOWWINDOW;
		startup.wShowWindow = SW_HIDE;
		startup.dwFlags |= STARTF_USESTDHANDLES;
		startup.hStdOutput = writePipe;
		startup.hStdError = writePipe;
		startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		PROCESS_INFORMATION process = {};
		const BOOL started = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
			TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
		CloseHandle(writePipe);
		if (!started) { CloseHandle(readPipe); return false; }
		std::string output;
		char buffer[4096];
		DWORD bytes = 0;
		while (ReadFile(readPipe, buffer, sizeof(buffer), &bytes, nullptr) && bytes != 0)
			output.append(buffer, bytes);
		CloseHandle(readPipe);
		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD exitCode = 1;
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		if (exitCode != 0) return false;
		const std::string marker = "Path = ";
		for (size_t position = 0; (position = output.find(marker, position)) != std::string::npos;)
		{
			position += marker.size();
			const size_t end = output.find_first_of("\r\n", position);
			const std::wstring listed = Utf8ToWide(output.substr(position, end - position));
			if (_wcsicmp(listed.c_str(), filePath.c_str()) == 0) return true;
			if (end == std::string::npos) break;
			position = end;
		}
		return false;
	}

	bool FindFileRecursive(const std::wstring& directory, const std::wstring& fileName,
		std::wstring& result)
	{
		WIN32_FIND_DATAW data = {};
		const HANDLE find = FindFirstFileW(JoinPath(directory, L"*").c_str(), &data);
		if (find == INVALID_HANDLE_VALUE) return false;
		bool found = false;
		do
		{
			if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
			const std::wstring path = JoinPath(directory, data.cFileName);
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				found = FindFileRecursive(path, fileName, result);
			else if (_wcsicmp(data.cFileName, fileName.c_str()) == 0)
			{
				result = path;
				found = true;
			}
		} while (!found && FindNextFileW(find, &data));
		FindClose(find);
		return found;
	}

	bool IsBlockedRestartCommand(const std::wstring& file, const std::wstring& arguments)
	{
		const std::wstring name = FileNamePart(file);
		const std::wstring args = Upper(arguments);
		if (name == L"SHUTDOWN.EXE" || name == L"RESTART.EXE") return true;
		if ((name == L"CMD.EXE" || name == L"POWERSHELL.EXE" || name == L"PWSH.EXE") &&
			(args.find(L"SHUTDOWN") != std::wstring::npos ||
			 args.find(L"RESTART-COMPUTER") != std::wstring::npos)) return true;
		return false;
	}

	std::wstring ActionExtractionPattern(const DriverPackage& driver,
		const PostInstallAction& action)
	{
		if (action.relativeToDriverDirectory)
		{
			const std::wstring driverDirectory = ParentDirectory(driver.infPath);
			const std::wstring normalizedMatch = Upper(action.match);
			const std::wstring normalizedDirectory = Upper(driverDirectory);
			if (!action.match.empty() && normalizedDirectory.find(normalizedMatch + L"\\") == 0)
				return action.match + L"\\*";
			return driverDirectory + L"\\*";
		}
		if (!action.match.empty())
		{
			const size_t dot = action.match.find_last_of(L'.');
			const size_t slash = action.match.find_last_of(L"\\/");
			return dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)
				? ParentDirectory(action.match) + L"\\*" : action.match + L"\\*";
		}
		if (action.file.find(L'\\') != std::wstring::npos || action.file.find(L'/') != std::wstring::npos)
			return ParentDirectory(action.file) + L"\\*";
		return ParentDirectory(driver.infPath) + L"\\*";
	}

	bool ResolvePostInstallFile(const std::wstring& cache, const DriverPackage& driver,
		const PostInstallAction& action, std::wstring& path)
	{
		path = JoinPath(cache, action.file);
		if (FileExists(path)) return true;
		if (action.relativeToDriverDirectory)
		{
			const std::wstring driverDirectory = JoinPath(cache, ParentDirectory(driver.infPath));
			path = JoinPath(driverDirectory, action.file);
			if (FileExists(path)) return true;
			if (FindFileRecursive(driverDirectory, FileNamePart(action.file), path)) return true;
			if (!action.match.empty())
				return FindFileRecursive(JoinPath(cache, action.match), FileNamePart(action.file), path);
			return false;
		}
		const size_t dot = action.match.find_last_of(L'.');
		const size_t slash = action.match.find_last_of(L"\\/");
		const bool matchIsFile = dot != std::wstring::npos &&
			(slash == std::wstring::npos || dot > slash);
		const std::wstring base = action.match.empty() ? ParentDirectory(driver.infPath) :
			(matchIsFile ? ParentDirectory(action.match) : action.match);
		const std::wstring searchRoot = JoinPath(cache, base);
		return FindFileRecursive(searchRoot, FileNamePart(action.file), path);
	}

	struct DetectedWindowsVersion
	{
		unsigned long majorVersion = 0;
		unsigned long minorVersion = 0;
		unsigned long buildNumber = 0;
		unsigned short servicePackMajor = 0;
		std::wstring architecture;
	};

	std::wstring NormalizeArchitecture(const std::wstring& architecture)
	{
		const std::wstring value = Upper(architecture);
		if (value == L"X64" || value == L"AMD64") return L"x64";
		if (value == L"X86" || value == L"I386" || value == L"WIN32") return L"x86";
		return {};
	}

	bool DetectWindowsVersion(DetectedWindowsVersion& version)
	{
		struct RtlVersionInfo
		{
			ULONG size;
			ULONG majorVersion;
			ULONG minorVersion;
			ULONG buildNumber;
			ULONG platformId;
			WCHAR servicePack[128];
			USHORT servicePackMajor;
			USHORT servicePackMinor;
			USHORT suiteMask;
			UCHAR productType;
			UCHAR reserved;
		};
		using RtlGetVersionFunction = LONG(WINAPI*)(RtlVersionInfo*);
		HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		RtlGetVersionFunction rtlGetVersion = ntdll == nullptr ? nullptr :
			reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"));
		if (rtlGetVersion == nullptr) return false;
		RtlVersionInfo info = {};
		info.size = sizeof(info);
		if (rtlGetVersion(&info) < 0) return false;
		version.majorVersion = info.majorVersion;
		version.minorVersion = info.minorVersion;
		version.buildNumber = info.buildNumber;
		version.servicePackMajor = info.servicePackMajor;

		SYSTEM_INFO systemInfo = {};
		GetNativeSystemInfo(&systemInfo);
		switch (systemInfo.wProcessorArchitecture)
		{
		case PROCESSOR_ARCHITECTURE_AMD64: version.architecture = L"x64"; break;
		case PROCESSOR_ARCHITECTURE_INTEL: version.architecture = L"x86"; break;
		case 12: version.architecture = L"ARM64"; break;
		default: version.architecture = L"unknown"; break;
		}
		return true;
	}

	std::wstring NumericVersion(unsigned long majorVersion, unsigned long minorVersion, unsigned long buildNumber)
	{
		return std::to_wstring(majorVersion) + L"." + std::to_wstring(minorVersion) + L"." + std::to_wstring(buildNumber);
	}

	std::wstring CurrentSystemName(const DetectedWindowsVersion& version)
	{
		std::wstring name = L"Windows";
		if (version.majorVersion == 5 && version.minorVersion == 1) name = L"Windows XP";
		else if (version.majorVersion == 5 && version.minorVersion == 2) name = L"Windows XP Professional x64 Edition";
		else if (version.majorVersion == 6 && version.minorVersion == 0) name = L"Windows Vista";
		else if (version.majorVersion == 6 && version.minorVersion == 1) name = L"Windows 7";
		else if (version.majorVersion == 6 && version.minorVersion == 2) name = L"Windows 8";
		else if (version.majorVersion == 6 && version.minorVersion == 3) name = L"Windows 8.1";
		else if (version.majorVersion >= 10) name = version.buildNumber >= 22000 ? L"Windows 11" : L"Windows 10";
		return name;
	}

	std::wstring RequiredSystemName(const std::wstring& targetOs, const std::wstring& architecture)
	{
		const std::wstring os = Upper(targetOs);
		if (os == L"WINXP") return Upper(architecture) == L"X64"
			? L"Windows XP Professional x64 Edition Service Pack 2"
			: L"Windows XP Service Pack 3";
		if (os == L"WINVISTA") return L"Windows Vista Service Pack 2";
		if (os == L"WIN7") return L"Windows 7 Service Pack 1";
		if (os == L"WIN8") return L"Windows 8.x";
		if (os == L"WIN10")
		{
			switch (Localization::GetLanguage())
			{
			case UiLanguage::ChineseSimplified: return L"Windows 10 或更高版本";
			case UiLanguage::ChineseTraditional: return L"Windows 10 或更新版本";
			default: return L"Windows 10 or later";
			}
		}
		if (os == L"WIN10RS5") return L"Windows 10";
		if (os == L"WIN11") return L"Windows 11";
		return {};
	}

	std::wstring DriverMediaSystemName(const std::wstring& targetOs)
	{
		const std::wstring os = Upper(targetOs);
		if (os == L"WINXP") return L"Windows XP";
		if (os == L"WINVISTA") return L"Windows Vista";
		if (os == L"WIN7") return L"Windows 7";
		if (os == L"WIN8") return L"Windows 8.x";
		if (os == L"WIN10") return L"Windows 10";
		if (os == L"WIN10RS5") return L"Windows 10";
		if (os == L"WIN11") return L"Windows 11";
		return {};
	}
}

bool SystemCompatibility::IsVersionSupported(const std::wstring& targetOs, unsigned long majorVersion,
	unsigned long minorVersion, unsigned long buildNumber, unsigned short servicePackMajor,
	const std::wstring& architecture, std::wstring& requiredVersion, std::wstring& updateHint)
{
	requiredVersion.clear();
	updateHint.clear();
	const std::wstring os = Upper(targetOs);
	if (os == L"WINXP")
	{
		const bool x64 = NormalizeArchitecture(architecture) == L"x64";
		requiredVersion = x64 ? L"5.2.3790 Service Pack 2" : L"5.1.2600 Service Pack 3";
		const unsigned short requiredServicePack = x64 ? 2 : 3;
		const bool versionMatches = x64
			? majorVersion == 5 && minorVersion == 2 && buildNumber == 3790
			: majorVersion == 5 && minorVersion == 1 && buildNumber == 2600;
		if (versionMatches && servicePackMajor < requiredServicePack)
		{
			switch (Localization::GetLanguage())
			{
			case UiLanguage::ChineseSimplified: updateHint = L"请更新操作系统至 Service Pack " + std::to_wstring(requiredServicePack) + L" 后继续执行驱动程序安装"; break;
			case UiLanguage::ChineseTraditional: updateHint = L"請更新作業系統至 Service Pack " + std::to_wstring(requiredServicePack) + L" 後繼續執行驅動程式安裝"; break;
			default: updateHint = L"Please update the operating system to Service Pack " + std::to_wstring(requiredServicePack) + L" before continuing driver installation"; break;
			}
		}
		return versionMatches && servicePackMajor >= requiredServicePack;
	}
	if (os == L"WINVISTA")
	{
		requiredVersion = L"6.0.6002";
		if (majorVersion == 6 && minorVersion == 0 && (buildNumber == 6000 || buildNumber == 6001))
			updateHint = Tr(TextId::VistaSp2Required);
		return majorVersion == 6 && minorVersion == 0 && buildNumber == 6002;
	}
	if (os == L"WIN7")
	{
		requiredVersion = L"6.1.7601";
		if (majorVersion == 6 && minorVersion == 1 && buildNumber == 7600)
			updateHint = Tr(TextId::Win7Sp1Required);
		return majorVersion == 6 && minorVersion == 1 && buildNumber == 7601;
	}
	if (os == L"WIN8")
	{
		requiredVersion = L"6.2.9200 / 6.3.9600";
		return (majorVersion == 6 && minorVersion == 2 && buildNumber == 9200) ||
			(majorVersion == 6 && minorVersion == 3 && buildNumber == 9600);
	}
	if (os == L"WIN10")
	{
		switch (Localization::GetLanguage())
		{
		case UiLanguage::ChineseSimplified: requiredVersion = L"10.0.10240 或更高版本"; break;
		case UiLanguage::ChineseTraditional: requiredVersion = L"10.0.10240 或更新版本"; break;
		default: requiredVersion = L"10.0.10240 or later"; break;
		}
		return majorVersion > 10 || (majorVersion == 10 &&
			(minorVersion > 0 || (minorVersion == 0 && buildNumber >= 10240)));
	}
	if (os == L"WIN10RS5")
	{
		switch (Localization::GetLanguage())
		{
		case UiLanguage::ChineseSimplified: requiredVersion = L"10.0.17763 或更高版本"; break;
		case UiLanguage::ChineseTraditional: requiredVersion = L"10.0.17763 或更新版本"; break;
		default: requiredVersion = L"10.0.17763 or later"; break;
		}
		return majorVersion > 10 || (majorVersion == 10 &&
			(minorVersion > 0 || (minorVersion == 0 && buildNumber >= 17763)));
	}
	if (os == L"WIN11")
	{
		switch (Localization::GetLanguage())
		{
		case UiLanguage::ChineseSimplified: requiredVersion = L"10.0.22000 或更高版本"; break;
		case UiLanguage::ChineseTraditional: requiredVersion = L"10.0.22000 或更新版本"; break;
		default: requiredVersion = L"10.0.22000 or later"; break;
		}
		return majorVersion > 10 || (majorVersion == 10 &&
			(minorVersion > 0 || (minorVersion == 0 && buildNumber >= 22000)));
	}
	return false;
}

bool SystemCompatibility::GetDriverMediaTarget(const std::wstring& dataRoot, std::wstring& targetSystem,
	std::wstring& targetArchitecture)
{
	targetSystem.clear();
	targetArchitecture.clear();
	DriverMediaConfig config;
	if (!LoadConfig(JoinPath(dataRoot, L"config.json"), config)) return false;
	targetSystem = DriverMediaSystemName(config.os);
	targetArchitecture = NormalizeArchitecture(config.osArchitecture);
	return !targetSystem.empty() && !targetArchitecture.empty();
}

bool SystemCompatibility::ValidateDriverMedia(const std::wstring& dataRoot, std::wstring& error)
{
	const std::wstring configPath = JoinPath(dataRoot, L"config.json");
	DriverMediaConfig config;
	if (!LoadConfig(configPath, config))
	{
		error = std::wstring(Tr(TextId::ConfigInvalid)) + configPath;
		return false;
	}
	const std::wstring requiredArchitecture = NormalizeArchitecture(config.osArchitecture);
	const std::wstring requiredSystemName = RequiredSystemName(config.os, requiredArchitecture);
	std::wstring requiredVersion;
	std::wstring updateHint;
	if (requiredArchitecture.empty() || requiredSystemName.empty())
	{
		error = std::wstring(Tr(TextId::UnsupportedSystemConfig)) + config.os + L" / " + config.osArchitecture;
		return false;
	}

	DetectedWindowsVersion current;
	if (!DetectWindowsVersion(current))
	{
		error = Tr(TextId::VersionDetectionFailed);
		return false;
	}
	const bool versionMatches = IsVersionSupported(config.os, current.majorVersion, current.minorVersion,
		current.buildNumber, current.servicePackMajor, requiredArchitecture, requiredVersion, updateHint);
	const bool architectureMatches = current.architecture == requiredArchitecture;
	if (versionMatches && architectureMatches) return true;

	CString message;
	message.Format(Tr(TextId::SystemMismatchFormat), CurrentSystemName(current).c_str(),
		current.architecture.c_str(), NumericVersion(current.majorVersion, current.minorVersion, current.buildNumber).c_str(),
		requiredSystemName.c_str(), requiredArchitecture.c_str(), requiredVersion.c_str());
	error = message.GetString();
	error += L"\n" + (updateHint.empty() ? std::wstring(Tr(TextId::UseCompatibleDriverPackage)) : updateHint);
	return false;
}

bool SystemCompatibility::RunRuleTests(std::wstring& error)
{
	struct TestCase
	{
		const wchar_t* os;
		unsigned long majorVersion;
		unsigned long minorVersion;
		unsigned long buildNumber;
		unsigned short servicePackMajor;
		const wchar_t* architecture;
		bool expected;
		bool expectsUpdateHint;
	};
	const TestCase cases[] =
	{
		{ L"WinXP", 5, 1, 2600, 3, L"x86", true, false },
		{ L"WinXP", 5, 1, 2600, 2, L"x86", false, true },
		{ L"WinXP", 5, 2, 3790, 2, L"x64", true, false },
		{ L"WinXP", 5, 2, 3790, 1, L"x64", false, true },
		{ L"WinXP", 6, 0, 6002, 0, L"x86", false, false },
		{ L"WinVista", 6, 0, 6002, 0, L"x64", true, false },
		{ L"WinVista", 6, 0, 6000, 0, L"x64", false, true },
		{ L"WinVista", 6, 0, 6001, 0, L"x64", false, true },
		{ L"Win7", 6, 1, 7601, 0, L"x64", true, false },
		{ L"Win7", 6, 1, 7600, 0, L"x64", false, true },
		{ L"Win8", 6, 2, 9200, 0, L"x64", true, false },
		{ L"Win8", 6, 3, 9600, 0, L"x64", true, false },
		// Negative case: reject an invalid Win8/8.1 version-and-build pairing.
		{ L"Win8", 6, 3, 9200, 0, L"x64", false, false },
		{ L"Win10", 10, 0, 10240, 0, L"x64", true, false },
		// Negative boundary: reject the build immediately below the Win10 RTM minimum.
		{ L"Win10", 10, 0, 10239, 0, L"x64", false, false },
		{ L"Win10", 10, 0, 26100, 0, L"x64", true, false },
		{ L"Win10RS5", 10, 0, 17763, 0, L"x64", true, false },
		{ L"Win10RS5", 10, 0, 17762, 0, L"x64", false, false },
		{ L"Win10RS5", 10, 0, 26100, 0, L"x64", true, false },
		{ L"Win11", 10, 0, 22000, 0, L"x64", true, false },
		{ L"Win11", 10, 0, 21999, 0, L"x64", false, false },
		{ L"Win11", 10, 0, 26100, 0, L"x64", true, false }
	};
	for (const auto& item : cases)
	{
		std::wstring requiredVersion;
		std::wstring updateHint;
		const bool actual = IsVersionSupported(item.os, item.majorVersion, item.minorVersion,
			item.buildNumber, item.servicePackMajor, item.architecture, requiredVersion, updateHint);
		if (actual != item.expected || (!updateHint.empty()) != item.expectsUpdateHint)
		{
			error = std::wstring(L"Compatibility rule failed: ") + item.os + L" " +
				NumericVersion(item.majorVersion, item.minorVersion, item.buildNumber);
			return false;
		}
	}
	if (NormalizeArchitecture(L"x64") != L"x64" || NormalizeArchitecture(L"AMD64") != L"x64" ||
		NormalizeArchitecture(L"x86") != L"x86" || NormalizeArchitecture(L"Win32") != L"x86")
	{
		error = L"Architecture normalization failed";
		return false;
	}
	error.clear();
	return true;
}

bool DriverCatalog::Load(const std::wstring& dataRoot, std::wstring& error)
{
	m_drivers.clear();
	m_hardwareIds.clear();
	DriverMediaConfig config;
	const std::wstring configPath = JoinPath(dataRoot, L"config.json");
	if (!LoadConfig(configPath, config) || config.packages.empty())
	{
		error = std::wstring(Tr(TextId::ConfigInvalid)) + configPath;
		return false;
	}
	for (const auto& package : config.packages)
	{
		const std::wstring path = JoinPath(JoinPath(dataRoot, package.directory), package.index);
		if (!ParseIndex(path, package.directory, m_drivers, m_hardwareIds))
		{
			error = std::wstring(Tr(TextId::IndexReadFailed)) + path;
			m_drivers.clear();
			m_hardwareIds.clear();
			return false;
		}
	}
	return true;
}

size_t DriverCatalog::AfterInstallActionCount() const
{
	size_t count = 0;
	for (const auto& item : m_drivers) count += item.second.afterInstallActions.size();
	return count;
}

bool DriverCatalog::Match(const std::vector<std::wstring>& hardwareIds,
	const DriverMatchContext& context, DriverPackage& driver,
	std::wstring& matchedHardwareId, std::wstring& indexedDeviceName) const
{
	for (const auto& hardwareId : hardwareIds)
	{
		if (context.compatibleIds && !IsVendorSpecificCompatibleId(hardwareId)) continue;
		auto route = m_hardwareIds.find(Upper(hardwareId));
		if (route == m_hardwareIds.end()) continue;
		std::wstring selectedDriverId = route->second.driverId;
		if (route->second.requiresParentBluetoothStackContext)
		{
			if (context.compatibleIds) continue;
			selectedDriverId.clear();
			for (const auto& candidateId : route->second.candidateDriverIds)
			{
				auto candidate = m_drivers.find(candidateId);
				if (candidate == m_drivers.end()) continue;
				if (SameText(candidate->second.provider, context.currentProvider) &&
					SameText(candidate->second.deviceClass, context.currentDeviceClass))
				{
					selectedDriverId = candidateId;
					break;
				}
				if (SameText(FileNamePart(candidate->second.infPath), context.currentInfName))
				{
					selectedDriverId = candidateId;
					break;
				}
			}
			if (selectedDriverId.empty()) continue;
		}
		auto package = m_drivers.find(selectedDriverId);
		if (package == m_drivers.end()) continue;
		if (IsGenericHardwareId(hardwareId) &&
			!SameText(package->second.provider, context.currentProvider)) continue;
		if (context.compatibleIds && !context.currentDeviceClass.empty() &&
			!SameText(package->second.deviceClass, context.currentDeviceClass)) continue;
		driver = package->second;
		matchedHardwareId = hardwareId;
		indexedDeviceName = route->second.deviceName;
		return true;
	}
	return false;
}

bool DriverCatalog::RunMatchingTests(std::wstring& error)
{
	DriverCatalog catalog;
	DriverPackage intel;
	intel.id = L"IntelAux";
	intel.provider = L"Intel Corporation";
	intel.deviceClass = L"BluetoothAuxiliary";
	intel.infPath = L"Intel\\btmaux.inf";
	DriverPackage microsoft;
	microsoft.id = L"MicrosoftMedia";
	microsoft.provider = L"Microsoft";
	microsoft.deviceClass = L"MEDIA";
	microsoft.infPath = L"microsoft_bluetooth_a2dp_snk.inf";
	catalog.m_drivers[intel.id] = intel;
	catalog.m_drivers[microsoft.id] = microsoft;
	HardwareRoute contextual;
	contextual.driverId = intel.id;
	contextual.candidateDriverIds = { intel.id };
	contextual.requiresParentBluetoothStackContext = true;
	catalog.m_hardwareIds[L"BTHENUM\\{0000110A}"] = contextual;
	HardwareRoute exact;
	exact.driverId = microsoft.id;
	catalog.m_hardwareIds[L"BTHENUM\\EXACT_XIAOMI"] = exact;
	HardwareRoute generic;
	generic.driverId = intel.id;
	catalog.m_hardwareIds[L"HID_DEVICE_UP:FF00_U:0001"] = generic;
	DriverPackage result;
	std::wstring matched, name;
	DriverMatchContext microsoftContext;
	microsoftContext.currentProvider = L"Microsoft";
	microsoftContext.currentDeviceClass = L"MEDIA";
	microsoftContext.currentInfName = L"microsoft_bluetooth_a2dp_snk.inf";
	microsoftContext.compatibleIds = true;
	if (catalog.Match({ L"BTHENUM\\{0000110A}" }, microsoftContext, result, matched, name))
	{
		error = L"Ambiguous Bluetooth profile route was accepted without matching stack context";
		return false;
	}
	microsoftContext.compatibleIds = false;
	if (catalog.Match({ L"HID_DEVICE_UP:FF00_U:0001" }, microsoftContext, result, matched, name))
	{
		error = L"Generic HID hardware ID was accepted for a different provider";
		return false;
	}
	if (!catalog.Match({ L"BTHENUM\\EXACT_XIAOMI" }, microsoftContext, result, matched, name) ||
		result.id != microsoft.id)
	{
		error = L"Exact hardware ID route did not match";
		return false;
	}
	DriverMatchContext intelContext;
	intelContext.currentProvider = L"Intel Corporation";
	intelContext.currentDeviceClass = L"BluetoothAuxiliary";
	intelContext.currentInfName = L"btmaux.inf";
	intelContext.compatibleIds = false;
	if (!catalog.Match({ L"BTHENUM\\{0000110A}" }, intelContext, result, matched, name) ||
		result.id != intel.id)
	{
		error = L"Bluetooth profile route did not honor matching stack context";
		return false;
	}
	error.clear();
	return true;
}

bool DeviceScanner::Scan(const std::wstring& dataRoot, const DriverCatalog& catalog,
	std::vector<DeviceMatch>& matches, int& scannedDeviceCount, std::wstring& error)
{
	matches.clear();
	scannedDeviceCount = 0;
	const std::wstring sevenZip = DriverInstaller::Find7Zip(dataRoot);
	std::unordered_map<std::wstring, bool> mediaAvailability;
	HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (set == INVALID_HANDLE_VALUE)
	{
		error = std::wstring(Tr(TextId::EnumerateFailed)) + FormatWin32Error(GetLastError());
		return false;
	}
	for (DWORD index = 0;; ++index)
	{
		SP_DEVINFO_DATA device = {};
		device.cbSize = sizeof(device);
		if (!SetupDiEnumDeviceInfo(set, index, &device))
		{
			if (GetLastError() != ERROR_NO_MORE_ITEMS) error = std::wstring(Tr(TextId::ReadDeviceListFailed)) + FormatWin32Error(GetLastError());
			break;
		}
		++scannedDeviceCount;
		std::vector<std::wstring> ids = GetMultiString(set, device, SPDRP_HARDWAREID);
		if (ids.empty()) continue;
		DriverMatchContext matchContext;
		matchContext.currentProvider = GetDeviceText(set, device, SPDRP_MFG);
		matchContext.currentDeviceClass = GetDeviceText(set, device, SPDRP_CLASS);
		matchContext.currentInfName = ReadDriverRegistryString(set, device, L"InfPath");
		DriverPackage package;
		std::wstring matchedId, indexedName;
		if (!catalog.Match(ids, matchContext, package, matchedId, indexedName))
		{
			std::vector<std::wstring> compatible = GetMultiString(set, device, SPDRP_COMPATIBLEIDS);
			matchContext.compatibleIds = true;
			if (!catalog.Match(compatible, matchContext, package, matchedId, indexedName)) continue;
		}
		const std::wstring archive = JoinPath(JoinPath(dataRoot, package.packageDirectory),
			package.archiveFile);
		const std::wstring mediaKey = Upper(archive + L"|" + package.infPath);
		auto availability = mediaAvailability.find(mediaKey);
		if (availability == mediaAvailability.end())
			availability = mediaAvailability.emplace(mediaKey,
				ArchiveContainsFile(sevenZip, archive, package.infPath)).first;
		if (!availability->second) continue;
		DeviceMatch match;
		wchar_t instanceId[4096] = {};
		if (SetupDiGetDeviceInstanceIdW(set, &device, instanceId, _countof(instanceId), nullptr)) match.instanceId = instanceId;
		std::vector<BYTE> installedDriver;
		const bool hasInstalledDriver = ReadRegistryProperty(set, device, SPDRP_DRIVER, installedDriver);
		match.installedDriverProvider = !hasInstalledDriver ? std::wstring() :
			ReadDriverRegistryString(set, device, L"ProviderName");
		match.usesSystemBuiltInDriver = hasInstalledDriver &&
			IsSystemBuiltInDriver(match.installedDriverProvider, matchContext.currentInfName);
		match.needsDriver = !hasInstalledDriver || match.usesSystemBuiltInDriver;
		const bool providerChanged = hasInstalledDriver &&
			!SameText(match.installedDriverProvider, package.provider);
		if ((match.needsDriver || providerChanged) && !indexedName.empty())
			match.displayName = indexedName;
		else
		{
			match.displayName = GetDeviceText(set, device, SPDRP_FRIENDLYNAME);
			if (match.displayName.empty()) match.displayName = GetDeviceText(set, device, SPDRP_DEVICEDESC);
			if (match.displayName.empty()) match.displayName = indexedName.empty() ? matchedId : indexedName;
		}
		match.hardwareId = matchedId;
		match.indexedDeviceName = indexedName;
		match.driver = package;
		if (hasInstalledDriver && !match.usesSystemBuiltInDriver)
		{
			match.installedDriverVersion = ReadDriverRegistryString(set, device, L"DriverVersion");
			match.installedDriverDate = ReadDriverRegistryString(set, device, L"DriverDate");
			match.updateAvailable = IsPackageNewer(match.driver, match.installedDriverVersion,
				match.installedDriverDate);
		}
		matches.push_back(match);
	}
	SetupDiDestroyDeviceInfoList(set);
	return error.empty();
}

bool DeviceScanner::RunVersionComparisonTests(std::wstring& error)
{
	struct Case
	{
		const wchar_t* packageVersion;
		const wchar_t* installedVersion;
		const wchar_t* packageDate;
		const wchar_t* installedDate;
		bool expected;
	};
	const Case cases[] =
	{
		{ L"1.2.0.34", L"1.2.0.33", L"01/01/2020", L"01/01/2020", true },
		{ L"1.2.001.0402", L"1.2.1.402", L"03/29/2015", L"03/28/2015", true },
		{ L"2.2.2.1045", L"2.1.101.10734", L"07/19/2013", L"09/17/2025", false },
		{ L"8.782.0.0000", L"8.782.0.0", L"09/28/2010", L"09/28/2010", false },
		{ L"2.0.0.0", L"3.0.0.0", L"12/31/2030", L"01/01/2020", false },
		{ L"", L"", L"04/18/2020", L"04/17/2020", true }
	};
	for (const auto& item : cases)
	{
		DriverPackage package;
		package.driverVersion = item.packageVersion;
		package.driverDate = item.packageDate;
		if (IsPackageNewer(package, item.installedVersion, item.installedDate) != item.expected)
		{
			error = std::wstring(L"Driver version comparison failed: ") + item.packageVersion +
				L" / " + item.installedVersion;
			return false;
		}
	}
	if (!IsSystemBuiltInDriver(L"Microsoft", L"oem42.inf") ||
		!IsSystemBuiltInDriver(L"Intel", L"display.inf") ||
		IsSystemBuiltInDriver(L"Intel", L"oem42.inf"))
	{
		error = L"System built-in driver detection failed";
		return false;
	}
	error.clear();
	return true;
}

std::wstring DriverInstaller::Find7Zip(const std::wstring& dataRoot)
{
	// 驱动介质的固定布局是 <介质根>\Data 和 <介质根>\bin。
	const std::wstring media7Zip = JoinPath(JoinPath(ParentDirectory(dataRoot), L"bin"), L"7z.exe");
	if (GetFileAttributesW(media7Zip.c_str()) != INVALID_FILE_ATTRIBUTES) return media7Zip;
	return {};
}

namespace
{
	bool BuildPostInstallCommand(const std::wstring& type, const std::wstring& actionFile,
		const PostInstallAction& action, std::wstring& command)
	{
		if (type == L"INSTALL_INF")
		{
			command = L"pnputil.exe -i -a " + Quote(actionFile);
			return true;
		}
		const std::wstring upperFileName = Upper(FileNamePart(actionFile));
		if (type == L"MSI" || upperFileName.size() >= 4 &&
			upperFileName.substr(upperFileName.size() - 4) == L".MSI")
		{
			command = L"msiexec.exe /i " + Quote(actionFile);
			if (!action.arguments.empty()) command += L" " + action.arguments;
			if (action.preventReboot) command += L" REBOOT=ReallySuppress";
			return true;
		}
		if (type == L"EXECUTE" || type == L"COMMAND")
		{
			command = Quote(actionFile);
			if (!action.arguments.empty()) command += L" " + action.arguments;
			return true;
		}
		return false;
	}

	bool FinalizeInstalledDriver(bool afterInstallSucceeded, int& warningCount, std::wstring& error)
	{
		if (!afterInstallSucceeded && warningCount == 0) ++warningCount;
		error.clear();
		return true;
	}

	bool RunPostInstallActions(const std::wstring& sevenZip, const std::wstring& archive,
		const std::wstring& cache, const DriverPackage& driver, bool& rebootRequired,
		int& warningCount, std::wstring& error, const DriverInstaller::LogCallback& log)
	{
		auto reportWarning = [&](const std::wstring& warning)
		{
			++warningCount;
			if (log) log(warning);
		};
		for (const auto& action : driver.afterInstallActions)
		{
			const bool matches = action.match.empty() ||
				Upper(driver.infPath).find(Upper(action.match)) == 0;
			if (!matches) continue;
			const std::wstring type = Upper(action.type);
			std::wstring unusedCommand;
			if (!BuildPostInstallCommand(type, L"configured.file", action, unusedCommand))
			{
				error = L"Unsupported after_install action type: " + action.type;
				reportWarning(error);
				if (action.continueOnError) continue;
				return false;
			}
			if (action.file.empty())
			{
				error = L"after_install action has no configured file.";
				reportWarning(error);
				if (action.continueOnError) continue;
				return false;
			}
			if (action.preventReboot && IsBlockedRestartCommand(action.file, action.arguments))
			{
				error = L"Blocked after_install restart command: " + action.file;
				reportWarning(error);
				if (action.continueOnError) continue;
				return false;
			}
			std::wstring actionFile;
			if (!ResolvePostInstallFile(cache, driver, action, actionFile))
			{
				const std::wstring pattern = ActionExtractionPattern(driver, action);
				const std::wstring extract = Quote(sevenZip) + L" x -y -aoa -bd " +
					Quote(L"-o" + cache) + L" " + Quote(archive) + L" " + Quote(pattern);
				DWORD extractExit = 0;
				if (!RunProcess(extract, extractExit, error))
				{
					reportWarning(error);
					return false;
				}
				if (extractExit != 0)
				{
					error = std::wstring(Tr(TextId::SevenZipExtractFailed)) +
						std::to_wstring(extractExit) + L".";
					reportWarning(error);
					return false;
				}
				if (!ResolvePostInstallFile(cache, driver, action, actionFile))
				{
					error.clear();
					continue;
				}
			}
			std::wstring workingDirectory = action.workingDirectory.empty()
				? ParentDirectory(actionFile) : JoinPath(cache, action.workingDirectory);
			std::wstring command;
			if (!BuildPostInstallCommand(type, actionFile, action, command))
			{
				error = L"Failed to build after_install command: " + action.file;
				reportWarning(error);
				return false;
			}
			if (log) log(L"after_install: " + FileNamePart(actionFile));
			DWORD exitCode = 0;
			if (!RunProcess(command, exitCode, error, workingDirectory))
			{
				error = L"Failed to start after_install action: " + action.file + L"; " + error;
				reportWarning(error);
				if (action.continueOnError) continue;
				return false;
			}
			if (exitCode == ERROR_SUCCESS_REBOOT_REQUIRED || exitCode == ERROR_SUCCESS_REBOOT_INITIATED)
			{
				rebootRequired = true;
				continue;
			}
			if (exitCode != 0)
			{
				error = L"after_install action failed: " + action.file + L"; exit code " +
					std::to_wstring(exitCode) + L".";
				reportWarning(error);
				if (action.continueOnError) continue;
				return false;
			}
		}
		return true;
	}
}

bool DriverInstaller::Install(HWND ownerWindow, const std::wstring& dataRoot, const DeviceMatch& match,
	bool& rebootRequired, int& afterInstallWarnings, std::wstring& error,
	const LogCallback& log)
{
	rebootRequired = false;
	afterInstallWarnings = 0;
	const std::wstring sevenZip = Find7Zip(dataRoot);
	if (sevenZip.empty())
	{
		error = Tr(TextId::SevenZipNotFound);
		return false;
	}
	const std::wstring archive = JoinPath(JoinPath(dataRoot, match.driver.packageDirectory), match.driver.archiveFile);
	if (GetFileAttributesW(archive.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		error = std::wstring(Tr(TextId::ArchiveMissing)) + archive;
		return false;
	}
	std::wstring archiveBase = match.driver.archiveFile;
	const size_t extension = archiveBase.find_last_of(L'.');
	if (extension != std::wstring::npos) archiveBase.resize(extension);
	wchar_t windowsDirectory[MAX_PATH] = {};
	const std::wstring systemDriversRoot = GetWindowsDirectoryW(windowsDirectory,
		_countof(windowsDirectory)) == 0
		? L"C:\\Drivers"
		: JoinPath(ParentDirectory(windowsDirectory), L"Drivers");
	const std::wstring cache = JoinPath(JoinPath(systemDriversRoot, L"Extracted"),
		match.driver.packageDirectory + L"\\" + archiveBase);
	if (!EnsureDirectory(cache))
	{
		error = std::wstring(Tr(TextId::CacheCreateFailed)) + cache;
		return false;
	}
	const std::wstring inf = JoinPath(cache, match.driver.infPath);
	if (GetFileAttributesW(inf.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		const size_t slash = match.driver.infPath.find_last_of(L"\\/");
		const std::wstring pattern = slash == std::wstring::npos ? L"*" : match.driver.infPath.substr(0, slash) + L"\\*";
		if (log) log(std::wstring(Tr(TextId::Extracting)) + match.driver.archiveFile + L" -> " + pattern);
		const std::wstring command = Quote(sevenZip) + L" x -y -aoa -bd " + Quote(L"-o" + cache) + L" " + Quote(archive) + L" " + Quote(pattern);
		DWORD exitCode = 0;
		if (!RunProcess(command, exitCode, error))
		{
			error = std::wstring(Tr(TextId::SevenZipLaunchFailed)) + error;
			return false;
		}
		if (exitCode != 0)
		{
			error = std::wstring(Tr(TextId::SevenZipExtractFailed)) + std::to_wstring(exitCode) + L".";
			return false;
		}
	}
	if (GetFileAttributesW(inf.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		error = std::wstring(Tr(TextId::InfMissing)) + inf;
		return false;
	}
	const size_t infSlash = match.driver.infPath.find_last_of(L"\\/");
	const std::wstring infFileName = infSlash == std::wstring::npos
		? match.driver.infPath
		: match.driver.infPath.substr(infSlash + 1);
	if (log) log(std::wstring(Tr(TextId::InstallingInf)) + match.displayName + L" [" +
		infFileName + L"]");
	using UpdateDriverFn = BOOL(WINAPI*)(HWND, LPCWSTR, LPCWSTR, DWORD, PBOOL);
	HMODULE newDev = LoadLibraryW(L"newdev.dll");
	if (!newDev)
	{
		error = std::wstring(Tr(TextId::NewDevLoadFailed)) + FormatWin32Error(GetLastError());
		return false;
	}
	auto updateDriver = reinterpret_cast<UpdateDriverFn>(GetProcAddress(newDev, "UpdateDriverForPlugAndPlayDevicesW"));
	if (!updateDriver)
	{
		FreeLibrary(newDev);
		error = Tr(TextId::UpdateApiUnsupported);
		return false;
	}
	BOOL reboot = FALSE;
	const BOOL installed = updateDriver(ownerWindow, match.hardwareId.c_str(), inf.c_str(), INSTALLFLAG_FORCE, &reboot);
	const DWORD installError = installed ? ERROR_SUCCESS : GetLastError();
	FreeLibrary(newDev);
	if (!installed)
	{
		error = std::wstring(Tr(TextId::DriverInstallFailed)) + FormatWin32Error(installError) + L" (" + std::to_wstring(installError) + L")";
		return false;
	}
	rebootRequired = reboot != FALSE;
	const bool afterInstallSucceeded = RunPostInstallActions(sevenZip, archive, cache, match.driver,
		rebootRequired, afterInstallWarnings, error, log);
	// The primary INF is already installed. A failed optional follow-up action is
	// reported as a warning and must not turn the driver installation into a failure.
	return FinalizeInstalledDriver(afterInstallSucceeded, afterInstallWarnings, error);
}

bool DriverInstaller::RunAfterInstallTests(std::wstring& error)
{
	PostInstallAction action;
	action.file = L"shutdown.exe";
	action.arguments = L"/r /t 0";
	if (!IsBlockedRestartCommand(action.file, action.arguments))
	{
		error = L"after_install restart blocking test failed";
		return false;
	}
	DriverPackage package;
	package.infPath = L"Video.AMD1\\Public_New\\C7380461.inf";
	action.match = L"Video.AMD1\\Public_New";
	if (ActionExtractionPattern(package, action) != L"Video.AMD1\\Public_New\\*")
	{
		error = L"after_install extraction pattern test failed";
		return false;
	}
	action.relativeToDriverDirectory = true;
	package.infPath = L"Video.HP\\Acer_7750\\C7120845.inf";
	if (ActionExtractionPattern(package, action) != L"Video.HP\\Acer_7750\\*")
	{
		error = L"embedded after_install relative extraction test failed";
		return false;
	}
	PostInstallAction installInf;
	installInf.type = L"install_inf";
	std::wstring installInfCommand;
	if (!BuildPostInstallCommand(L"INSTALL_INF", L"C:\\Drivers\\iusb3hcs.inf",
		installInf, installInfCommand) ||
		installInfCommand != L"pnputil.exe -i -a \"C:\\Drivers\\iusb3hcs.inf\"")
	{
		error = L"after_install install_inf command test failed";
		return false;
	}
	PostInstallAction lowerCaseMsi;
	lowerCaseMsi.type = L"execute";
	lowerCaseMsi.arguments = L"/qn";
	lowerCaseMsi.preventReboot = true;
	std::wstring lowerCaseMsiCommand;
	if (!BuildPostInstallCommand(L"EXECUTE", L"C:\\Drivers\\package.msi",
		lowerCaseMsi, lowerCaseMsiCommand) ||
		lowerCaseMsiCommand != L"msiexec.exe /i \"C:\\Drivers\\package.msi\" /qn REBOOT=ReallySuppress")
	{
		error = L"after_install lowercase MSI command test failed";
		return false;
	}
	int simulatedWarnings = 0;
	std::wstring simulatedAfterInstallError = L"after_install action failed";
	if (!FinalizeInstalledDriver(false, simulatedWarnings, simulatedAfterInstallError) ||
		simulatedWarnings != 1 || !simulatedAfterInstallError.empty())
	{
		error = L"after_install warning classification test failed";
		return false;
	}
	error.clear();
	return true;
}
