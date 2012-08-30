/*
 * RSP Compiler plug in for Project 64 (A Nintendo 64 emulator).
 *
 * (c) Copyright 2001 jabo (jabo@emulation64.com) and
 * zilmar (zilmar@emulation64.com)
 *
 * pj64 homepage: www.pj64.net
 * 
 * Permission to use, copy, modify and distribute Project64 in both binary and
 * source form, for non-commercial purposes, is hereby granted without fee,
 * providing that this license information and copyright notice appear with
 * all copies and any derived work.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event shall the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Project64 is freeware for PERSONAL USE only. Commercial users should
 * seek permission of the copyright holders first. Commercial use includes
 * charging money for Project64 or software derived from Project64.
 *
 * The copyright holders request that bug fixes and improvements to the code
 * should be forwarded to them so if they want them.
 *
 */

#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include "Rsp.h"
#include "CPU.h"
#include "Recompiler CPU.h"
#include "Rsp Command.h"
#include "Rsp Registers.h"
#include "memory.h"
#include "breakpoint.h"
#include "profiling.h"
#include "log.h"
#include "resource.h"

void ProcessMenuItem(int ID);
BOOL CALLBACK CompilerDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

BOOL GraphicsHle = TRUE, AudioHle, ConditionalMove;
BOOL Profiling, IndvidualBlock, ShowErrors;
DWORD CPUCore = RecompilerCPU;

HANDLE hMutex = NULL;

DEBUG_INFO DebugInfo;
RSP_INFO RSPInfo;
HINSTANCE hinstDLL;
HMENU hRSPMenu = NULL;

DWORD AsciiToHex (char * HexValue) {
	DWORD Count, Finish, Value = 0;

	Finish = strlen(HexValue);
	if (Finish > 8 ) { Finish = 8; }

	for (Count = 0; Count < Finish; Count++){
		Value = (Value << 4);
		switch( HexValue[Count] ) {
		case '0': break;
		case '1': Value += 1; break;
		case '2': Value += 2; break;
		case '3': Value += 3; break;
		case '4': Value += 4; break;
		case '5': Value += 5; break;
		case '6': Value += 6; break;
		case '7': Value += 7; break;
		case '8': Value += 8; break;
		case '9': Value += 9; break;
		case 'A': Value += 10; break;
		case 'a': Value += 10; break;
		case 'B': Value += 11; break;
		case 'b': Value += 11; break;
		case 'C': Value += 12; break;
		case 'c': Value += 12; break;
		case 'D': Value += 13; break;
		case 'd': Value += 13; break;
		case 'E': Value += 14; break;
		case 'e': Value += 14; break;
		case 'F': Value += 15; break;
		case 'f': Value += 15; break;
		default: 
			Value = (Value >> 4);
			Count = Finish;
		}
	}
	return Value;
}

void DisplayError (char * Message, ...) {
	char Msg[400];
	va_list ap;

	va_start( ap, Message );
	vsprintf( Msg, Message, ap );
	va_end( ap );
	MessageBox(NULL,Msg,"Error",MB_OK|MB_ICONERROR);
}

/******************************************************************
  Function: CloseDLL
  Purpose:  This function is called when the emulator is closing
            down allowing the dll to de-initialise.
  input:    none
  output:   none
*******************************************************************/ 
__declspec(dllexport) void CloseDLL (void) {
	char RegPath[256];
	HKEY hKey = NULL;

	FreeMemory();
	
	sprintf(RegPath,"Software\\N64 Emulation\\DLL\\%s",AppName);
	if (RegOpenKeyEx(HKEY_CURRENT_USER, (LPSTR) RegPath, 0, KEY_ALL_ACCESS, &hKey) != ERROR_SUCCESS) {
		if (RegCreateKeyEx(HKEY_CURRENT_USER, RegPath, 0, TEXT(""), REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, 0) != ERROR_SUCCESS) {
			return;
		}
	}

	#ifndef EXTERNAL_RELEASE
	RegSetValueEx(hKey, "Compiler", 0, REG_BINARY, (BYTE*)&Compiler, sizeof(RSP_COMPILER));
	#endif

	RegSetValueEx(hKey, "Audio HLE", 0, REG_DWORD, (BYTE*)&AudioHle, sizeof(DWORD));
	RegSetValueEx(hKey, "CPU Core", 0, REG_DWORD, (BYTE*)&CPUCore, sizeof(DWORD));
	RegCloseKey(hKey);
}

