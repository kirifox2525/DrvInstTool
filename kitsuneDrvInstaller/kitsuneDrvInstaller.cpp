
// kitsuneDrvInstaller.cpp: 定义应用程序的类行为。
//

#include "pch.h"
#include "framework.h"
#include "afxwinappex.h"
#include "afxdialogex.h"
#include "kitsuneDrvInstaller.h"
#include "MainFrm.h"

#include "ChildFrm.h"
#include "kitsuneDrvInstallerDoc.h"
#include "kitsuneDrvInstallerView.h"
#include "Localization.h"
#include "DriverEngine.h"
#include <shellapi.h>
#include <fstream>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CkitsuneDrvInstallerApp

BEGIN_MESSAGE_MAP(CkitsuneDrvInstallerApp, CWinAppEx)
	ON_COMMAND(ID_APP_ABOUT, &CkitsuneDrvInstallerApp::OnAppAbout)
	// 基于文件的标准文档命令
	ON_COMMAND(ID_FILE_NEW, &CWinAppEx::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, &CWinAppEx::OnFileOpen)
	// 标准打印设置命令
	ON_COMMAND(ID_FILE_PRINT_SETUP, &CWinAppEx::OnFilePrintSetup)
END_MESSAGE_MAP()


// CkitsuneDrvInstallerApp 构造

CkitsuneDrvInstallerApp::CkitsuneDrvInstallerApp() noexcept
{
	m_bHiColorIcons = TRUE;


	// 支持重新启动管理器
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_ALL_ASPECTS;
#ifdef _MANAGED
	// 如果应用程序是利用公共语言运行时支持(/clr)构建的，则: 
	//     1) 必须有此附加设置，“重新启动管理器”支持才能正常工作。
	//     2) 在您的项目中，您必须按照生成顺序向 System.Windows.Forms 添加引用。
	System::Windows::Forms::Application::SetUnhandledExceptionMode(System::Windows::Forms::UnhandledExceptionMode::ThrowException);
#endif

	// TODO: 将以下应用程序 ID 字符串替换为唯一的 ID 字符串；建议的字符串格式
	//为 CompanyName.ProductName.SubProduct.VersionInformation
	SetAppID(_T("kiri.DriverInstaller.1"));

	// TODO:  在此处添加构造代码，
	// 将所有重要的初始化放置在 InitInstance 中
}

// 唯一的 CkitsuneDrvInstallerApp 对象

CkitsuneDrvInstallerApp theApp;

namespace
{
	bool HasAiArgument()
	{
		int count = 0;
		LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
		if (!arguments) return false;
		bool found = false;
		for (int index = 1; index < count; ++index)
			if (_wcsicmp(arguments[index], L"/ai") == 0) { found = true; break; }
		LocalFree(arguments);
		return found;
	}

