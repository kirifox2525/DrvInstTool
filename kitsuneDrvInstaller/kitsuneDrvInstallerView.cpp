#include "pch.h"
#include "framework.h"
#include "kitsuneDrvInstaller.h"
#include "kitsuneDrvInstallerDoc.h"
#include "kitsuneDrvInstallerView.h"
#include "Localization.h"
#include "MainFrm.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	constexpr COLORREF ViewBackgroundColor = RGB(245, 247, 250);

	enum ControlId
	{
		IDC_SCAN = 2003,
		IDC_SELECT_RECOMMENDED = 2004,
		IDC_INSTALL = 2005,
		IDC_DEVICE_LIST = 2006,
		IDC_PROGRESS = 2007,
		IDC_LOG = 2008
		, IDC_LANGUAGE = 2009
	};
	constexpr UINT WM_AUTO_SCAN = WM_APP + 1;
	constexpr UINT_PTR AI_INSTALL_TIMER = 2;
	constexpr UINT_PTR AI_EXIT_TIMER = 3;

	struct InstallerLayout
	{
		CRect title;
		CRect languageLabel;
		CRect language;
		CRect scan;
		CRect selectRecommended;
		CRect install;
		CRect devices;
		CRect progress;
		CRect logLabel;
		CRect log;
		int columnWidths[5] = {};
	};

	InstallerLayout ComputeInstallerLayout(int width, int height,
		int titleTextHeight, int uiTextHeight)
	{
		InstallerLayout layout;
		const bool ultraCompact = width < 700 || height < 450;
		const bool compact = ultraCompact || width < 900 || height < 650;
		const int margin = ultraCompact ? 6 : (compact ? 12 : 22);
		const int gap = ultraCompact ? 4 : (compact ? 6 : 8);
		const int contentWidth = max(1, width - margin * 2);
		const int titleTop = ultraCompact ? 2 : (compact ? 5 : 12);
		const int titleHeight = max(ultraCompact ? 24 : (compact ? 30 : 36),
			titleTextHeight + (ultraCompact ? 2 : (compact ? 6 : 12)));
		const int languageWidth = ultraCompact ? 108 : (compact ? 140 : 172);
		const int languageLabelWidth = ultraCompact ? 44 : (compact ? 58 : 74);
		const int languageX = width - margin - languageWidth;
		const int languageLabelX = languageX - gap - languageLabelWidth;
		layout.title = CRect(margin, titleTop,
			max(margin + 1, languageLabelX - gap), titleTop + titleHeight);
		layout.languageLabel = CRect(languageLabelX,
			titleTop + (ultraCompact ? 1 : (compact ? 3 : 5)),
			languageX - gap, titleTop + (ultraCompact ? 23 : 27));
		layout.language = CRect(languageX, titleTop + 1,
			languageX + languageWidth, titleTop + 25);

		const int scanWidth = ultraCompact ? 72 : (compact ? 90 : 112);
		const int selectWidth = ultraCompact ? 88 : (compact ? 110 : 140);
		const int installWidth = ultraCompact ? 88 : (compact ? 110 : 140);
		const int installX = width - margin - installWidth;
		const int selectX = installX - gap - selectWidth;
		const int scanX = selectX - gap - scanWidth;
		const int actionsTop = titleTop + titleHeight + (ultraCompact ? 2 : gap);
		const int actionHeight = ultraCompact ? 24 : (compact ? 28 : 32);
		layout.scan = CRect(scanX, actionsTop, scanX + scanWidth, actionsTop + actionHeight);
		layout.selectRecommended = CRect(selectX, actionsTop,
			selectX + selectWidth, actionsTop + actionHeight);
		layout.install = CRect(installX, actionsTop,
			installX + installWidth, actionsTop + actionHeight);

		const int devicesTop = actionsTop + actionHeight + (ultraCompact ? 3 : (compact ? 7 : 11));
		const int logHeight = ultraCompact ? max(48, min(64, height / 6))
			: (compact ? max(78, min(105, height / 5)) : max(96, min(150, height / 5)));
		const int logTop = height - margin - logHeight;
		const int logLabelHeight = max(ultraCompact ? 18 : (compact ? 24 : 26),
			uiTextHeight + (ultraCompact ? 2 : 6));
		const int progressHeight = ultraCompact ? 8 : (compact ? 13 : 17);
		const int logLabelY = logTop - logLabelHeight - (ultraCompact ? 1 : 2);
		const int progressY = logLabelY - gap - progressHeight;
		const int devicesBottom = max(devicesTop + 1, progressY - gap);
		layout.devices = CRect(margin, devicesTop, margin + contentWidth, devicesBottom);
		layout.progress = CRect(margin, progressY, margin + contentWidth, progressY + progressHeight);
		layout.logLabel = CRect(margin, logLabelY, min(width - margin, margin + 160),
			logLabelY + logLabelHeight);
		layout.log = CRect(margin, logTop, margin + contentWidth,
			max(logTop + 1, height - margin));

		const int columnWidth = max(5, contentWidth - 4);
		layout.columnWidths[1] = columnWidth * 20 / 100;
		layout.columnWidths[2] = columnWidth * 15 / 100;
		layout.columnWidths[3] = columnWidth * 15 / 100;
		layout.columnWidths[4] = columnWidth * 13 / 100;
		layout.columnWidths[0] = columnWidth - layout.columnWidths[1] -
			layout.columnWidths[2] - layout.columnWidths[3] - layout.columnWidths[4];
		return layout;
	}

	bool RectInside(const CRect& rect, int width, int height)
	{
		return rect.left >= 0 && rect.top >= 0 && rect.right <= width &&
			rect.bottom <= height && rect.Width() > 0 && rect.Height() > 0;
	}
	bool FileExists(const std::wstring& path)
	{
		return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
	{
		return left.empty() || left.back() == L'\\' ? left + right : left + L"\\" + right;
	}

	std::wstring ParentDirectory(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
	}

	std::wstring ProgramDataRoot()
	{
		wchar_t module[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, module, _countof(module));
		return JoinPath(ParentDirectory(module), L"Data");
	}

	std::wstring SystemDriversRoot()
	{
		wchar_t windowsDirectory[MAX_PATH] = {};
		if (GetWindowsDirectoryW(windowsDirectory, _countof(windowsDirectory)) == 0)
			return L"C:\\Drivers";
		return JoinPath(ParentDirectory(windowsDirectory), L"Drivers");
	}

	void PumpMessages()
	{
		MSG message;
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}

	void ShowSystemCompatibilityError(HWND owner, const std::wstring& message)
	{
		MessageBoxW(owner, message.c_str(), Tr(TextId::UnsupportedSystemTitle), MB_OK | MB_ICONERROR);
	}
}