/******************************************************************
  Function: DllAbout
  Purpose:  This function is optional function that is provided
            to give further information about the DLL.
  input:    a handle to the window that calls this function
  output:   none
*******************************************************************/ 
__declspec(dllexport) void DllAbout ( HWND hParent ) {
	MessageBox(hParent,AboutMsg,"About",MB_OK | MB_ICONINFORMATION );
}

BOOL WINAPI DllMain(  HINSTANCE hinst, DWORD fdwReason, LPVOID lpvReserved ){ 
	hinstDLL = hinst;
	return TRUE;
}
/******************************************************************
  Function: GetDllInfo
  Purpose:  This function allows the emulator to gather information
            about the dll by filling in the PluginInfo structure.
  input:    a pointer to a PLUGIN_INFO stucture that needs to be
            filled by the function. (see def above)
  output:   none
*******************************************************************/ 
__declspec(dllexport) void GetDllInfo ( PLUGIN_INFO * PluginInfo ) {
	PluginInfo->Version = 0x0101;
	PluginInfo->Type = PLUGIN_TYPE_RSP;
	sprintf(PluginInfo->Name,"RSP emulation Plugin");
	PluginInfo->NormalMemory = FALSE;
	PluginInfo->MemoryBswaped = TRUE;
}

/******************************************************************
  Function: GetRspDebugInfo
  Purpose:  This function allows the emulator to gather information
            about the debug capabilities of the dll by filling in
			the DebugInfo structure.
  input:    a pointer to a RSPDEBUG_INFO stucture that needs to be
            filled by the function. (see def above)
  output:   none
*******************************************************************/ 

#if !defined(EXTERNAL_RELEASE)

__declspec(dllexport) void GetRspDebugInfo ( RSPDEBUG_INFO * DebugInfo ) {
	HKEY hKeyResults = 0;
	char String[200];
	long lResult;

	hRSPMenu = LoadMenu(hinstDLL,"RspMenu");
	DebugInfo->hRSPMenu = hRSPMenu;
	DebugInfo->ProcessMenuItem = ProcessMenuItem;

	DebugInfo->UseBPoints = TRUE;
	sprintf(DebugInfo->BPPanelName," RSP ");
	DebugInfo->Add_BPoint = Add_BPoint;
	DebugInfo->CreateBPPanel = CreateBPPanel;
	DebugInfo->HideBPPanel = HideBPPanel;
	DebugInfo->PaintBPPanel = PaintBPPanel;
	DebugInfo->RefreshBpoints = RefreshBpoints;
	DebugInfo->RemoveAllBpoint = RemoveAllBpoint;
	DebugInfo->RemoveBpoint = RemoveBpoint;
	DebugInfo->ShowBPPanel = ShowBPPanel;
	
	DebugInfo->Enter_RSP_Commands_Window = Enter_RSP_Commands_Window;

	sprintf(String,"Software\\N64 Emulation\\DLL\\%s",AppName);
	lResult = RegOpenKeyEx( HKEY_CURRENT_USER,String,0,KEY_ALL_ACCESS,&hKeyResults);
	if (lResult == ERROR_SUCCESS) {
		DWORD Type, Bytes = 4;

		lResult = RegQueryValueEx(hKeyResults,"Profiling On",0,&Type,(LPBYTE)(&Profiling),&Bytes);
		if (Type != REG_DWORD || lResult != ERROR_SUCCESS) { 
			Profiling = Default_ProfilingOn;
		}
		lResult = RegQueryValueEx(hKeyResults,"Log Indvidual Blocks",0,&Type,(LPBYTE)(&IndvidualBlock),&Bytes);
		if (Type != REG_DWORD || lResult != ERROR_SUCCESS) { 
			IndvidualBlock = Default_IndvidualBlock;
		}		
		lResult = RegQueryValueEx(hKeyResults,"Show Compiler Errors",0,&Type,(LPBYTE)(&ShowErrors),&Bytes);
		if (Type != REG_DWORD || lResult != ERROR_SUCCESS) { 
			ShowErrors = Default_ShowErrors;
		}
		RegCloseKey(hKeyResults);
	}
	if (Profiling) {
		CheckMenuItem( hRSPMenu, ID_PROFILING_ON, MF_BYCOMMAND | MFS_CHECKED );
	} else {
		CheckMenuItem( hRSPMenu, ID_PROFILING_OFF, MF_BYCOMMAND | MFS_CHECKED );
	}
	if (IndvidualBlock) {
		CheckMenuItem( hRSPMenu, ID_PROFILING_LOGINDIVIDUALBLOCKS, MF_BYCOMMAND | MFS_CHECKED );
	}
	if (ShowErrors) {
		CheckMenuItem( hRSPMenu, ID_SHOWCOMPILERERRORS, MF_BYCOMMAND | MFS_CHECKED );
	}
}

