/* 
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdafx.h"
#include "Resource.h"
#include "AirAppearancePage.h"

#include "WinUtil.h"
#include "PropertiesDlg.h"

PropPage::TextItem AirAppearancePage::texts[] = {
	{ IDC_SETTINGS_BACKGROUND_IMAGE,	ResourceManager::BACKGROUND_IMAGE },
	{ IDC_IMAGEBROWSE,			ResourceManager::BROWSE },
	{ IDC_DUPE_SEARCH,			ResourceManager::SETTINGS_DUPE_SEARCH },
	{ IDC_DUPE_CHAT,			ResourceManager::SETTINGS_DUPE_CHAT	},
	{ IDC_DUPE_FILELISTS,		ResourceManager::SETTINGS_DUPE_FILELIST	},
	{ IDC_DUPES,				ResourceManager::SETTINGS_DUPES	},
	{ IDC_MAX_RESIZE_LINES_STR, ResourceManager::MAX_CHAT_RESIZE_LINES },
	{ IDC_SETCZDC_PM_LINES,		ResourceManager::MAX_PM_HISTORY_LINES },
	{ 0, ResourceManager::SETTINGS_AUTO_AWAY }
};

PropPage::Item AirAppearancePage::items[] = {
	{ IDC_BACKGROUND_IMAGE, SettingsManager::BACKGROUND_IMAGE, PropPage::T_STR },
	{ IDC_RESIZE_LINES, SettingsManager::MAX_RESIZE_LINES, PropPage::T_INT },//ApexDC
	{ IDC_PM_LINES, SettingsManager::SHOW_LAST_LINES_LOG, PropPage::T_INT },
	{ IDC_DUPE_SEARCH, SettingsManager::DUPE_SEARCH, PropPage::T_BOOL },
	{ IDC_DUPE_CHAT,		SettingsManager::DUPES_IN_CHAT, PropPage::T_BOOL	},
	{ IDC_DUPE_FILELISTS,	SettingsManager::DUPES_IN_FILELIST, PropPage::T_BOOL	},
	{ 0, 0, PropPage::T_END }
};


void AirAppearancePage::write() {
	PropPage::write((HWND)*this, items);
}


LRESULT AirAppearancePage::onInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	PropPage::translate((HWND)(*this), texts);

	PropPage::read((HWND)*this, items);
	
	setMinMax(IDC_RESIZE_LINES_SPIN, 1, 10); //ApexDC
	setMinMax(IDC_PM_LINESSPIN, 0, 999);

	// Do specialized reading here
	return TRUE;
}

void AirAppearancePage::BrowseForPic(int DLGITEM) {
	TCHAR buf[MAX_PATH];

	GetDlgItemText(DLGITEM, buf, MAX_PATH);
	tstring x = buf;

	if(WinUtil::browseFile(x, m_hWnd, false) == IDOK) {
		SetDlgItemText(DLGITEM, x.c_str());
	}
}

LRESULT AirAppearancePage::onImageBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	BrowseForPic(IDC_BACKGROUND_IMAGE);
	return 0;
}

/**
 * @file
 * $Id: AppearancePage.cpp 265 2007-12-15 09:57:02 Night $
 */