#include "stdafx.h"
#include "main.h"

CNtPlayer::CNtPlayer(HINSTANCE hInst, LPWSTR cmdLine) {
	
	wcscpy(lpCmdLine, cmdLine);
	hInstance = hInst;

	wchar_t fName[MAX_PATH];
	GetModuleFileName(NULL, fName, MAX_PATH);
	ExtractFilePath(Path, fName);

	musicCode = 0;
	KeysLocked = false;

	// Loads skins/files directories
	SkinsPath[0] = 0;
	FilesPath[0] = 0;
	LangsPath[0] = 0;

	HKEY hKey;

	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"\\Software\\Nitrogen\\", 0, 0, &hKey) == ERROR_SUCCESS) {
	
		wchar_t buf[MAX_PATH];
		DWORD ln;

		ln = MAX_PATH;
		if (RegQueryValueExW(hKey, L"SkinsPath", NULL, NULL, (BYTE*)&buf, &ln) == ERROR_SUCCESS) {
			wcscpy(SkinsPath, buf);
		}

		ln = MAX_PATH;
		if (RegQueryValueExW(hKey, L"FilesPath", NULL, NULL, (BYTE*)&buf, &ln) == ERROR_SUCCESS) {
			wcscpy(FilesPath, buf);
		}

		ln = MAX_PATH;
		if (RegQueryValueExW(hKey, L"LangsPath", NULL, NULL, (BYTE*)&buf, &ln) == ERROR_SUCCESS) {
			wcscpy(LangsPath, buf);
		}

		RegCloseKey(hKey);
	}

	if (SkinsPath[0] == 0) {
		wsprintf(SkinsPath, L"%sskins\\", Path);
	}

	if (FilesPath[0] == 0) {
		wcscpy(FilesPath, Path);
	}

	if (LangsPath[0] == 0) {
		wsprintf(LangsPath, L"%slang\\", Path);
	}

	MAP = MAP_Initialize();
	
	ExtCount = 3;
	for (int i = 0; i < MAP_GetDecoderPlugInCount(MAP); i++) {
		MAP_DEC_PLUGIN * mplug = MAP_GetDecoderPlugIn(MAP, i);
		ExtCount += mplug->GetFileExtCount();
	}

	ExtNames = new WEXTSTR[ExtCount];
	wcscpy(ExtNames[0], L"mp3");
	wcscpy(ExtNames[1], L"ogg");
	wcscpy(ExtNames[2], L"wav");

	int y = 3;
	for (int i = 0; i < MAP_GetDecoderPlugInCount(MAP); i++) {
		MAP_DEC_PLUGIN * mplug = MAP_GetDecoderPlugIn(MAP, i);
		for (int x = 0; x < mplug->GetFileExtCount(); x++) {
			
			WEXTSTR bufExtName;
			wchar_t bufExtDesc[40];

			mplug->GetFileExt(x, bufExtName, bufExtDesc);
			wcscpy(ExtNames[y], bufExtName);
			y++;
		}
	}

	PlayingSongDuration = 0;
	PlayingSongPos = 0;
	PlayingIndex = -1;

	lpConfig = new CConfig;

	wchar_t fn[MAX_PATH];
	wsprintf(fn, L"%sconfig.dat", FilesPath);
	if (!lpConfig->LoadFromFile(fn)) {
		lpConfig->LoadDefaults();
	}

	lang_init();
	if (lpConfig->cf.sLangFile[0] == 0) {

		lang_loaddefaults();

	} else {

		wchar_t pt[MAX_PATH];
		wsprintf(pt, L"%s%s", LangsPath, lpConfig->cf.sLangFile);
		lang_loadfromfile(pt);

	}

	lpPlaylist = new CPlaylist;
	lpWndMain = new CWndMain;
	lpWndBrowser = new CWndBrowser;
	lpBrMedia = new CBrMedia;
	lpWndEqualizer = new CWndEqualizer;
	lpWndAbout = new CWndAbout;
	lpDlgSleep = new CDlgSleep;
	lpDlgSettings = new CDlgSettings;
	lpDlgFileAssoc = new CDlgFileAssoc;
	lpDlgLists = new CDlgLists;
	lpBrSkin = new CBrSkin;
	lpBrLang = new CBrLang;

}

