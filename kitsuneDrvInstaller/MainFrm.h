#pragma once

#include <string>

class CMainFrame : public CMDIFrameWndEx
{
	DECLARE_DYNAMIC(CMainFrame)
public:
	CMainFrame() noexcept;
	virtual ~CMainFrame();
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	virtual void OnUpdateFrameMenu(HMENU hMenuAlt);
	void CenterBeforeFirstShow();
	void SetInstallationActive(bool active);
	void RefreshVersionStatus();
	void RefreshWindowTitle();
	static bool RunWindowSizeTests(std::wstring& error);

#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:
	bool m_firstShowCentered = false;
	bool m_fixedSizeActive = false;
	bool m_installationActive = false;
	CSize m_fixedWindowSize;
	CMFCStatusBar m_statusBar;
	afx_msg int OnCreate(LPCREATESTRUCT createStruct);
	afx_msg void OnShowWindow(BOOL show, UINT status);
	afx_msg void OnSysCommand(UINT id, LPARAM parameter);
	afx_msg void OnClose();
	afx_msg BOOL OnQueryEndSession();
	afx_msg void OnAppExit();
	afx_msg LRESULT OnSetMessageString(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnSetIcon(WPARAM wParam, LPARAM lParam);
	afx_msg void OnTimer(UINT_PTR timerId);
	afx_msg void OnWindowPosChanging(WINDOWPOS* position);
	DECLARE_MESSAGE_MAP()
};