	void ShowTimedCompatibilityError(const std::wstring& message)
	{
		const DWORD started = GetTickCount();
		using MessageBoxTimeoutProc = int (WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT, WORD, DWORD);
		const HMODULE user32 = GetModuleHandleW(L"user32.dll");
		const auto messageBoxTimeout = reinterpret_cast<MessageBoxTimeoutProc>(
			GetProcAddress(user32, "MessageBoxTimeoutW"));
		if (messageBoxTimeout)
			messageBoxTimeout(nullptr, message.c_str(), Tr(TextId::UnsupportedSystemTitle),
				MB_OK | MB_ICONERROR | MB_SETFOREGROUND, 0, 5000);
		else
			MessageBoxW(nullptr, message.c_str(), Tr(TextId::UnsupportedSystemTitle),
				MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
		const DWORD elapsed = GetTickCount() - started;
		if (elapsed < 5000) Sleep(5000 - elapsed);
	}

	std::wstring ProgramDataRoot()
	{
		wchar_t module[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, module, _countof(module));
		std::wstring executablePath(module);
		const size_t slash = executablePath.find_last_of(L"\\/");
		const std::wstring programDirectory = slash == std::wstring::npos
			? std::wstring()
			: executablePath.substr(0, slash);
		return programDirectory + L"\\Data";
	}

	std::string JsonEscapeUtf8(const std::wstring& value)
	{
		if (value.empty()) return {};
		const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		std::string utf8(static_cast<size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &utf8[0], size, nullptr, nullptr);
		std::string escaped;
		for (char ch : utf8)
		{
			if (ch == '\\' || ch == '"') escaped.push_back('\\');
			if (ch == '\n') escaped += "\\n";
			else if (ch == '\r') escaped += "\\r";
			else escaped.push_back(ch);
		}
		return escaped;
	}

	bool RunSelfTestIfRequested()
	{
		int count = 0;
		LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
		if (!arguments) return false;
		int option = -1;
		for (int index = 1; index < count; ++index)
			if (_wcsicmp(arguments[index], L"--self-test") == 0) { option = index; break; }
		if (option < 0)
		{
			LocalFree(arguments);
			return false;
		}
		const std::wstring dataRoot = option + 1 < count ? arguments[option + 1] : L"Data";
		const std::wstring reportPath = option + 2 < count ? arguments[option + 2] : L"driver-installer-self-test.json";
		DriverCatalog catalog;
		std::wstring error;
		std::wstring compatibilityError;
		const bool compatibilityRulesOk = SystemCompatibility::RunRuleTests(compatibilityError);
		std::wstring driverVersionError;
		const bool driverVersionRulesOk = DeviceScanner::RunVersionComparisonTests(driverVersionError);
		std::wstring driverMatchingError;
		const bool driverMatchingRulesOk = DriverCatalog::RunMatchingTests(driverMatchingError);
		std::wstring afterInstallError;
		const bool afterInstallRulesOk = DriverInstaller::RunAfterInstallTests(afterInstallError);
		const std::wstring multilineLog = CkitsuneDrvInstallerView::FormatTimestampedLogForTest(
			L"line one\r\nline two\nline three", 1, 2, 3);
		const bool timestampedLogLinesOk = multilineLog ==
			L"[01:02:03] line one\r\n[01:02:03] line two\r\n[01:02:03] line three";
		std::wstring layoutError;
		const bool layout640x400Ok = CkitsuneDrvInstallerView::RunLayoutTests(layoutError);
		std::wstring windowSizeError;
		const bool fixedWindowSizeOk = CMainFrame::RunWindowSizeTests(windowSizeError);
		std::wstring mediaCompatibilityError;
		const bool mediaCompatibilityOk = SystemCompatibility::ValidateDriverMedia(dataRoot, mediaCompatibilityError);
		std::wstring mediaTargetSystem;
		std::wstring mediaTargetArchitecture;
		const bool mediaTargetOk = SystemCompatibility::GetDriverMediaTarget(
			dataRoot, mediaTargetSystem, mediaTargetArchitecture);
		const bool catalogLoaded = catalog.Load(dataRoot, error);
		std::vector<DeviceMatch> matches;
		int scanned = 0;
		bool scanOk = false;
		if (catalogLoaded) scanOk = DeviceScanner::Scan(dataRoot, catalog, matches, scanned, error);
		size_t missingDrivers = 0, updateDrivers = 0, installedDrivers = 0;
		size_t systemBuiltInDrivers = 0;
		for (const auto& match : matches)
		{
			if (match.usesSystemBuiltInDriver) ++systemBuiltInDrivers;
			if (match.needsDriver) ++missingDrivers;
			else if (match.updateAvailable) ++updateDrivers;
			else ++installedDrivers;
		}
		size_t indexedNameFallbackCount = 0;
		for (const auto& match : matches)
			if (!match.indexedDeviceName.empty() && match.displayName == match.indexedDeviceName)
				++indexedNameFallbackCount;
		size_t ambiguousBluetoothProfileMatches = 0;
		for (const auto& match : matches)
		{
			const std::wstring hardwareId = match.hardwareId;
			if (hardwareId.size() >= 9 && _wcsnicmp(hardwareId.c_str(), L"BTHENUM\\{", 9) == 0 &&
				_wcsicmp(match.driver.deviceClass.c_str(), L"BluetoothAuxiliary") == 0)
				++ambiguousBluetoothProfileMatches;
		}
		const std::wstring sevenZip = DriverInstaller::Find7Zip(dataRoot);
		std::ofstream report(reportPath, std::ios::binary | std::ios::trunc);
		report << "{\n"
			<< "  \"compatibility_rules_ok\": " << (compatibilityRulesOk ? "true" : "false") << ",\n"
			<< "  \"compatibility_error\": \"" << JsonEscapeUtf8(compatibilityError) << "\",\n"
			<< "  \"driver_version_rules_ok\": " << (driverVersionRulesOk ? "true" : "false") << ",\n"
			<< "  \"driver_version_error\": \"" << JsonEscapeUtf8(driverVersionError) << "\",\n"
			<< "  \"driver_matching_rules_ok\": " << (driverMatchingRulesOk ? "true" : "false") << ",\n"
			<< "  \"driver_matching_error\": \"" << JsonEscapeUtf8(driverMatchingError) << "\",\n"
			<< "  \"after_install_rules_ok\": " << (afterInstallRulesOk ? "true" : "false") << ",\n"
			<< "  \"after_install_error\": \"" << JsonEscapeUtf8(afterInstallError) << "\",\n"
			<< "  \"timestamped_log_lines_ok\": " << (timestampedLogLinesOk ? "true" : "false") << ",\n"
			<< "  \"timestamped_log_output\": \"" << JsonEscapeUtf8(multilineLog) << "\",\n"
			<< "  \"layout_640x400_ok\": " << (layout640x400Ok ? "true" : "false") << ",\n"
			<< "  \"layout_640x400_error\": \"" << JsonEscapeUtf8(layoutError) << "\",\n"
			<< "  \"fixed_window_size_ok\": " << (fixedWindowSizeOk ? "true" : "false") << ",\n"
			<< "  \"fixed_window_size_error\": \"" << JsonEscapeUtf8(windowSizeError) << "\",\n"
			<< "  \"media_compatibility_ok\": " << (mediaCompatibilityOk ? "true" : "false") << ",\n"
			<< "  \"media_compatibility_error\": \"" << JsonEscapeUtf8(mediaCompatibilityError) << "\",\n"
			<< "  \"media_target_ok\": " << (mediaTargetOk ? "true" : "false") << ",\n"
			<< "  \"media_target_system\": \"" << JsonEscapeUtf8(mediaTargetSystem) << "\",\n"
			<< "  \"media_target_architecture\": \"" << JsonEscapeUtf8(mediaTargetArchitecture) << "\",\n"
			<< "  \"catalog_loaded\": " << (catalogLoaded ? "true" : "false") << ",\n"
			<< "  \"driver_count\": " << catalog.DriverCount() << ",\n"
			<< "  \"after_install_action_count\": " << catalog.AfterInstallActionCount() << ",\n"
			<< "  \"hardware_id_count\": " << catalog.HardwareIdCount() << ",\n"
			<< "  \"device_scan_ok\": " << (scanOk ? "true" : "false") << ",\n"
			<< "  \"scanned_device_count\": " << scanned << ",\n"
			<< "  \"matched_device_count\": " << matches.size() << ",\n"
			<< "  \"missing_driver_count\": " << missingDrivers << ",\n"
			<< "  \"update_driver_count\": " << updateDrivers << ",\n"
			<< "  \"installed_driver_count\": " << installedDrivers << ",\n"
			<< "  \"system_builtin_driver_count\": " << systemBuiltInDrivers << ",\n"
			<< "  \"indexed_name_fallback_count\": " << indexedNameFallbackCount << ",\n"
			<< "  \"ambiguous_bluetooth_profile_match_count\": " << ambiguousBluetoothProfileMatches << ",\n"
			<< "  \"matches\": [\n";
		for (size_t index = 0; index < matches.size(); ++index)
		{
			const auto& match = matches[index];
			report << "    {\"device\": \"" << JsonEscapeUtf8(match.displayName)
				<< "\", \"hardware_id\": \"" << JsonEscapeUtf8(match.hardwareId)
				<< "\", \"driver_id\": \"" << JsonEscapeUtf8(match.driver.id)
				<< "\", \"provider\": \"" << JsonEscapeUtf8(match.driver.provider)
				<< "\", \"installed_provider\": \"" << JsonEscapeUtf8(match.installedDriverProvider)
				<< "\", \"system_builtin_driver\": " << (match.usesSystemBuiltInDriver ? "true" : "false")
				<< ", \"uses_indexed_name\": " <<
					(!match.indexedDeviceName.empty() && match.displayName == match.indexedDeviceName ? "true" : "false")
				<< ", \"device_class\": \"" << JsonEscapeUtf8(match.driver.deviceClass)
				<< "\"}" << (index + 1 < matches.size() ? "," : "") << "\n";
		}
		report << "  ],\n"
			<< "  \"seven_zip\": \"" << JsonEscapeUtf8(sevenZip) << "\",\n"
			<< "  \"error\": \"" << JsonEscapeUtf8(error) << "\"\n"
			<< "}\n";
		report.close();
		LocalFree(arguments);
		return true;
	}
}


// CkitsuneDrvInstallerApp 初始化

BOOL CkitsuneDrvInstallerApp::InitInstance()
{
	// 如果一个运行在 Windows XP 上的应用程序清单指定要
	// 使用 ComCtl32.dll 版本 6 或更高版本来启用可视化方式，
	//则需要 InitCommonControlsEx()。  否则，将无法创建窗口。
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// 将它设置为包括所有要在应用程序中使用的
	// 公共控件类。
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinAppEx::InitInstance();
	m_aiMode = HasAiArgument();
	Localization::SetLanguage(Localization::DetectSystemLanguage());
	if (RunSelfTestIfRequested()) return FALSE;
	const std::wstring dataRoot = ProgramDataRoot();
	const std::wstring configPath = dataRoot + L"\\config.json";
	if (GetFileAttributesW(configPath.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		std::wstring compatibilityError;
		if (!SystemCompatibility::ValidateDriverMedia(dataRoot, compatibilityError))
		{
			if (m_aiMode) ShowTimedCompatibilityError(compatibilityError);
			else MessageBoxW(nullptr, compatibilityError.c_str(), Tr(TextId::UnsupportedSystemTitle),
					MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
			return FALSE;
		}
	}


	// 初始化 OLE 库
	if (!AfxOleInit())
	{
		AfxMessageBox(IDP_OLE_INIT_FAILED);
		return FALSE;
	}

	AfxEnableControlContainer();

	EnableTaskbarInteraction();

	// 使用 RichEdit 控件需要 AfxInitRichEdit2()
	// AfxInitRichEdit2();

	// 标准初始化
	// 如果未使用这些功能并希望减小
	// 最终可执行文件的大小，则应移除下列
	// 不需要的特定初始化例程
	// 更改用于存储设置的注册表项
	// TODO: 应适当修改该字符串，
	// 例如修改为公司或组织名
	SetRegistryKey(_T("kitsune"));
	LoadStdProfileSettings(4);  // 加载标准 INI 文件选项(包括 MRU)


	InitContextMenuManager();

	InitKeyboardManager();

	InitTooltipManager();
	CMFCToolTipInfo ttParams;
	ttParams.m_bVislManagerTheme = TRUE;
	theApp.GetTooltipManager()->SetTooltipParams(AFX_TOOLTIP_TYPE_ALL,
		RUNTIME_CLASS(CMFCToolTipCtrl), &ttParams);

	// 注册应用程序的文档模板。  文档模板
	// 将用作文档、框架窗口和视图之间的连接
	CMultiDocTemplate* pDocTemplate;
	pDocTemplate = new CMultiDocTemplate(IDR_kitsuneDrvInstallerTYPE,
		RUNTIME_CLASS(CkitsuneDrvInstallerDoc),
		RUNTIME_CLASS(CChildFrame), // 自定义 MDI 子框架
		RUNTIME_CLASS(CkitsuneDrvInstallerView));
	if (!pDocTemplate)
		return FALSE;
	AddDocTemplate(pDocTemplate);

	// 创建主 MDI 框架窗口
	CMainFrame* pMainFrame = new CMainFrame;
	if (!pMainFrame || !pMainFrame->LoadFrame(IDR_MAINFRAME))
	{
		delete pMainFrame;
		return FALSE;
	}
	// LoadFrame may restore a saved registry position. Override it while the
	// frame is still hidden, before ProcessShellCommand creates the visible UI.
	pMainFrame->CenterBeforeFirstShow();
	m_pMainWnd = pMainFrame;


	// 分析标准 shell 命令、DDE、打开文件操作的命令行
	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);



	// 调度在命令行中指定的命令。  如果
	// 用 /RegServer、/Register、/Unregserver 或 /Unregister 启动应用程序，则返回 FALSE。
	if (!ProcessShellCommand(cmdInfo))
		return FALSE;
	// 安装器采用单页界面，不显示文档模板生成的编辑菜单。
	pMainFrame->SetMenu(nullptr);
	pMainFrame->DrawMenuBar();
	if (pMainFrame->MDIGetActive() != nullptr)
		pMainFrame->MDIMaximize(pMainFrame->MDIGetActive());
	// 主窗口已初始化，因此显示它并对其进行更新
	pMainFrame->ShowWindow(SW_SHOWNORMAL);
	pMainFrame->UpdateWindow();

	return TRUE;
}

BOOL CkitsuneDrvInstallerApp::PreTranslateMessage(MSG* message)
{
	if (m_aiMode && m_pMainWnd && message)
	{
		const bool mouseInput = message->message >= WM_LBUTTONDOWN && message->message <= WM_XBUTTONDBLCLK;
		const bool nonClientMouseInput = message->message >= WM_NCLBUTTONDOWN && message->message <= WM_NCXBUTTONDBLCLK;
		const bool keyboardInput = message->message >= WM_KEYFIRST && message->message <= WM_KEYLAST;
		const bool belongsToApplication = message->hwnd == m_pMainWnd->GetSafeHwnd() ||
			(message->hwnd != nullptr && ::IsChild(m_pMainWnd->GetSafeHwnd(), message->hwnd));
		if (belongsToApplication && (mouseInput || nonClientMouseInput || keyboardInput))
			NotifyAiUserActivity();
	}
	return CWinAppEx::PreTranslateMessage(message);
}

void CkitsuneDrvInstallerApp::NotifyAiUserActivity()
{
	if (!m_aiMode) return;
	CkitsuneDrvInstallerView* view =
		DYNAMIC_DOWNCAST(CkitsuneDrvInstallerView, m_installerView);
	if (view) view->CancelAiCountdownForUserInput();
}

int CkitsuneDrvInstallerApp::ExitInstance()
{
	//TODO: 处理可能已添加的附加资源
	AfxOleTerm(FALSE);

	return CWinAppEx::ExitInstance();
}

// CkitsuneDrvInstallerApp 消息处理程序


// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg() noexcept;

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() noexcept : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

// 用于运行对话框的应用程序命令
void CkitsuneDrvInstallerApp::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}

// CkitsuneDrvInstallerApp 自定义加载/保存方法

void CkitsuneDrvInstallerApp::PreLoadState()
{
	BOOL bNameValid;
	CString strName;
	bNameValid = strName.LoadString(IDS_EDIT_MENU);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EDIT);
	bNameValid = strName.LoadString(IDS_EXPLORER);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EXPLORER);
}

void CkitsuneDrvInstallerApp::LoadCustomState()
{
}

void CkitsuneDrvInstallerApp::SaveCustomState()
{
}

// CkitsuneDrvInstallerApp 消息处理程序



