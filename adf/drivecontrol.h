/* DiskFlashback, Copyright (C) 2021-2024 Robert Smith (@RobSmithDev)
 * https://robsmithdev.co.uk/diskflashback
 *
 * This file is multi-licensed under the terms of the Mozilla Public
 * License Version 2.0 as published by Mozilla Corporation and the
 * GNU General Public License, version 2 or later, as published by the
 * Free Software Foundation.
 *
 * MPL2: https://www.mozilla.org/en-US/MPL/2.0/
 * GPL2: https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 *
 * This file is maintained at https://github.com/RobSmithDev/DiskFlashback
 */

#pragma once

#define MESSAGEWINDOW_CLASS_NAME	L"VIRTUALDRIVE_CONTROLLER_CLASS"
#define APP_TITLE					L"DiskFlashback Tray Control"


#define WM_PHYSICAL_EJECT		(WM_USER + 1)
// Sent directly to a drive
#define WM_COPYTOFILE			(WM_USER + 2) 
#define WM_AUTORENAME			(WM_USER + 5)
#define WM_POPUP_INFO           (WM_USER + 6) 
 // Sent by other programs to "dismount" any real drives while they operate
 // 
 // Example: SendMessage(remoteWindow, WM_REMOTEUSAGE, (controllerType & 0x7FFF) | (release ? 0 : 0x8000), (LPARAM)GetCurrentProcessId());
#define WM_REMOTEUSAGE        (WM_USER+2)

#define REMOTECTRL_FORMAT           3
#define REMOTECTRL_INSTALLBB        4
#define REMOTECTRL_COPYTODISK       5
#define REMOTECTRL_COPYTOADF        6
#define REMOTECTRL_EJECT            7
#define REMOTECTRL_EJECT_SILENT     8
#define REMOTECTRL_CLEAN			9

// These get turned into the above
#define CTRL_PARAM_CLEAN			L"CLEAN"
#define CTRL_PARAM_FORMAT			L"FORMAT"
#define CTRL_PARAM_EJECT			L"EJECT"
#define CTRL_PARAM_INSTALLBB		L"BB"
#define CTRL_PARAM_COPY2DISK		L"2DISK"
#define CTRL_PARAM_BACKUP			L"BACKUP"

#define RETURNCODE_OK				 0
#define RETURNCODE_BADARGS			 1
#define RETURNCODE_BADLETTER		 2
#define RETURNCODE_MOUNTFAIL		 3
#define RETURNCODE_MOUNTFAILDRIVE	 4

// Responses from drive copy request
#define MESSAGE_RESPONSE_OK					3
#define MESSAGE_RESPONSE_DRIVENOTFOUND		2
#define MESSAGE_RESPONSE_BADFORMAT          1
#define MESSAGE_RESPONSE_FAILED				0

#define COMMANDLINE_CONTROL					L"CONTROL"
#define COMMANDLINE_MOUNTDRIVE              L"BRIDGE"
#define COMMANDLINE_MOUNTFILE				L"FILE"
#define COMMANDLINE_MOUNTRAW                L"RAW"



