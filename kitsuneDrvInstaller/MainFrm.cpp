#include "pch.h"
#include "framework.h"
#include "kitsuneDrvInstaller.h"
#include "MainFrm.h"
#include "Localization.h"
#include <vector>
#include <winver.h>
#pragma comment(lib, "Version.lib")
#include "DriverEngine.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	CString ReadExecutableFileVersion()
	{
		wchar_t modulePath[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, modulePath, _countof(modulePath)) == 0)
			return CString();

		DWORD handle = 0;
		const DWORD infoSize = GetFileVersionInfoSizeW(modulePath, &handle);
		if (infoSize == 0)
			return CString();

		std::vector<BYTE> versionData(infoSize);
		if (!GetFileVersionInfoW(modulePath, 0, infoSize, versionData.data()))
			return CString();

		VS_FIXEDFILEINFO* fixedInfo = nullptr;
		UINT fixedInfoSize = 0;
		if (!VerQueryValueW(versionData.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &fixedInfoSize) ||
			fixedInfo == nullptr || fixedInfoSize < sizeof(VS_FIXEDFILEINFO) ||
			fixedInfo->dwSignature != 0xFEEF04BD)
			return CString();

		CString version;
		version.Format(L"%u.%u.%u.%u",
			HIWORD(fixedInfo->dwFileVersionMS), LOWORD(fixedInfo->dwFileVersionMS),
			HIWORD(fixedInfo->dwFileVersionLS), LOWORD(fixedInfo->dwFileVersionLS));
		return version;
	}
}

	constexpr int PreferredWindowWidth = 860;
	constexpr int PreferredWindowHeight = 560;
	constexpr int ScreenEdgeMargin = 8;

	CSize CalculateFixedWindowSize(int workWidth, int workHeight)
	{
		return CSize(max(1, min(PreferredWindowWidth, workWidth - ScreenEdgeMargin * 2)),
			max(1, min(PreferredWindowHeight, workHeight - ScreenEdgeMargin * 2)));
	}

IMPLEMENT_DYNAMIC(CMainFrame, CMDIFrameWndEx)

BEGIN_MESSAGE_MAP(CMainFrame, CMDIFrameWndEx)
	ON_WM_CREATE()
	ON_WM_SHOWWINDOW()
	ON_WM_SYSCOMMAND()
	ON_WM_CLOSE()
	ON_WM_QUERYENDSESSION()
	ON_COMMAND(ID_APP_EXIT, &CMainFrame::OnAppExit)
	ON_MESSAGE(WM_SETMESSAGESTRING, &CMainFrame::OnSetMessageString)
	ON_MESSAGE(WM_SETICON, &CMainFrame::OnSetIcon)
	ON_WM_TIMER()
	ON_WM_WINDOWPOSCHANGING()
END_MESSAGE_MAP()

CMainFrame::CMainFrame() noexcept {}
CMainFrame::~CMainFrame() = default;

LRESULT CMainFrame::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	const bool mouseInput = message >= WM_LBUTTONDOWN && message <= WM_XBUTTONDBLCLK;
	const bool nonClientMouseInput = message >= WM_NCLBUTTONDOWN && message <= WM_NCXBUTTONDBLCLK;
	const bool keyboardInput = message >= WM_KEYFIRST && message <= WM_KEYLAST;
	if (mouseInput || nonClientMouseInput || keyboardInput)
		theApp.NotifyAiUserActivity();
	return CMDIFrameWndEx::WindowProc(message, wParam, lParam);
}