IMPLEMENT_DYNCREATE(CkitsuneDrvInstallerView, CView)

BEGIN_MESSAGE_MAP(CkitsuneDrvInstallerView, CView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_SCAN, &CkitsuneDrvInstallerView::OnScan)
	ON_BN_CLICKED(IDC_SELECT_RECOMMENDED, &CkitsuneDrvInstallerView::OnSelectRecommended)
	ON_BN_CLICKED(IDC_INSTALL, &CkitsuneDrvInstallerView::OnInstall)
	ON_CBN_SELCHANGE(IDC_LANGUAGE, &CkitsuneDrvInstallerView::OnLanguageChanged)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_DEVICE_LIST, &CkitsuneDrvInstallerView::OnDeviceItemChanged)
	ON_MESSAGE(WM_AUTO_SCAN, &CkitsuneDrvInstallerView::OnAutoScan)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CkitsuneDrvInstallerView::CkitsuneDrvInstallerView() noexcept {}
CkitsuneDrvInstallerView::~CkitsuneDrvInstallerView()
{
	if (theApp.m_pMainWnd != nullptr) theApp.SetInstallerView(nullptr);
}

LRESULT CkitsuneDrvInstallerView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	const bool mouseInput = message >= WM_LBUTTONDOWN && message <= WM_XBUTTONDBLCLK;
	const bool keyboardInput = message >= WM_KEYFIRST && message <= WM_KEYLAST;
	if (mouseInput || keyboardInput) theApp.NotifyAiUserActivity();
	return CView::WindowProc(message, wParam, lParam);
}

BOOL CkitsuneDrvInstallerView::PreCreateWindow(CREATESTRUCT& cs)
{
	cs.style |= WS_CLIPCHILDREN;
	return CView::PreCreateWindow(cs);
}