#endif

int GetStoredWinPos( char * WinName, DWORD * X, DWORD * Y ) {
	long lResult;
	HKEY hKeyResults = 0;
	char String[200];

	sprintf(String,"Software\\N64 Emulation\\DLL\\%s\\Page Setup", AppName);
	lResult = RegOpenKeyEx( HKEY_CURRENT_USER,String,0, KEY_ALL_ACCESS,&hKeyResults);
	
	if (lResult == ERROR_SUCCESS) {
		DWORD Type, Value, Bytes = 4;

		sprintf(String,"%s Top",WinName);
		lResult = RegQueryValueEx(hKeyResults,String,0,&Type,(LPBYTE)(&Value),&Bytes);
		if (Type == REG_DWORD && lResult == ERROR_SUCCESS) { 
			*Y = Value;
		} else {
			RegCloseKey(hKeyResults);
			return FALSE;
		}
	
		sprintf(String,"%s Left",WinName);
		lResult = RegQueryValueEx(hKeyResults,String,0,&Type,(LPBYTE)(&Value),&Bytes);
		if (Type == REG_DWORD && lResult == ERROR_SUCCESS) { 
			*X = Value;
		} else {
			RegCloseKey(hKeyResults);
			return FALSE;
		}
		RegCloseKey(hKeyResults);
		return TRUE;
	}
	return FALSE;
}

void StoreCurrentWinPos (  char * WinName, HWND hWnd ) {
	long lResult;
	HKEY hKeyResults = 0;
	DWORD Disposition = 0;
	RECT WinRect;
	char String[200];

	GetWindowRect(hWnd, &WinRect );
	sprintf(String,"Software\\N64 Emulation\\DLL\\%s\\Page Setup",AppName);
	lResult = RegCreateKeyEx( HKEY_CURRENT_USER, String,0,"", REG_OPTION_NON_VOLATILE,
		KEY_ALL_ACCESS,NULL, &hKeyResults,&Disposition);
	if (lResult == ERROR_SUCCESS) {
		sprintf(String,"%s Top",WinName);
		RegSetValueEx(hKeyResults,String,0, REG_DWORD,(CONST BYTE *)(&WinRect.top),
			sizeof(DWORD));
		sprintf(String,"%s Left",WinName);
		RegSetValueEx(hKeyResults,String,0, REG_DWORD,(CONST BYTE *)(&WinRect.left),
			sizeof(DWORD));
	}
	RegCloseKey(hKeyResults);
}

/******************************************************************
  Function: InitiateRSP
  Purpose:  This function is called when the DLL is started to give
            information from the emulator that the n64 RSP 
			interface needs
  input:    Rsp_Info is passed to this function which is defined
            above.
			CycleCount is the number of cycles between switching
			control between teh RSP and r4300i core.
  output:   none
*******************************************************************/ 

RSP_COMPILER Compiler;

void DetectCpuSpecs(void) {
	DWORD Intel_Features = 0;
	DWORD AMD_Features = 0;

	__try {
		_asm {
			/* Intel features */
			mov eax, 1
			cpuid
			mov [Intel_Features], edx

			/* AMD features */
			mov eax, 80000001h
			cpuid
			or [AMD_Features], edx
		}
    } __except ( EXCEPTION_EXECUTE_HANDLER) {
		AMD_Features = Intel_Features = 0;
    }

	if (Intel_Features & 0x02000000) {
		Compiler.mmx2 = TRUE;
		Compiler.sse = TRUE;
	}
	if (Intel_Features & 0x00800000) {
		Compiler.mmx = TRUE;
	}
	if (AMD_Features & 0x40000000) {
		Compiler.mmx2 = TRUE;
	}
	if (Intel_Features & 0x00008000) {
		ConditionalMove = TRUE;
	} else {
		ConditionalMove = FALSE;
	}
}

