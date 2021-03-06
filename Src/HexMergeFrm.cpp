/////////////////////////////////////////////////////////////////////////////
//    WinMerge:  an interactive diff/merge utility
//    Copyright (C) 1997-2000  Thingamahoochie Software
//    Author: Dean Grimm
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
/////////////////////////////////////////////////////////////////////////////
/** 
 * @file  HexMergeFrm.cpp
 *
 * @brief Implementation file for CHexMergeFrame
 *
 */

#include "stdafx.h"
#include "HexMergeFrm.h"
#include "Merge.h"
#include "HexMergeDoc.h"
#include "HexMergeView.h"
#include "OptionsDef.h"
#include "OptionsMgr.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// CHexMergeFrame

IMPLEMENT_DYNCREATE(CHexMergeFrame, CMDIChildWnd)

BEGIN_MESSAGE_MAP(CHexMergeFrame, CMDIChildWnd)
	//{{AFX_MSG_MAP(CHexMergeFrame)
	ON_WM_CREATE()
	ON_WM_CLOSE()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_MESSAGE_VOID(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DETAIL_BAR, OnUpdateControlBarMenu)
	ON_COMMAND_EX(ID_VIEW_DETAIL_BAR, OnBarCheck)
	ON_UPDATE_COMMAND_UI(ID_VIEW_LOCATION_BAR, OnUpdateControlBarMenu)
	ON_COMMAND_EX(ID_VIEW_LOCATION_BAR, OnBarCheck)
	ON_MESSAGE(MSG_STORE_PANESIZES, OnStorePaneSizes)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/**
 * @brief Statusbar pane indexes
 */
enum
{
	PANE_LEFT_INFO = 0,
	PANE_LEFT_RO,
	PANE_LEFT_EOL,
	PANE_RIGHT_INFO,
	PANE_RIGHT_RO,
	PANE_RIGHT_EOL,
};

/////////////////////////////////////////////////////////////////////////////
// CHexMergeFrame construction/destruction

CHexMergeFrame::CHexMergeFrame()
: m_hIdentical(NULL)
, m_hDifferent(NULL)
{
	std::fill_n(m_nLastSplitPos, 2, 0);
	m_pMergeDoc = 0;
}

CHexMergeFrame::~CHexMergeFrame()
{
}

/**
 * @brief Create a status bar to be associated with a heksedit control
 */
