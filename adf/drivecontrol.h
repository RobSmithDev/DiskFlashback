#pragma once

#define MESSAGEWINDOW_CLASS_NAME L"VIRTUALDRIVE_CONTROLLER_CLASS"

#define REMOTECTRL_RELEASE          1
#define REMOTECTRL_RESTORE          2
#define REMOTECTRL_FORMAT           3
#define REMOTECTRL_INSTALLBB        4
#define REMOTECTRL_COPYTODISK       5
#define REMOTECTRL_COPYTOADF        6
#define REMOTECTRL_EJECT            7

// These get turned into the above
#define CTRL_PARAM_FORMAT			L"FORMAT"
#define CTRL_PARAM_EJECT			L"EJECT"
#define CTRL_PARAM_INSTALLBB		L"BB"
#define CTRL_PARAM_COPY2DISK		L"2DISK"
#define CTRL_PARAM_BACKUP			L"BACKUP"

#define RETURNCODE_OK           0
#define RETURNCODE_BADARGS      1
#define RETURNCODE_BADLETTER    2
#define RETURNCODE_MOUNTFAIL    3

// Responses from drive copy request
#define MESSAGE_RESPONSE_OK					3
#define MESSAGE_RESPONSE_DRIVENOTFOUND		2
#define MESSAGE_RESPONSE_BADFORMAT          1
#define MESSAGE_RESPONSE_FAILED				0

#define COMMANDLINE_CONTROL					L"CONTROL"
#define COMMANDLINE_MOUNTDRIVE              L"BRIDGE"
#define COMMANDLINE_MOUNTFILE				L"FILE"