int CkitsuneDrvInstallerView::OnCreate(LPCREATESTRUCT createStruct)
{
	if (CView::OnCreate(createStruct) == -1) return -1;
	theApp.SetInstallerView(this);
	if (!m_backgroundBrush.CreateSolidBrush(ViewBackgroundColor)) return -1;

	NONCLIENTMETRICSW metrics = {};
	metrics.cbSize = sizeof(metrics);
	LOGFONTW uiLogFont = {};
	if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
		uiLogFont = metrics.lfMessageFont;
	else
	{
		uiLogFont.lfHeight = -12;
		uiLogFont.lfCharSet = DEFAULT_CHARSET;
		uiLogFont.lfQuality = CLEARTYPE_QUALITY;
		wcscpy_s(uiLogFont.lfFaceName, L"Segoe UI");
	}
	uiLogFont.lfQuality = CLEARTYPE_QUALITY;
	if (!m_uiFont.CreateFontIndirectW(&uiLogFont)) return -1;
	LOGFONTW titleLogFont = uiLogFont;
	CClientDC viewDc(this);
	titleLogFont.lfHeight = -MulDiv(18, viewDc.GetDeviceCaps(LOGPIXELSY), 72);
	titleLogFont.lfWeight = FW_SEMIBOLD;
	if (!m_titleFont.CreateFontIndirectW(&titleLogFont)) return -1;
	Localization::SetLanguage(Localization::DetectSystemLanguage());
	m_aiMode = theApp.IsAiMode();
	m_aiInteractionEnabled = !m_aiMode;
	m_title.Create(L"", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_NOPREFIX | SS_ENDELLIPSIS, CRect(), this);
	m_title.SetFont(&m_titleFont);
	m_languageLabel.Create(L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, CRect(), this);
	m_languageLabel.SetFont(&m_uiFont);
	if (!m_language.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
		CBS_HASSTRINGS | CBS_NOINTEGRALHEIGHT | WS_VSCROLL, CRect(0, 0, 180, 240), this, IDC_LANGUAGE))
		return -1;
	m_language.SetFont(&m_uiFont);
	if (!InitializeLanguageSelector()) return -1;
	m_scan.Create(L"", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, CRect(), this, IDC_SCAN);
	m_selectRecommended.Create(L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, IDC_SELECT_RECOMMENDED);
	m_install.Create(L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, IDC_INSTALL);
	m_scan.SetFont(&m_uiFont);
	m_selectRecommended.SetFont(&m_uiFont);
	m_install.SetFont(&m_uiFont);
	m_install.EnableWindow(FALSE);
	m_devices.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, CRect(), this, IDC_DEVICE_LIST);
	m_devices.SetFont(&m_uiFont);
	m_devices.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
	m_devices.InsertColumn(0, L"", LVCFMT_LEFT, 335);
	m_devices.InsertColumn(1, L"", LVCFMT_LEFT, 210);
	m_devices.InsertColumn(2, L"", LVCFMT_LEFT, 165);
	m_devices.InsertColumn(3, L"", LVCFMT_LEFT, 155);
	m_devices.InsertColumn(4, L"", LVCFMT_LEFT, 135);
	if (m_devices.GetHeaderCtrl()) m_devices.GetHeaderCtrl()->SetFont(&m_uiFont);
	m_progress.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, CRect(), this, IDC_PROGRESS);
	m_logLabel.Create(L"", WS_CHILD | WS_VISIBLE, CRect(), this);
	m_logLabel.SetFont(&m_uiFont);
	m_log.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
		CRect(), this, IDC_LOG);
	m_log.SetFont(&m_uiFont);
	ApplyLanguage();
	UpdateInteractionState();
	CRect clientRect;
	GetClientRect(&clientRect);
	LayoutControls(clientRect.Width(), clientRect.Height());
	return 0;
}

void CkitsuneDrvInstallerView::OnInitialUpdate()
{
	CView::OnInitialUpdate();
	if (m_language.GetCount() != 3 || m_language.GetCurSel() == CB_ERR)
		InitializeLanguageSelector();
	UpdateTitle();
	if (FileExists(JoinPath(CurrentDataRoot(), L"config.json")))
		PostMessageW(WM_AUTO_SCAN);
}