__declspec(dllexport) void InitiateRSP ( RSP_INFO Rsp_Info, DWORD * CycleCount) {
	char RegPath[256];
	HKEY hKey = NULL;
	DWORD dwSize, lResult, Disposition;

	RSPInfo = Rsp_Info;
	memset(&Compiler, 0, sizeof(Compiler));
	
	Compiler.bAlignGPR = TRUE;
	Compiler.bAlignVector = TRUE;
	Compiler.bFlags = TRUE;
	Compiler.bReOrdering = TRUE;
	Compiler.bSections = TRUE;
	Compiler.bDest = TRUE;
	Compiler.bAccum = TRUE;
	Compiler.bGPRConstants = TRUE;
	
	DetectCpuSpecs();
	hMutex = CreateMutex(NULL, FALSE, NULL);

	sprintf(RegPath,"Software\\N64 Emulation\\DLL\\%s",AppName);
	lResult = RegCreateKeyEx( HKEY_CURRENT_USER,RegPath,0,"",
			REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,&hKey,&Disposition);

	if (lResult == ERROR_SUCCESS) {
		#if !defined( EXTERNAL_RELEASE )
		dwSize = sizeof(RSP_COMPILER);
		RegQueryValueEx(hKey, (LPSTR) "Compiler", NULL, (LPDWORD) 0, (LPBYTE) &Compiler, &dwSize);
		#endif

		dwSize = 4;
		lResult = RegQueryValueEx(hKey, (LPSTR) "Audio HLE", NULL, (LPDWORD) 0, (LPBYTE) &AudioHle, &dwSize);
		if (lResult != ERROR_SUCCESS) { 
			AudioHle = FALSE;
		}
		dwSize = 4;
		lResult = RegQueryValueEx(hKey, (LPSTR) "CPU Core", NULL, (LPDWORD) 0, (LPBYTE) &CPUCore, &dwSize);
		if (lResult != ERROR_SUCCESS) { 
			CPUCore = RecompilerCPU;
		}
		RegCloseKey(hKey);
	}
		
	*CycleCount = 0;
	AllocateMemory();
	InitilizeRSPRegisters();
	Build_RSP();
	#ifdef GenerateLog
	Start_Log();
	#endif
}

/******************************************************************
  Function: InitiateRSPDebugger
  Purpose:  This function is called when the DLL is started to give
            information from the emulator that the n64 RSP 
			interface needs to intergrate the debugger with the
			rest of the emulator.
  input:    DebugInfo is passed to this function which is defined
            above.
  output:   none
*******************************************************************/ 

#if !defined(EXTERNAL_RELEASE)

__declspec(dllexport) void InitiateRSPDebugger ( DEBUG_INFO Debug_Info) {
	DebugInfo = Debug_Info;
}