int CMainFrame::OnCreate(LPCREATESTRUCT createStruct)
{
	if (CMDIFrameWndEx::OnCreate(createStruct) == -1) return -1;
	static const UINT statusIndicators[] = { ID_SEPARATOR };
	if (!m_statusBar.Create(this) ||
		!m_statusBar.SetIndicators(statusIndicators, _countof(statusIndicators)))
		return -1;
	m_statusBar.SetPaneStyle(0, SBPS_STRETCH | SBPS_NOBORDERS);
	RefreshVersionStatus();
	SetTimer(1, 250, nullptr);
	// Keep the resource menu owned by MFC, but detach it while the frame is
	// still hidden. This avoids both a first-frame flash and an invalid menu
	// handle during later MDI updates/shutdown.
	SetMenu(nullptr);
	CMDITabInfo tabs;
	EnableMDITabbedGroups(FALSE, tabs);
	EnableMDITabs(FALSE);
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));
	SetWindowTextW(Tr(TextId::AppTitle));
	// Remove both per-window and class fallback icons. Together with
	// WS_EX_DLGMODALFRAME this removes the caption icon slot itself instead of
	// merely painting a transparent icon inside the reserved space.
	DefWindowProcW(WM_SETICON, ICON_BIG, 0);
	DefWindowProcW(WM_SETICON, ICON_SMALL, 0);
	SetClassLongPtrW(GetSafeHwnd(), GCLP_HICON, 0);
	SetClassLongPtrW(GetSafeHwnd(), GCLP_HICONSM, 0);
	SetWindowPos(nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	if (CMenu* systemMenu = GetSystemMenu(FALSE))
	{
		systemMenu->DeleteMenu(SC_SIZE, MF_BYCOMMAND);
		systemMenu->DeleteMenu(SC_MINIMIZE, MF_BYCOMMAND);
		systemMenu->DeleteMenu(SC_MAXIMIZE, MF_BYCOMMAND);
	}
	return 0;
}

void CMainFrame::RefreshVersionStatus()
{
	if (!m_statusBar.GetSafeHwnd()) return;
	CString text = L"kiri Driver Installer - ";
	switch (Localization::GetLanguage())
	{
	case UiLanguage::ChineseSimplified: text += L"版本："; break;
	case UiLanguage::ChineseTraditional: text += L"版本："; break;
	default: text += L"Version: "; break;
	}
	const CString executableVersion = ReadExecutableFileVersion();
	text += executableVersion.IsEmpty() ? L"Unknown" : executableVersion;
	CString current;
	m_statusBar.GetPaneText(0, current);
	if (current != text) m_statusBar.SetPaneText(0, text);
}

void CMainFrame::RefreshWindowTitle()
{
	wchar_t module[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, module, _countof(module));
	std::wstring executablePath(module);
	const size_t slash = executablePath.find_last_of(L"\\/");
	const std::wstring dataRoot =
		(executablePath.substr(0, slash) + L"\\Data");
	std::wstring targetSystem;
	std::wstring targetArchitecture;
	std::wstring title = L"kiri Driver Installer";
	if (SystemCompatibility::GetDriverMediaTarget(
		dataRoot, targetSystem, targetArchitecture))
	{
		title += L" - [" + targetSystem + L" " + targetArchitecture + L"]";
	}
	CString current;
	GetWindowTextW(current);
	if (current != title.c_str()) SetWindowTextW(title.c_str());
}

LRESULT CMainFrame::OnSetMessageString(WPARAM wParam, LPARAM lParam)
{
	const LRESULT result = CMDIFrameWndEx::OnSetMessageString(wParam, lParam);
	RefreshVersionStatus();
	return result;
}

LRESULT CMainFrame::OnSetIcon(WPARAM wParam, LPARAM)
{
	// LoadFrame and shell integration can reapply IDR_MAINFRAME after OnCreate.
	// Keep the corresponding window icon empty and reject the supplied handle.
	return DefWindowProcW(WM_SETICON, wParam, 0);
}

void CMainFrame::OnTimer(UINT_PTR timerId)
{
	if (timerId == 1)
	{
		RefreshVersionStatus();
		RefreshWindowTitle();
	}
	CMDIFrameWndEx::OnTimer(timerId);
}

void CMainFrame::OnUpdateFrameMenu(HMENU /*hMenuAlt*/)
{
	// MDI 会在子窗口激活时重新合并文档菜单；在框架更新点持续清空它。
	if (m_hWndMDIClient != nullptr)
		::SendMessageW(m_hWndMDIClient, WM_MDISETMENU, 0, 0);
	SetMenu(nullptr);
	DrawMenuBar();
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	if (!CMDIFrameWndEx::PreCreateWindow(cs)) return FALSE;
	// Use the dialog-style non-client frame to suppress the title-bar icon
	// while retaining the close button, and keep the installer above ordinary
	// application windows for the lifetime of the process.
	cs.dwExStyle |= WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
	// The installer supplies its complete title. Do not let the MDI framework
	// append the active child/document title a second time.
	cs.style &= ~(FWS_ADDTOTITLE | WS_THICKFRAME | WS_MINIMIZEBOX |
		WS_MAXIMIZEBOX | WS_MINIMIZE | WS_MAXIMIZE);
	// Set the final coordinates before CreateWindowEx. The frame is therefore
	// born at the centered position and never paints at a saved/old position.
	POINT cursor = {};
	GetCursorPos(&cursor);
	const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO info = { sizeof(info) };
	if (GetMonitorInfoW(monitor, &info))
	{
		const int workWidth = info.rcWork.right - info.rcWork.left;
		const int workHeight = info.rcWork.bottom - info.rcWork.top;
		// Use 900x600 on ordinary displays. Only shrink when the monitor work area
		// is smaller, preserving a margin and the complete compact layout.
		const CSize windowSize = CalculateFixedWindowSize(workWidth, workHeight);
		cs.cx = windowSize.cx;
		cs.cy = windowSize.cy;
		cs.x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - cs.cx) / 2;
		cs.y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - cs.cy) / 2;
	}
	else
	{
		cs.cx = PreferredWindowWidth;
		cs.cy = PreferredWindowHeight;
	}
	return TRUE;
}