LRESULT CkitsuneDrvInstallerView::OnAutoScan(WPARAM, LPARAM)
{
	OnScan();
	return 0;
}

void CkitsuneDrvInstallerView::OnDraw(CDC*) {}

BOOL CkitsuneDrvInstallerView::OnEraseBkgnd(CDC* dc)
{
	CRect rect;
	GetClientRect(&rect);
	dc->FillSolidRect(rect, ViewBackgroundColor);
	return TRUE;
}

HBRUSH CkitsuneDrvInstallerView::OnCtlColor(CDC* dc, CWnd* window, UINT controlColor)
{
	HBRUSH brush = CView::OnCtlColor(dc, window, controlColor);
	if (controlColor == CTLCOLOR_STATIC)
	{
		dc->SetBkMode(TRANSPARENT);
		return static_cast<HBRUSH>(m_backgroundBrush.GetSafeHandle());
	}
	return brush;
}

void CkitsuneDrvInstallerView::OnSize(UINT type, int cx, int cy)
{
	CView::OnSize(type, cx, cy);
	if (m_title.GetSafeHwnd()) LayoutControls(cx, cy);
}

void CkitsuneDrvInstallerView::LayoutControls(int width, int height)
{
	CClientDC dc(this);
	CFont* previousFont = dc.SelectObject(&m_titleFont);
	TEXTMETRICW titleMetrics = {};
	dc.GetTextMetricsW(&titleMetrics);
	dc.SelectObject(&m_uiFont);
	TEXTMETRICW uiMetrics = {};
	dc.GetTextMetricsW(&uiMetrics);
	dc.SelectObject(previousFont);
	const InstallerLayout layout = ComputeInstallerLayout(width, height,
		titleMetrics.tmHeight + titleMetrics.tmExternalLeading,
		uiMetrics.tmHeight + uiMetrics.tmExternalLeading);
	m_title.MoveWindow(layout.title);
	m_languageLabel.MoveWindow(layout.languageLabel);
	m_language.SetWindowPos(nullptr, layout.language.left, layout.language.top,
		layout.language.Width(), 240, SWP_NOZORDER | SWP_SHOWWINDOW);
	m_scan.MoveWindow(layout.scan);
	m_selectRecommended.MoveWindow(layout.selectRecommended);
	m_install.MoveWindow(layout.install);
	m_devices.MoveWindow(layout.devices);
	m_progress.MoveWindow(layout.progress);
	m_logLabel.MoveWindow(layout.logLabel);
	m_log.MoveWindow(layout.log);
	for (int index = 0; index < 5; ++index)
		m_devices.SetColumnWidth(index, layout.columnWidths[index]);
}

bool CkitsuneDrvInstallerView::RunLayoutTests(std::wstring& error)
{
	// A 640x400 monitor leaves roughly 600x280 client pixels after the fixed
	// frame, caption, status bar and taskbar are removed. Conservative font
	// metrics also cover classic Windows and high-DPI text.
	const int width = 600;
	const int height = 280;
	const InstallerLayout layout = ComputeInstallerLayout(width, height, 30, 22);
	const CRect controls[] = { layout.title, layout.languageLabel, layout.language,
		layout.scan, layout.selectRecommended, layout.install, layout.devices,
		layout.progress, layout.logLabel, layout.log };
	for (const CRect& control : controls)
	{
		if (RectInside(control, width, height)) continue;
		error = L"640x400 layout places a control outside the client area";
		return false;
	}
	if (layout.title.right > layout.languageLabel.left ||
		layout.scan.right > layout.selectRecommended.left ||
		layout.selectRecommended.right > layout.install.left ||
		layout.devices.bottom > layout.progress.top ||
		layout.progress.bottom > layout.logLabel.top ||
		layout.logLabel.bottom > layout.log.top)
	{
		error = L"640x400 layout contains overlapping controls";
		return false;
	}
	error.clear();
	return true;
}
bool CkitsuneDrvInstallerView::InitializeLanguageSelector()
{
	if (!m_language.GetSafeHwnd()) return false;
	m_language.SetRedraw(FALSE);
	m_language.ResetContent();
	const wchar_t* const languages[] = { L"English", L"简体中文", L"繁體中文" };
	for (const wchar_t* language : languages)
	{
		if (m_language.AddString(language) != CB_ERR) continue;
		m_language.SetRedraw(TRUE);
		return false;
	}
	int selection = static_cast<int>(Localization::GetLanguage());
	if (selection < 0 || selection >= _countof(languages)) selection = 0;
	if (m_language.SetCurSel(selection) == CB_ERR) m_language.SetCurSel(0);
	m_language.SetRedraw(TRUE);
	m_language.ShowWindow(SW_SHOW);
	m_language.Invalidate(FALSE);
	return m_language.GetCount() == _countof(languages) && m_language.GetCurSel() != CB_ERR;
}