void ProcessMenuItem(int ID) {
	DWORD Disposition;
	HKEY hKeyResults;
	char String[200];
	long lResult;
	UINT uState;

	switch (ID) {
	case ID_RSPCOMMANDS: Enter_RSP_Commands_Window(); break;
	case ID_RSPREGISTERS: Enter_RSP_Register_Window(); break;
	case ID_DUMP_RSPCODE: DumpRSPCode(); break;
	case ID_DUMP_DMEM: DumpRSPData(); break;
	case ID_PROFILING_ON:
	case ID_PROFILING_OFF:
		uState = GetMenuState( hRSPMenu, ID_PROFILING_ON, MF_BYCOMMAND);			
		hKeyResults = 0;
		Disposition = 0;

		if ( uState & MFS_CHECKED ) {								
			CheckMenuItem( hRSPMenu, ID_PROFILING_ON, MF_BYCOMMAND | MFS_UNCHECKED );
			CheckMenuItem( hRSPMenu, ID_PROFILING_OFF, MF_BYCOMMAND | MFS_CHECKED );
			GenerateTimerResults();
			Profiling = FALSE;
		} else {
			CheckMenuItem( hRSPMenu, ID_PROFILING_ON, MF_BYCOMMAND | MFS_CHECKED );
			CheckMenuItem( hRSPMenu, ID_PROFILING_OFF, MF_BYCOMMAND | MFS_UNCHECKED );
			ResetTimerList();
			Profiling = TRUE;
		}
			
		sprintf(String,"Software\\N64 Emulation\\DLL\\%s",AppName);
		lResult = RegCreateKeyEx( HKEY_CURRENT_USER,String,0,"",
			REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,&hKeyResults,&Disposition);
			
		if (lResult == ERROR_SUCCESS) { 
			RegSetValueEx(hKeyResults,"Profiling On",0,REG_DWORD,(BYTE *)&Profiling,sizeof(DWORD));
		}
		RegCloseKey(hKeyResults);
		break;
	case ID_PROFILING_RESETSTATS: ResetTimerList(); break;
	case ID_PROFILING_GENERATELOG: GenerateTimerResults(); break;
	case ID_PROFILING_LOGINDIVIDUALBLOCKS:
		uState = GetMenuState( hRSPMenu, ID_PROFILING_LOGINDIVIDUALBLOCKS, MF_BYCOMMAND);
		hKeyResults = 0;
		Disposition = 0;

		ResetTimerList();
		if ( uState & MFS_CHECKED ) {								
			CheckMenuItem( hRSPMenu, ID_PROFILING_LOGINDIVIDUALBLOCKS, MF_BYCOMMAND | MFS_UNCHECKED );
			IndvidualBlock = FALSE;
		} else {
			CheckMenuItem( hRSPMenu, ID_PROFILING_LOGINDIVIDUALBLOCKS, MF_BYCOMMAND | MFS_CHECKED );
			IndvidualBlock = TRUE;
		}
			
		sprintf(String,"Software\\N64 Emulation\\DLL\\%s",AppName);
		lResult = RegCreateKeyEx( HKEY_CURRENT_USER,String,0,"",
		REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,&hKeyResults,&Disposition);
			
		if (lResult == ERROR_SUCCESS) { 
			RegSetValueEx(hKeyResults,"Log Indvidual Blocks",0,REG_DWORD,
				(BYTE *)&IndvidualBlock,sizeof(DWORD));
		}
		RegCloseKey(hKeyResults);
		break;
	case ID_SHOWCOMPILERERRORS:
		uState = GetMenuState(hRSPMenu, ID_SHOWCOMPILERERRORS, MF_BYCOMMAND);
		
		if (uState & MFS_CHECKED) {
			CheckMenuItem( hRSPMenu, ID_SHOWCOMPILERERRORS, MF_BYCOMMAND | MFS_UNCHECKED );
			ShowErrors = FALSE;
		} else {
			CheckMenuItem( hRSPMenu, ID_SHOWCOMPILERERRORS, MF_BYCOMMAND | MFS_CHECKED );
			ShowErrors = TRUE;
		}
		sprintf(String,"Software\\N64 Emulation\\DLL\\%s",AppName);
		lResult = RegCreateKeyEx( HKEY_CURRENT_USER,String,0,"",
		REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,&hKeyResults,&Disposition);
			
		if (lResult == ERROR_SUCCESS) { 
			RegSetValueEx(hKeyResults,"Show Compiler Errors",0,REG_DWORD,
				(BYTE *)&ShowErrors, sizeof(DWORD));
		}
		RegCloseKey(hKeyResults);
		break;
	case ID_COMPILER:		
		DialogBox(hinstDLL, "RSPCOMPILER", HWND_DESKTOP, CompilerDlgProc);
		break;
	}
}

#endif

/******************************************************************
  Function: RomClosed
  Purpose:  This function is called when a rom is closed.
  input:    none
  output:   none
*******************************************************************/ 
__declspec(dllexport) void RomClosed (void) {
	void ClearAllx86Code(void);

	if (Profiling) {
		StopTimer();
		GenerateTimerResults();
	}
	InitilizeRSPRegisters();
	ClearAllx86Code();

	#ifdef GenerateLog
	Stop_Log();
	#endif
	#ifdef Log_x86Code
	Stop_x86_Log();
	#endif
}

BOOL GetBooleanCheck(HWND hDlg, DWORD DialogID) {
	return (IsDlgButtonChecked(hDlg, DialogID) == BST_CHECKED) ? TRUE : FALSE;
}