void CHexMergeFrame::CreateHexWndStatusBar(CStatusBar &wndStatusBar, CWnd *pwndPane)
{
	const int lpx = CClientDC(this).GetDeviceCaps(LOGPIXELSX);
	auto pointToPixel = [lpx](int point) { return MulDiv(point, lpx, 72); };
	wndStatusBar.Create(pwndPane, WS_CHILD|WS_VISIBLE);
	wndStatusBar.SetIndicators(0, 3);
	wndStatusBar.SetPaneInfo(0, 0, SBPS_STRETCH, 0);
	wndStatusBar.SetPaneInfo(1, 0, 0, pointToPixel(60));
	wndStatusBar.SetPaneInfo(2, 0, 0, pointToPixel(60));
	wndStatusBar.SetParent(this);
	wndStatusBar.SetWindowPos(&wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

/**
 * @brief Create the splitter, the filename bar, the status bar, and the two views
 */
BOOL CHexMergeFrame::OnCreateClient( LPCREATESTRUCT /*lpcs*/,
	CCreateContext* pContext)
{
	m_pMergeDoc = dynamic_cast<CHexMergeDoc *>(pContext->m_pCurrentDoc);

	// create a splitter with 1 row, 2 columns
	if (!m_wndSplitter.CreateStatic(this, 1, m_pMergeDoc->m_nBuffers, WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL) )
	{
		TRACE0("Failed to CreateStaticSplitter\n");
		return FALSE;
	}

	int nPane;
	for (nPane = 0; nPane < m_pMergeDoc->m_nBuffers; nPane++)
	{
		if (!m_wndSplitter.CreateView(0, nPane,
			RUNTIME_CLASS(CHexMergeView), CSize(-1, 200), pContext))
		{
			TRACE0("Failed to create first pane\n");
			return FALSE;
		}
	}

	m_wndSplitter.ResizablePanes(TRUE);
	m_wndSplitter.AutoResizePanes(GetOptionsMgr()->GetBool(OPT_RESIZE_PANES));

	// Merge frame has a header bar at top
	if (!m_wndFilePathBar.Create(this))
	{
		TRACE0("Failed to create dialog bar\n");
		return FALSE;      // fail to create
	}

	m_wndFilePathBar.SetPaneCount(m_pMergeDoc->m_nBuffers);

	// Set filename bars inactive so colors get initialized
	for (nPane = 0; nPane < m_pMergeDoc->m_nBuffers; nPane++)
		m_wndFilePathBar.SetActive(nPane, false);

	CHexMergeView *pView[3];
	for (nPane = 0; nPane < m_pMergeDoc->m_nBuffers; nPane++)
		pView[nPane] = static_cast<CHexMergeView *>(m_wndSplitter.GetPane(0, nPane));

	for (nPane = 0; nPane < m_pMergeDoc->m_nBuffers; nPane++)
		CreateHexWndStatusBar(m_wndStatusBar[nPane], pView[nPane]);
	CSize size = m_wndStatusBar[0].CalcFixedLayout(TRUE, TRUE);
	m_rectBorder.bottom = size.cy;

	m_hIdentical = AfxGetApp()->LoadIcon(IDI_EQUALBINARY);
	m_hDifferent = AfxGetApp()->LoadIcon(IDI_BINARYDIFF);

	// get the IHexEditorWindow interfaces
	IHexEditorWindow *pif[3];
	for (nPane = 0; nPane < m_pMergeDoc->m_nBuffers; nPane++)
	{
		pif[nPane] = reinterpret_cast<IHexEditorWindow *>(
			::GetWindowLongPtr(pView[nPane]->m_hWnd, GWLP_USERDATA));
	}

	// tell the heksedit controls about each other
	if (m_pMergeDoc->m_nBuffers == 2)
	{
		pif[0]->set_sibling(pif[1]);
		pif[1]->set_sibling(pif[0]);
		pif[1]->share_undorecords(pif[0]);
	}
	else if (m_pMergeDoc->m_nBuffers > 2)
	{
		pif[0]->set_sibling(pif[1]);
		pif[1]->set_sibling2(pif[0], pif[2]);
		pif[2]->set_sibling(pif[1]);
		pif[1]->share_undorecords(pif[0]);
		pif[2]->share_undorecords(pif[0]);
	}

	// tell merge doc about these views
	m_pMergeDoc = dynamic_cast<CHexMergeDoc *>(pContext->m_pCurrentDoc);
	m_pMergeDoc->SetMergeViews(pView);
	m_pMergeDoc->RefreshOptions();

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CHexMergeFrame message handlers

void CHexMergeFrame::ActivateFrame(int nCmdShow) 
{
	if (!GetMDIFrame()->MDIGetActive() && theApp.GetProfileInt(_T("Settings"), _T("ActiveFrameMax"), FALSE))
	{
		nCmdShow = SW_SHOWMAXIMIZED;
	}
	CMDIChildWnd::ActivateFrame(nCmdShow);
}

/**
 * @brief Save the window's position, free related resources, and destroy the window
 */
BOOL CHexMergeFrame::DestroyWindow() 
{
	SavePosition();
	// If we are active, save the restored/maximized state
	// If we are not, do nothing and let the active frame do the job.
 	if (GetParentFrame()->GetActiveFrame() == this)
	{
		WINDOWPLACEMENT wp;
		wp.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(&wp);
		theApp.WriteProfileInt(_T("Settings"), _T("ActiveFrameMax"), (wp.showCmd == SW_MAXIMIZE));
	}

	return CMDIChildWnd::DestroyWindow();
}

/**
 * @brief Save coordinates of the frame, splitters, and bars
 *
 * @note Do not save the maximized/restored state here. We are interested
 * in the state of the active frame, and maybe this frame is not active
 */
void CHexMergeFrame::SavePosition()
{
	if (CWnd *pLeft = m_wndSplitter.GetPane(0,0))
	{
		CRect rc;
		pLeft->GetWindowRect(&rc);
		theApp.WriteProfileInt(_T("Settings"), _T("WLeft"), rc.Width());
	}
}

void CHexMergeFrame::OnMDIActivate(BOOL bActivate, CWnd* pActivateWnd, CWnd* pDeactivateWnd)
{
	CMDIChildWnd::OnMDIActivate(bActivate, pActivateWnd, pDeactivateWnd);

	CHexMergeDoc *pDoc = GetMergeDoc();
	if (bActivate && pDoc)
		this->GetParentFrame()->PostMessage(WM_USER+1);
	return;
}

void CHexMergeFrame::OnSize(UINT nType, int cx, int cy) 
{
	CMDIChildWnd::OnSize(nType, cx, cy);
	UpdateHeaderSizes();
}

void CHexMergeFrame::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CMDIChildWnd::OnGetMinMaxInfo(lpMMI);
	// [Fix for MFC 8.0 MDI Maximizing Child Window bug on Vista]
	// https://groups.google.com/forum/#!topic/microsoft.public.vc.mfc/iajCdW5DzTM
#if _MFC_VER >= 0x0800
	lpMMI->ptMaxTrackSize.x = max(lpMMI->ptMaxTrackSize.x, lpMMI->ptMaxSize.x);
	lpMMI->ptMaxTrackSize.y = max(lpMMI->ptMaxTrackSize.y, lpMMI->ptMaxSize.y);
#endif
}

/// update splitting position for panels 1/2 and headerbar and statusbar 
void CHexMergeFrame::UpdateHeaderSizes()
{
	if (IsWindowVisible())
	{
		int w[3],wmin;
		int nPaneCount = m_wndSplitter.GetColumnCount();
		for (int pane = 0; pane < nPaneCount; pane++)
		{
			m_wndSplitter.GetColumnInfo(pane, w[pane], wmin);
			if (w[pane]<1) w[pane]=1; // Perry 2003-01-22 (I don't know why this happens)
		}
		
		if (!std::equal(m_nLastSplitPos, m_nLastSplitPos + nPaneCount - 1, w))
		{
			std::copy_n(w, nPaneCount - 1, m_nLastSplitPos);

			// resize controls in header dialog bar
			m_wndFilePathBar.Resize(w);
			RECT rcFrame, rc;
			GetClientRect(&rcFrame);
			rc = rcFrame;
			rc.top = rc.bottom - m_rectBorder.bottom;
			rc.right = 0;
			for (int pane = 0; pane < nPaneCount; pane++)
			{
				if (pane < nPaneCount - 1)
					rc.right += w[pane] + 6;
				else
					rc.right = rcFrame.right;
				m_wndStatusBar[pane].MoveWindow(&rc);
				rc.left = rc.right;
			}
		}
	}
}

IHeaderBar *CHexMergeFrame::GetHeaderInterface()
{
	return &m_wndFilePathBar;
}

/**
 * @brief Reflect comparison result in window's icon.
 * @param nResult [in] Last comparison result which the application returns.
 */
void CHexMergeFrame::SetLastCompareResult(int nResult)
{
	HICON hCurrent = GetIcon(FALSE);
	HICON hReplace = (nResult == 0) ? m_hIdentical : m_hDifferent;

	if (hCurrent != hReplace)
	{
		SetIcon(hReplace, TRUE);

		BOOL bMaximized;
		GetMDIFrame()->MDIGetActive(&bMaximized);

		// When MDI maximized the window icon is drawn on the menu bar, so we
		// need to notify it that our icon has changed.
		if (bMaximized)
		{
			GetMDIFrame()->DrawMenuBar();
		}
		GetMDIFrame()->OnUpdateFrameTitle(FALSE);
	}

	theApp.SetLastCompareResult(nResult);
}

void CHexMergeFrame::UpdateAutoPaneResize()
{
	m_wndSplitter.AutoResizePanes(GetOptionsMgr()->GetBool(OPT_RESIZE_PANES));
}

void CHexMergeFrame::UpdateSplitter()
{
	m_wndSplitter.RecalcLayout();
}

/**
 * @brief Synchronize control and status bar placements with splitter position,
 * update mod indicators, synchronize scrollbars
 */
void CHexMergeFrame::OnIdleUpdateCmdUI()
{
	if (IsWindowVisible())
	{
		UpdateHeaderSizes();

		int pane;
		int nColumns = m_wndSplitter.GetColumnCount();
		CHexMergeView *pView[3] = {0};
		for (pane = 0; pane < nColumns; ++pane)
			pView[pane] = static_cast<CHexMergeView *>(m_wndSplitter.GetPane(0, pane));

		// Update mod indicators
		TCHAR ind[2];

		for (pane = 0; pane < nColumns; ++pane)
		{
			if (m_wndFilePathBar.GetDlgItemText(IDC_STATIC_TITLE_PANE0 + pane, ind, 2))
				if (pView[pane]->GetModified() ? ind[0] != _T('*') : ind[0] == _T('*'))
					m_pMergeDoc->UpdateHeaderPath(pane);
		}

		// Synchronize scrollbars
		SCROLLINFO si, siView[3];
		// Synchronize horizontal scrollbars
		pView[0]->GetScrollInfo(SB_HORZ, &si, SIF_ALL | SIF_DISABLENOSCROLL);
		for (pane = 1; pane < nColumns; ++pane)
		{
			SCROLLINFO siCur;
			pView[pane]->GetScrollInfo(SB_HORZ, &siCur, SIF_ALL | SIF_DISABLENOSCROLL);
			siView[pane] = siCur;
			if (si.nMin > siCur.nMin)
				si.nMin = siCur.nMin;
			if (si.nPage < siCur.nPage)
				si.nPage = siCur.nPage;
			if (si.nMax < siCur.nMax)
				si.nMax = siCur.nMax;
			if (GetFocus() == pView[pane])
			{
				si.nPos = siCur.nPos;
				si.nTrackPos = siCur.nTrackPos;
			}
		}
		for (pane = 0; pane < nColumns; ++pane)
		{
			if (memcmp(&si, &siView[pane], sizeof si))
			{
				pView[pane]->SetScrollInfo(SB_HORZ, &si);
				pView[pane]->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, si.nTrackPos));
			}
		}
		for (pane = 0; pane < nColumns; ++pane)
			m_wndSplitter.GetScrollBarCtrl(pView[pane], SB_HORZ)->SetScrollInfo(&si);

		// Synchronize vertical scrollbars
		pView[0]->GetScrollInfo(SB_VERT, &si, SIF_ALL | SIF_DISABLENOSCROLL);
		for (pane = 1; pane < nColumns; ++pane)
		{
			SCROLLINFO siCur;
			pView[pane]->GetScrollInfo(SB_VERT, &siCur, SIF_ALL | SIF_DISABLENOSCROLL);
			siView[pane] = siCur;
			if (si.nMin > siCur.nMin)
				si.nMin = siCur.nMin;
			if (si.nMax < siCur.nMax)
				si.nMax = siCur.nMax;
			if (GetFocus() == pView[pane])
			{
				si.nPos = siCur.nPos;
				si.nTrackPos = siCur.nTrackPos;
			}
		}
		for (pane = 0; pane < nColumns; ++pane)
		{
			if (memcmp(&si, &siView[pane], sizeof si))
			{
				pView[pane]->SetScrollInfo(SB_VERT, &si);
				pView[pane]->SendMessage(WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, si.nTrackPos));
			}
		}
		m_wndSplitter.GetScrollBarCtrl(pView[nColumns - 1], SB_VERT)->SetScrollInfo(&si);
	}
	CMDIChildWnd::OnIdleUpdateCmdUI();
}

/// Document commanding us to close
void CHexMergeFrame::CloseNow()
{
	SavePosition(); // Save settings before closing!
	MDIActivate();
	MDIDestroy();
}

/**
 * @brief Update any resources necessary after a GUI language change
 */
void CHexMergeFrame::UpdateResources()
{
}

/**
 * @brief Save pane sizes and positions when one of panes requests it.
 */
LRESULT CHexMergeFrame::OnStorePaneSizes(WPARAM wParam, LPARAM lParam)
{
	SavePosition();
	return 0;
}