std::wstring CkitsuneDrvInstallerView::CurrentDataRoot() const
{
	return ProgramDataRoot();
}

void CkitsuneDrvInstallerView::AppendLog(const std::wstring& text)
{
	CString existing;
	m_log.GetWindowTextW(existing);
	SYSTEMTIME now;
	GetLocalTime(&now);
	const std::wstring formatted = FormatTimestampedLogForTest(
		text, now.wHour, now.wMinute, now.wSecond);
	if (formatted.empty()) return;
	CString appended(formatted.c_str());
	if (!existing.IsEmpty()) existing += L"\r\n";
	existing += appended;
	m_log.SetWindowTextW(existing);
	m_log.SetSel(-1, -1);
	m_log.SendMessageW(EM_SCROLLCARET, 0, 0);
	m_log.LineScroll(m_log.GetLineCount());
	m_log.SendMessageW(WM_VSCROLL, SB_BOTTOM, 0);
	const std::wstring logDirectory = SystemDriversRoot();
	CreateDirectoryW(logDirectory.c_str(), nullptr);
	const std::wstring logPath = JoinPath(logDirectory, L"Install.log");
	const HANDLE logFile = CreateFileW(logPath.c_str(), FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (logFile != INVALID_HANDLE_VALUE)
	{
		CString fileLine = appended + L"\r\n";
		const int byteCount = WideCharToMultiByte(CP_UTF8, 0, fileLine.GetString(), fileLine.GetLength(),
			nullptr, 0, nullptr, nullptr);
		if (byteCount > 0)
		{
			std::string utf8(static_cast<size_t>(byteCount), '\0');
			WideCharToMultiByte(CP_UTF8, 0, fileLine.GetString(), fileLine.GetLength(), &utf8[0],
				byteCount, nullptr, nullptr);
			DWORD written = 0;
			WriteFile(logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
		}
		CloseHandle(logFile);
	}
	PumpMessages();
}

std::wstring CkitsuneDrvInstallerView::FormatTimestampedLogForTest(
	const std::wstring& text, unsigned hour, unsigned minute, unsigned second)
{
	CString timestamp;
	timestamp.Format(L"[%02u:%02u:%02u] ", hour, minute, second);
	CString normalized(text.c_str());
	normalized.Replace(L"\r\n", L"\n");
	normalized.Replace(L"\r", L"\n");
	CString appended;
	int start = 0;
	while (start <= normalized.GetLength())
	{
		const int end = normalized.Find(L'\n', start);
		const CString part = end < 0 ? normalized.Mid(start) : normalized.Mid(start, end - start);
		if (!part.IsEmpty())
		{
			if (!appended.IsEmpty()) appended += L"\r\n";
			appended += timestamp;
			appended += part;
		}
		if (end < 0) break;
		start = end + 1;
	}
	return std::wstring(appended.GetString());
}

void CkitsuneDrvInstallerView::SetBusy(bool busy)
{
	m_busy = busy;
	UpdateInteractionState();
	UpdateWindow();
}

void CkitsuneDrvInstallerView::UpdateInteractionState()
{
	const bool enabled = !m_busy && (!m_aiMode || m_aiInteractionEnabled);
	// Language and driver selection remain available during the /ai countdown.
	// Both are disabled only while scanning/installing is actually busy.
	m_language.EnableWindow(!m_busy);
	// Keep driver check boxes available during the /ai countdown so the user
	// can adjust the installation set. Actual installation still locks the list.
	m_devices.EnableWindow(!m_busy);
	m_scan.EnableWindow(enabled);
	m_selectRecommended.EnableWindow(enabled && !m_matches.empty());
	const bool installAvailable = !m_busy && HasCheckedDrivers() &&
		(!m_aiMode || m_aiInteractionEnabled || m_aiCountdownActive);
	m_install.EnableWindow(installAvailable);
}

bool CkitsuneDrvInstallerView::HasCheckedDrivers()
{
	for (int row = 0; row < m_devices.GetItemCount(); ++row)
		if (m_devices.GetCheck(row)) return true;
	return false;
}

void CkitsuneDrvInstallerView::UpdateInstallButtonState()
{
	UpdateInteractionState();
}

void CkitsuneDrvInstallerView::OnDeviceItemChanged(NMHDR* notifyHeader, LRESULT* result)
{
	if (m_busy)
	{
		*result = 0;
		return;
	}
	const NMLISTVIEW* item = reinterpret_cast<NMLISTVIEW*>(notifyHeader);
	if ((item->uChanged & LVIF_STATE) != 0 &&
		((item->uOldState ^ item->uNewState) & LVIS_STATEIMAGEMASK) != 0)
	{
		CancelAiCountdownForUserInput();
		UpdateInstallButtonState();
	}
	*result = 0;
}

void CkitsuneDrvInstallerView::OnScan()
{
	if (m_busy) return;
	const std::wstring root = CurrentDataRoot();
	if (!FileExists(JoinPath(root, L"config.json")))
	{
		AfxMessageBox(Tr(TextId::DataConfigMissing), MB_ICONWARNING);
		return;
	}
	std::wstring error;
	if (!SystemCompatibility::ValidateDriverMedia(root, error))
	{
		AppendLog(Tr(TextId::UnsupportedSystemLog));
		ShowSystemCompatibilityError(GetSafeHwnd(), error);
		return;
	}
	SetBusy(true);
	m_progress.SetRange32(0, 100);
	m_progress.SetPos(12);
	AppendLog(Tr(TextId::LoadingIndex));
	if (!m_catalog.Load(root, error))
	{
		AppendLog(std::wstring(Tr(TextId::InstallFailureLog)) + error);
		SetBusy(false);
		AfxMessageBox(error.c_str(), MB_ICONERROR);
		return;
	}
	m_progress.SetPos(58);
	int scanned = 0;
	if (!DeviceScanner::Scan(root, m_catalog, m_matches, scanned, error))
	{
		AppendLog(std::wstring(Tr(TextId::InstallFailureLog)) + error);
		SetBusy(false);
		AfxMessageBox(error.c_str(), MB_ICONERROR);
		return;
	}
	m_progress.SetPos(100);
	m_lastScannedDeviceCount = scanned;
	RefreshDeviceList();
	if (m_aiMode && m_matches.empty())
	{
		CString noMatch;
		noMatch.Format(Tr(TextId::AiNoDriverMatchFormat), scanned);
		AppendLog(noMatch.GetString());
		SetBusy(false);
		ScheduleAiExit(5000);
		return;
	}
	CString summary;
	summary.Format(Tr(TextId::ScanSummaryFormat),
		static_cast<unsigned>(m_matches.size()));
	AppendLog(summary.GetString());
	SetBusy(false);
	if (m_aiMode)
	{
		m_aiCountdownActive = true;
		AppendLog(Tr(TextId::AiInstallCountdown));
		SetTimer(AI_INSTALL_TIMER, 5000, nullptr);
		UpdateInteractionState();
	}
}

void CkitsuneDrvInstallerView::CancelAiCountdownForUserInput()
{
	if (!m_aiMode || !m_aiCountdownActive) return;
	KillTimer(AI_INSTALL_TIMER);
	m_aiCountdownActive = false;
	m_aiInteractionEnabled = true;
	AppendLog(Tr(TextId::AiInstallCancelled));
	UpdateInteractionState();
}

void CkitsuneDrvInstallerView::ScheduleAiExit(UINT delayMilliseconds)
{
	KillTimer(AI_EXIT_TIMER);
	SetTimer(AI_EXIT_TIMER, delayMilliseconds, nullptr);
}

void CkitsuneDrvInstallerView::OnTimer(UINT_PTR timerId)
{
	if (timerId == AI_INSTALL_TIMER)
	{
		KillTimer(AI_INSTALL_TIMER);
		m_aiCountdownActive = false;
		if (HasCheckedDrivers()) OnInstall();
		else if (AfxGetMainWnd()) AfxGetMainWnd()->PostMessageW(WM_CLOSE);
		return;
	}
	if (timerId == AI_EXIT_TIMER)
	{
		KillTimer(AI_EXIT_TIMER);
		if (AfxGetMainWnd()) AfxGetMainWnd()->PostMessageW(WM_CLOSE);
		return;
	}
	CView::OnTimer(timerId);
}

void CkitsuneDrvInstallerView::RefreshDeviceList()
{
	m_devices.DeleteAllItems();
	for (size_t i = 0; i < m_matches.size(); ++i)
	{
		const DeviceMatch& match = m_matches[i];
		const int row = m_devices.InsertItem(static_cast<int>(i), match.displayName.c_str());
		m_devices.SetItemText(row, 1, match.driver.provider.c_str());
		m_devices.SetItemText(row, 2, match.driver.driverVersion.c_str());
		m_devices.SetItemText(row, 3, match.driver.driverDate.c_str());
		const TextId status = match.needsDriver ? TextId::MissingDriver :
			(match.updateAvailable ? TextId::InstalledUpdate : TextId::InstalledSuccess);
		m_devices.SetItemText(row, 4, Tr(status));
		m_devices.SetCheck(row, (match.needsDriver || match.updateAvailable) ? TRUE : FALSE);
	}
	UpdateInstallButtonState();
}

void CkitsuneDrvInstallerView::OnSelectRecommended()
{
	for (int row = 0; row < m_devices.GetItemCount(); ++row)
	{
		const DeviceMatch& match = m_matches[static_cast<size_t>(row)];
		m_devices.SetCheck(row, (match.needsDriver || match.updateAvailable) ? TRUE : FALSE);
	}
	UpdateInstallButtonState();
}

void CkitsuneDrvInstallerView::OnInstall()
{
	if (m_busy) return;
	if (m_aiCountdownActive)
	{
		KillTimer(AI_INSTALL_TIMER);
		m_aiCountdownActive = false;
	}
	std::vector<int> selected;
	for (int row = 0; row < m_devices.GetItemCount(); ++row)
		if (m_devices.GetCheck(row)) selected.push_back(row);
	if (selected.empty())
	{
		AfxMessageBox(Tr(TextId::SelectDriverFirst), MB_ICONINFORMATION);
		return;
	}
	const std::wstring root = CurrentDataRoot();
	std::wstring compatibilityError;
	if (!SystemCompatibility::ValidateDriverMedia(root, compatibilityError))
	{
		AppendLog(Tr(TextId::UnsupportedSystemLog));
		ShowSystemCompatibilityError(GetSafeHwnd(), compatibilityError);
		return;
	}
	const std::wstring sevenZip = DriverInstaller::Find7Zip(root);
	if (sevenZip.empty())
	{
		AfxMessageBox(Tr(TextId::SevenZipMediaMissing), MB_ICONERROR);
		return;
	}
	SetBusy(true);
	CMainFrame* mainFrame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
	if (mainFrame) mainFrame->SetInstallationActive(true);
	const HWND ownerWindow = mainFrame ? mainFrame->GetSafeHwnd() : GetSafeHwnd();
	m_progress.SetRange32(0, static_cast<int>(selected.size()));
	m_progress.SetPos(0);
	int success = 0;
	int warnings = 0;
	for (size_t position = 0; position < selected.size(); ++position)
	{
		const int row = selected[position];
		DeviceMatch& match = m_matches[static_cast<size_t>(row)];
		AppendLog(std::wstring(Tr(TextId::StartInstall)) + match.displayName);
		bool reboot = false;
		int afterInstallWarnings = 0;
		std::wstring error;
		if (DriverInstaller::Install(ownerWindow, root, match, reboot, afterInstallWarnings, error,
			[this](const std::wstring& line) { AppendLog(line); }))
		{
			++success;
			warnings += afterInstallWarnings;
			m_devices.SetItemText(row, 4, reboot ? Tr(TextId::InstalledReboot) : Tr(TextId::InstalledSuccess));
			m_devices.SetCheck(row, FALSE);
			AppendLog(std::wstring(Tr(TextId::InstallSuccessLog)) + match.displayName);
		}
		else
		{
			m_devices.SetItemText(row, 4, Tr(TextId::InstallFailedStatus));
			AppendLog(std::wstring(Tr(TextId::InstallFailureLog)) + error);
		}
		m_progress.SetPos(static_cast<int>(position + 1));
		PumpMessages();
	}
	UpdateInstallButtonState();
	const int failed = static_cast<int>(selected.size()) - success;
	CString successLine;
	successLine.Format(Tr(TextId::InstallSuccessCountFormat), success);
	CString summary = Tr(TextId::InstallCompleteHeading);
	summary += L"\r\n";
	summary += successLine;
	if (failed > 0)
	{
		CString failureLine;
		failureLine.Format(Tr(TextId::InstallFailureCountFormat), failed);
		summary += L"\r\n";
		summary += failureLine;
	}
	if (warnings > 0)
	{
		CString warningLine;
		warningLine.Format(Tr(TextId::InstallWarningCountFormat), warnings);
		summary += L"\r\n";
		summary += warningLine;
	}
	if (!m_aiMode)
	{
		summary += L"\r\n";
		summary += Tr(TextId::RestartToApply);
	}
	AppendLog(summary.GetString());
	SetBusy(false);
	if (mainFrame) mainFrame->SetInstallationActive(false);
	if (m_aiMode)
	{
		ScheduleAiExit(3000);
	}
	else
	{
		::MessageBoxW(GetSafeHwnd(), summary, Tr(TextId::InstallCompleteTitle),
			MB_OK | (failed == 0 && warnings == 0 ? MB_ICONINFORMATION : MB_ICONWARNING));
	}
}

void CkitsuneDrvInstallerView::ApplyLanguage()
{
	UpdateTitle();
	if (CMainFrame* mainFrame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd()))
		mainFrame->RefreshVersionStatus();
	m_languageLabel.SetWindowTextW(Tr(TextId::Language));
	m_scan.SetWindowTextW(Tr(TextId::ScanHardware));
	m_selectRecommended.SetWindowTextW(Tr(TextId::SelectMissing));
	m_install.SetWindowTextW(Tr(TextId::InstallSelected));
	m_logLabel.SetWindowTextW(Tr(TextId::OperationLog));
	const TextId columns[] = { TextId::ColumnDevice, TextId::ColumnProvider,
		TextId::ColumnVersion, TextId::ColumnDate, TextId::ColumnStatus };
	for (int index = 0; index < _countof(columns); ++index)
	{
		LVCOLUMNW column = {};
		column.mask = LVCF_TEXT;
		column.pszText = const_cast<LPWSTR>(Tr(columns[index]));
		m_devices.SetColumn(index, &column);
	}
	if (!m_matches.empty()) RefreshDeviceList();
	Invalidate();
}

void CkitsuneDrvInstallerView::UpdateTitle()
{
	const std::wstring applicationTitle = L"kiri Driver Installer";
	m_title.SetWindowTextW(applicationTitle.c_str());
	std::wstring targetSystem;
	std::wstring targetArchitecture;
	std::wstring windowTitle = applicationTitle;
	if (SystemCompatibility::GetDriverMediaTarget(
		CurrentDataRoot(), targetSystem, targetArchitecture))
	{
		windowTitle += L" - [";
		windowTitle += targetSystem;
		windowTitle += L" ";
		windowTitle += targetArchitecture;
		windowTitle += L"]";
	}
	if (AfxGetMainWnd()) AfxGetMainWnd()->SetWindowTextW(windowTitle.c_str());
}

void CkitsuneDrvInstallerView::OnLanguageChanged()
{
	CancelAiCountdownForUserInput();
	if (m_busy) return;
	const int selection = m_language.GetCurSel();
	if (selection < 0 || selection > 2) return;
	Localization::SetLanguage(static_cast<UiLanguage>(selection));
	ApplyLanguage();
}

#ifdef _DEBUG
void CkitsuneDrvInstallerView::AssertValid() const { CView::AssertValid(); }
void CkitsuneDrvInstallerView::Dump(CDumpContext& dc) const { CView::Dump(dc); }
CkitsuneDrvInstallerDoc* CkitsuneDrvInstallerView::GetDocument() const
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CkitsuneDrvInstallerDoc)));
	return static_cast<CkitsuneDrvInstallerDoc*>(m_pDocument);
}
#endif