void CNtPlayer::OnChangePlayerStatus(MAP_STATUS newStatus) {

	if (newStatus == MAP_STATUS_PLAY) {
		SetTimer(lpWndMain->hWnd, TMR_UPDATEPOS, 1000, NULL);
		SetTimer(lpWndMain->hWnd, TMR_RESETIDLE, 7000, NULL);
	} else {
		KillTimer(lpWndMain->hWnd, TMR_UPDATEPOS);
		KillTimer(lpWndMain->hWnd, TMR_RESETIDLE);
	}

	if (lpConfig->cf.WriteSongRegistry && newStatus != MAP_STATUS_WAIT) {
	
		HKEY hKey;
		if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"System\\State\\Nitrogen", 0, NULL, REG_OPTION_NON_VOLATILE, 0, NULL, &hKey, NULL) == ERROR_SUCCESS) {

			DWORD pStatus = (MAP_GetStatus(MAP) == MAP_STATUS_PLAY)*2+(PlayingIndex == lpPlaylist->Index);
			RegSetValueExW(hKey, L"PlayStatus", 0, REG_DWORD, (BYTE*)&pStatus, 4);
			RegCloseKey(hKey);
	
		}
	}
}

void CNtPlayer::OnSongEOF() {

	if (PlayingIndex == lpPlaylist->Index) {

		int ind = lpPlaylist->Index;

		if (lpConfig->cf.Shuffle) {
			ind = lpPlaylist->ShuffleIndex;
		}

		if ((ind < lpPlaylist->Count-1) | (lpConfig->cf.Repeat)) {
			player()->PlayingSongPos = 0;
			ChangePlaylistIndex(+1, true);
			Play();
		} else {
			StopAndClose();
			lpBrMedia->UpdatePlaylist();
		}

	} else {
		Play();
	}

}

void CNtPlayer::ChangeVolume(int newVolume, bool relative) {

	if (relative) {
		lpConfig->cf.Volume += newVolume;
		if (lpConfig->cf.Volume > 100) { lpConfig->cf.Volume = 100; }
		if (lpConfig->cf.Volume < 0) { lpConfig->cf.Volume = 0; }
	} else {
		lpConfig->cf.Volume = newVolume;
	}

	wchar_t s[5];
	wsprintf(s, L"%i%%", lpConfig->cf.Volume);
	iSetText(ITT_VOLUME, s);
	iSetTrackPosition(IRT_VOLUME, (double)lpConfig->cf.Volume/100);

	DWORD vol = (DWORD)((lpConfig->cf.Volume*65535)/100);
	MAP_SetVolume(MAP, MAKELONG(vol, vol), false);

	lpWndMain->Repaint(PNT_VOLUME, 0);

}

void CNtPlayer::UpdatePrevNextToggle() {

	if (lpPlaylist->Count > 0) {
		if (!lpConfig->cf.Shuffle) {
			
			iSetCmdToggle(ICMD_PREVSONG, (lpPlaylist->Index == 0));
			iSetCmdToggle(ICMD_NEXTSONG, (lpPlaylist->Index == lpPlaylist->Count-1));

		} else {

			iSetCmdToggle(ICMD_PREVSONG, (lpPlaylist->ShuffleIndex == 0));
			iSetCmdToggle(ICMD_NEXTSONG, (lpPlaylist->ShuffleIndex == lpPlaylist->Count-1));
		}
	} else {

		iSetCmdToggle(ICMD_PREVSONG, true);
		iSetCmdToggle(ICMD_NEXTSONG, true);

	}
	lpWndMain->Repaint(PNT_PLAYBACK, 0);
}