bool CMainFrame::RunWindowSizeTests(std::wstring& error)
{
	struct SizeCase { int workWidth; int workHeight; int width; int height; };
	const SizeCase cases[] =
	{
		{ 1024, 768, 860, 560 },
		{ 1024, 728, 860, 560 },
		{ 800, 560, 784, 544 },
		{ 640, 360, 624, 344 }
	};
	for (const SizeCase& test : cases)
	{
		const CSize size = CalculateFixedWindowSize(test.workWidth, test.workHeight);
		if (size.cx == test.width && size.cy == test.height) continue;
		error = L"fixed window size regression";
		return false;
	}
	error.clear();
	return true;
}

void CMainFrame::CenterBeforeFirstShow()
{
	POINT cursor = {};
	GetCursorPos(&cursor);
	const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO info = { sizeof(info) };
	if (!GetMonitorInfoW(monitor, &info)) return;

	CRect windowRect;
	GetWindowRect(&windowRect);
	const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - windowRect.Width()) / 2;
	const int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - windowRect.Height()) / 2;
	SetWindowPos(nullptr, x, y, 0, 0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
}

void CMainFrame::OnShowWindow(BOOL show, UINT status)
{
	// WM_SHOWWINDOW arrives before the first visible frame is composed. Apply
	// the final position here so even a late MFC placement restore stays hidden.
	if (show && !m_firstShowCentered)
	{
		CenterBeforeFirstShow();
		CRect rect;
		GetWindowRect(&rect);
		m_fixedWindowSize = rect.Size();
		m_fixedSizeActive = true;
		m_firstShowCentered = true;
	}
	CMDIFrameWndEx::OnShowWindow(show, status);
}

void CMainFrame::OnSysCommand(UINT id, LPARAM parameter)
{
	const UINT command = id & 0xFFF0;
	if (command == SC_SIZE || command == SC_MINIMIZE || command == SC_MAXIMIZE ||
		(m_installationActive && command == SC_CLOSE)) return;
	CMDIFrameWndEx::OnSysCommand(id, parameter);
}

void CMainFrame::SetInstallationActive(bool active)
{
	m_installationActive = active;
	if (CMenu* systemMenu = GetSystemMenu(FALSE))
		systemMenu->EnableMenuItem(SC_CLOSE, MF_BYCOMMAND |
			(active ? MF_GRAYED : MF_ENABLED));
	DrawMenuBar();
}

void CMainFrame::OnClose()
{
	if (m_installationActive) return;
	CMDIFrameWndEx::OnClose();
}

BOOL CMainFrame::OnQueryEndSession()
{
	if (m_installationActive) return FALSE;
	return CMDIFrameWndEx::OnQueryEndSession();
}

void CMainFrame::OnAppExit()
{
	if (m_installationActive) return;
	SendMessageW(WM_CLOSE);
}

void CMainFrame::OnWindowPosChanging(WINDOWPOS* position)
{
	if (m_fixedSizeActive && (position->flags & SWP_NOSIZE) == 0)
	{
		position->cx = m_fixedWindowSize.cx;
		position->cy = m_fixedWindowSize.cy;
	}
	CMDIFrameWndEx::OnWindowPosChanging(position);
}

#ifdef _DEBUG
void CMainFrame::AssertValid() const { CMDIFrameWndEx::AssertValid(); }
void CMainFrame::Dump(CDumpContext& dc) const { CMDIFrameWndEx::Dump(dc); }
#endif