BOOL CALLBACK CompilerDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	extern BYTE * pLastSecondary;
	char Buffer[256];

	switch (uMsg) {	
	case WM_INITDIALOG:	
		if (Compiler.mmx == TRUE) 
			CheckDlgButton(hDlg, IDC_CHECK_MMX, BST_CHECKED);
		if (Compiler.mmx2 == TRUE)
			CheckDlgButton(hDlg, IDC_CHECK_MMX2, BST_CHECKED);
		if (Compiler.sse == TRUE) 
			CheckDlgButton(hDlg, IDC_CHECK_SSE, BST_CHECKED);
		
		if (Compiler.bAlignGPR == TRUE)
			CheckDlgButton(hDlg, IDC_COMPILER_ALIGNGPR, BST_CHECKED);
		if (Compiler.bAlignVector == TRUE)
			CheckDlgButton(hDlg, IDC_COMPILER_ALIGNVEC, BST_CHECKED);

		if (Compiler.bSections == TRUE) 
			CheckDlgButton(hDlg, IDC_COMPILER_SECTIONS, BST_CHECKED);
		if (Compiler.bGPRConstants == TRUE)
			CheckDlgButton(hDlg, IDC_COMPILER_GPRCONSTANTS, BST_CHECKED);
		if (Compiler.bReOrdering == TRUE)
			CheckDlgButton(hDlg, IDC_COMPILER_REORDER, BST_CHECKED);
		if (Compiler.bFlags == TRUE)
			CheckDlgButton(hDlg, IDC_COMPILER_FLAGS, BST_CHECKED);
		if (Compiler.bAccum == TRUE)
			CheckDlgButton(hDlg, IDC_COMPILER_ACCUM, BST_CHECKED);
		if (Compiler.bDest == TRUE)
			CheckDlgButton(hDlg, IDC_COMPILER_DEST, BST_CHECKED);

		SetTimer(hDlg, 1, 250, NULL);
		break;

	case WM_TIMER:
		sprintf(Buffer, "x86: %2.2f KB / %2.2f KB", (float)(RecompPos - RecompCode) / 1024.0F,
			pLastSecondary?(float)((pLastSecondary - RecompCodeSecondary) / 1024.0F):0);

		SetDlgItemText(hDlg, IDC_COMPILER_BUFFERS, Buffer);
		break;

	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam)) {
		case IDOK:
			Compiler.mmx = GetBooleanCheck(hDlg, IDC_CHECK_MMX);
			Compiler.mmx2 = GetBooleanCheck(hDlg, IDC_CHECK_MMX2);
			Compiler.sse = GetBooleanCheck(hDlg, IDC_CHECK_SSE);
			Compiler.bSections = GetBooleanCheck(hDlg, IDC_COMPILER_SECTIONS);
			Compiler.bReOrdering = GetBooleanCheck(hDlg, IDC_COMPILER_REORDER);
			Compiler.bGPRConstants = GetBooleanCheck(hDlg, IDC_COMPILER_GPRCONSTANTS);
			Compiler.bFlags = GetBooleanCheck(hDlg, IDC_COMPILER_FLAGS);
			Compiler.bAccum = GetBooleanCheck(hDlg, IDC_COMPILER_ACCUM);
			Compiler.bDest = GetBooleanCheck(hDlg, IDC_COMPILER_DEST);
			Compiler.bAlignGPR = GetBooleanCheck(hDlg, IDC_COMPILER_ALIGNGPR);
			Compiler.bAlignVector = GetBooleanCheck(hDlg, IDC_COMPILER_ALIGNVEC);
			KillTimer(hDlg, 1);
			EndDialog(hDlg, TRUE);
			break;

		case IDCANCEL:	
			KillTimer(hDlg, 1);
			EndDialog(hDlg, TRUE);
			break;
		}
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

BOOL CALLBACK ConfigDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	HWND hWndItem;
	DWORD value;

	switch (uMsg) {	
	case WM_INITDIALOG:	
		if (AudioHle == TRUE) {
			CheckDlgButton(hDlg, IDC_AUDIOHLE, BST_CHECKED);
		}

		if (GraphicsHle == TRUE) {
			CheckDlgButton(hDlg, IDC_GRAPHICSHLE, BST_CHECKED);
		}

		hWndItem = GetDlgItem(hDlg, IDC_COMPILER_SELECT);
		ComboBox_AddString(hWndItem, "Interpreter");
		ComboBox_AddString(hWndItem, "Recompiler");
		ComboBox_SetCurSel(hWndItem, CPUCore);
		break;

	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam)) {
		case IDOK:
			hWndItem = GetDlgItem(hDlg, IDC_COMPILER_SELECT);
			value = ComboBox_GetCurSel(hWndItem);
			SetCPU(value);

			AudioHle = GetBooleanCheck(hDlg, IDC_AUDIOHLE);
			GraphicsHle = GetBooleanCheck(hDlg, IDC_GRAPHICSHLE);

			EndDialog(hDlg, TRUE);
			break;
		case IDCANCEL:	
			EndDialog(hDlg, TRUE);
			break;
		}
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

__declspec(dllexport) void DllConfig (HWND hWnd) {
	DialogBox(hinstDLL, "RSPCONFIG", hWnd, ConfigDlgProc);
}