void CNtPlayer::ChangePlaylistIndex(int newIndex, bool relative) {
	
	if (lpPlaylist->Count > 0) {
		
		if (relative) {
		
			if (lpConfig->cf.Shuffle) {

				lpPlaylist->ShuffleIndex += newIndex;
				while (lpPlaylist->ShuffleIndex >= lpPlaylist->Count) {
					lpPlaylist->ShuffleIndex -= lpPlaylist->Count;
				}
				while (lpPlaylist->ShuffleIndex < 0) {
					lpPlaylist->ShuffleIndex += lpPlaylist->Count;			
				}
				for (int i = 0; i < lpPlaylist->Count; i++) {

					if (lpPlaylist->Data[i].ShuffleIndex == lpPlaylist->ShuffleIndex) {
						lpPlaylist->Index = i;
					}
				
				}

			} else {

				lpPlaylist->Index += newIndex;
				while (lpPlaylist->Index >= lpPlaylist->Count) {
					lpPlaylist->Index -= lpPlaylist->Count;
				}
				while (lpPlaylist->Index < 0) {
					lpPlaylist->Index += lpPlaylist->Count;			
				}
			
			}
		} else {
			if (newIndex >= lpPlaylist->Count) {
				newIndex = lpPlaylist->Count-1;
			}
			if (newIndex < 0) {
				newIndex = 0;
			}
			lpPlaylist->Index = newIndex;
			lpPlaylist->ShuffleIndex = lpPlaylist->Data[newIndex].ShuffleIndex;
		}

		SONGINFO sInfo;
		GetSongInfo(&sInfo, lpPlaylist->Data[lpPlaylist->Index].FileName);

		GetAlbumArtFilename(lpPlaylist->IndexAlbumArtFilename, lpPlaylist->Data[lpPlaylist->Index].FileName, sInfo.songAlbum);

		iSetText(ITT_SONGNAME, sInfo.songName);
		iSetText(ITT_SONGARTIST, sInfo.songArtist);

		iSetCmdToggle(ICMD_PLAYPAUSE, (MAP_GetStatus(MAP) == MAP_STATUS_PLAY) & (PlayingIndex == lpPlaylist->Index));

		if (PlayingIndex == -1) {

			PlayingIndex = lpPlaylist->Index;
			lpPlaylist->ReadSongDuration();
		
		}

	} else {

		player()->PlayingIndex = -1;
		player()->PlayingSongPos = 0;
		player()->PlayingSongDuration = 0;
		iSetText(ITT_SONGNAME, _str(STR_EMPTYPLAYLIST));
		iSetText(ITT_SONGARTIST, _str(STR_UNKNOWNARTIST));
		lpPlaylist->IndexAlbumArtFilename[0] = 0;
		iSetCmdToggle(ICMD_PLAYPAUSE, false);

	}

	// musicCode is available to other applications through WM_NTG_GETMUSICCODE
	musicCode++;

	if (lpConfig->cf.WriteSongRegistry) {
	
		HKEY hKey;
		if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"System\\State\\Nitrogen", 0, NULL, REG_OPTION_NON_VOLATILE, 0, NULL, &hKey, NULL) == ERROR_SUCCESS) {

			wchar_t buf[100];

			iGetText(buf, ITT_SONGNAME);
			RegSetValueExW(hKey, L"SongTitle", 0, REG_SZ, (BYTE*)buf, wcslen(buf)*2+2);

			iGetText(buf, ITT_SONGARTIST);
			RegSetValueExW(hKey, L"SongArtist", 0, REG_SZ, (BYTE*)buf, wcslen(buf)*2+2);

			RegSetValueExW(hKey, L"MusicCode", 0, REG_DWORD, (BYTE*)&musicCode, 4);

			DWORD pStatus = (MAP_GetStatus(MAP) == MAP_STATUS_PLAY)*2+(PlayingIndex == lpPlaylist->Index);
			RegSetValueExW(hKey, L"PlayStatus", 0, REG_DWORD, (BYTE*)&pStatus, 4);
			
			if (lpPlaylist->IndexAlbumArtFilename[0] != 0) {
				RegSetValueExW(hKey, L"AlbumArtFilename", 0, REG_SZ, (BYTE*)lpPlaylist->IndexAlbumArtFilename, wcslen(lpPlaylist->IndexAlbumArtFilename)*2+2);
			} else {
				RegSetValueExW(hKey, L"AlbumArtFilename", 0, REG_SZ, (BYTE*)L"(none)", 14);
			}

			RegCloseKey(hKey);
		}
	}

	lpWndMain->UpdatePlayPos();
	UpdatePrevNextToggle();

	SetTimer(lpWndMain->hWnd, TMR_ALBUMART, 1000, NULL);
	lpWndMain->Repaint(PNT_SONGINFO, 0);
	lpBrMedia->UpdatePlaylist();
}

bool CNtPlayer::Play() {

	if (MAP_GetStatus(MAP) == MAP_STATUS_PAUSE) {
		if (PlayingIndex == lpPlaylist->Index) {
			MAP_Play(MAP);
			iSetCmdToggle(ICMD_PLAYPAUSE, true);
			lpWndMain->Repaint(PNT_BTCOMMAND, ICMD_PLAYPAUSE);
			return true;
		}
	}

	if ((lpPlaylist->Count > 0) & (lpPlaylist->Index < lpPlaylist->Count) & (lpPlaylist->Index > -1)) {

		if (MAP_GetStatus(MAP) != MAP_STATUS_STOP) {
			MAP_Stop(MAP);
		}
		
		MAP_Close(MAP);
		int delCnt = 0;
		lpPlaylist->ClearDelList();
		
		while (lpPlaylist->Data[lpPlaylist->Index].DelMark == false) {
			
			if (MAP_Open(MAP, lpPlaylist->Data[lpPlaylist->Index].FileName)) {
				break;
			} else {
				delCnt++;
				lpPlaylist->Data[lpPlaylist->Index].DelMark = true;
				ChangePlaylistIndex(+1, true);
			}

		}

		if (lpPlaylist->Data[lpPlaylist->Index].DelMark == false) {
		
			if (delCnt > 0) {
				lpPlaylist->DeleteMarked();
				lpPlaylist->RebuildShuffle();
			}

			MAP_Play(MAP);
			PlayingSongDuration = MAP_GetDuration(MAP)/1000;
			if (delCnt == 0 && PlayingIndex == lpPlaylist->Index && PlayingSongPos > 0) {
				MAP_Seek(MAP, PlayingSongPos*1000);
			} else {
				PlayingSongPos = 0;
			}
			lpWndMain->UpdatePlayPos();
			PlayingIndex = lpPlaylist->Index;
			iSetCmdToggle(ICMD_PLAYPAUSE, true);
			lpWndMain->Repaint(PNT_BTCOMMAND, ICMD_PLAYPAUSE);
			lpBrMedia->UpdatePlaylist();
			return true;

		} else {

			lpPlaylist->Clear();
			iSetCmdToggle(ICMD_PLAYPAUSE, false);
			lpWndMain->Repaint(PNT_BTCOMMAND, ICMD_PLAYPAUSE);
			lpBrMedia->UpdatePlaylist();
			return false;
		
		}

	}
	return false;
}

bool CNtPlayer::StopAndClose() {
	if (MAP_GetStatus(MAP) != MAP_STATUS_STOP) {
		MAP_Stop(MAP);
	}
	MAP_Close(MAP);
	iSetCmdToggle(ICMD_PLAYPAUSE, false);

	PlayingSongPos = 0;
	lpWndMain->UpdatePlayPos();

	lpWndMain->Repaint(PNT_FORCEFULL, 0);
	lpBrMedia->UpdatePlaylist();

	return true;
}

bool CNtPlayer::Pause() {
	if (MAP_GetStatus(MAP) == MAP_STATUS_PLAY) {
		MAP_Pause(MAP);
		iSetCmdToggle(ICMD_PLAYPAUSE, false);
		lpWndMain->Repaint(PNT_BTCOMMAND, ICMD_PLAYPAUSE);
		return true;
	}
	return false;
}

void CNtPlayer::ToggleDisplay(_VIDEO_POWER_STATE PowerState) {
	HDC hDC = GetDC(lpWndMain->hWnd);
	int nESC = SETPOWERMANAGEMENT;
	if (ExtEscape(hDC, QUERYESCSUPPORT, sizeof(int), (LPCSTR)&nESC, 0, NULL)) {
		VIDEO_POWER_MANAGEMENT vpm;
		vpm.Length = sizeof(VIDEO_POWER_MANAGEMENT);
		vpm.DPMSVersion = 0x0001;
		vpm.PowerState = PowerState;
		ExtEscape(hDC, SETPOWERMANAGEMENT, vpm.Length, (LPCSTR) &vpm, 0, NULL);
		if (PowerState == VideoPowerOff && lpConfig->cf.LockHardKeys) {
			KeysLocked = true;
			SetTimer(lpWndMain->hWnd, TMR_UNLOCKKEYS, 1000, NULL);
		}
	}
	ReleaseDC(NULL, hDC);
}

bool CNtPlayer::IsDisplayOn() {

	bool result = true;
	HDC hDC = GetDC(lpWndMain->hWnd);
	int nESC = SETPOWERMANAGEMENT;

	if (ExtEscape(hDC, QUERYESCSUPPORT, sizeof(int), (LPCSTR)&nESC, 0, NULL)) {
		VIDEO_POWER_MANAGEMENT vpm;
		ExtEscape(hDC, GETPOWERMANAGEMENT, 0, NULL, sizeof(VIDEO_POWER_MANAGEMENT), (LPSTR)&vpm);
		if (vpm.PowerState != VideoPowerOn) {
			result = false;
		}
	}

	ReleaseDC(NULL, hDC);
	return result;

}

CNtPlayer::~CNtPlayer() {

	delete lpConfig;
	delete lpPlaylist;
	delete lpWndMain;
	delete lpWndBrowser;
	delete lpBrMedia;
	delete lpWndEqualizer;
	delete lpWndAbout;
	delete lpDlgSleep;
	delete lpDlgSettings;
	delete lpDlgFileAssoc;
	delete lpDlgLists;
	delete lpBrSkin;
	delete lpBrLang;

	MAP_Uninitialize(MAP);
	lang_destroy();

	delete [] ExtNames;
}