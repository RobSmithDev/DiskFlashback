; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!

#define MyAppName "DiskFlashback"
#define MyAppVersion "1.0"
#define MyAppPublisher "RobSmithDev"
#define MyAppURL "https://robsmithdev.co.uk/diskflashback"
#define MyAppExeName "DiskFlashback.exe"
#define MyAppAssocKey "DiskFlashback"


[Setup]
; NOTE: The value of AppId uniquely identifies this application. Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{ABCA6F16-2CF6-4CD9-AB97-672E757E598A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}VersionInfoVersion={#MyAppVersion}
AppCopyright=� 2024 {#MyAppPublisher}
;AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
ChangesAssociations=yes
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=D:\Virtual Floppy Drive\dokany-master\FloppyDrive\installer\adflib.txt
; Uncomment the following line to run in non administrative install mode (install for current user only.)
;PrivilegesRequired=lowest
OutputDir=D:\Virtual Floppy Drive\dokany-master\FloppyDrive\
OutputBaseFilename=DiskFlashbackInstall
//SetupIconFile=D:\Virtual Floppy Drive\dokany-master\FloppyDrive\adf\floppy2.ico
//UninstallDisplayIcon=D:\Virtual Floppy Drive\dokany-master\FloppyDrive\adf\floppy2.ico
Compression=zip
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "D:\Virtual Floppy Drive\dokany-master\FloppyDrive\installer\dokansetup.exe"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall; AfterInstall: InstallDokan
Source: "D:\Virtual Floppy Drive\dokany-master\x64\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "D:\Virtual Floppy Drive\dokany-master\x64\Release\FloppyBridge.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "D:\Virtual Floppy Drive\dokany-master\x64\Release\FloppyBridge_x64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "D:\Virtual Floppy Drive\dokany-master\FloppyDrive\ADFlib\win32\src\Release\adflib.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "D:\Virtual Floppy Drive\dokany-master\FloppyDrive\installer\adflib.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "D:\Virtual Floppy Drive\dokany-master\FloppyDrive\installer\fatfs.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "D:\Virtual Floppy Drive\dokany-master\FloppyDrive\installer\dokan2.dll"; DestDir: "{app}"; Flags: ignoreversion
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\{#MyAppExeName}"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".adf"; ValueData: ""; 
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".dms"; ValueData: ""; 
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".hda"; ValueData: "";
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".hdf"; ValueData: "";
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".img"; ValueData: "";
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".ima"; ValueData: "";
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".st"; ValueData: "";
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".msa"; ValueData: "";
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".scp"; ValueData: "";
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\shell\mount"; ValueType: string; ValueName: ""; ValueData: "&Mount Disk"
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\shell\mount\icon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},1"
Root: HKCU; Subkey: "Software\Classes\Applications\{#MyAppExeName}\shell\mount\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""


; Create asociations, and default to us if nothing else exists
Root: HKCU; Subkey: "Software\Classes\.adf"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocKey}.amiga.fd"; Flags: uninsdeletevalue createvalueifdoesntexist
Root: HKCU; Subkey: "Software\Classes\.dms"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocKey}.amiga.fd"; Flags: uninsdeletevalue createvalueifdoesntexist
Root: HKCU; Subkey: "Software\Classes\.hda"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocKey}.amiga.hd"; Flags: uninsdeletevalue createvalueifdoesntexist
Root: HKCU; Subkey: "Software\Classes\.hdf"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocKey}.amiga.hd"; Flags: uninsdeletevalue createvalueifdoesntexist
Root: HKCU; Subkey: "Software\Classes\.img"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocKey}.ibmpc"; Flags: uninsdeletevalue createvalueifdoesntexist
Root: HKCU; Subkey: "Software\Classes\.ima"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocKey}.ibmpc"; Flags: uninsdeletevalue createvalueifdoesntexist
Root: HKCU; Subkey: "Software\Classes\.st";  ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocKey}.atarist"; Flags: uninsdeletevalue createvalueifdoesntexist
Root: HKCU; Subkey: "Software\Classes\.msa"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocKey}.atarist"; Flags: uninsdeletevalue createvalueifdoesntexist
Root: HKCU; Subkey: "Software\Classes\.scp"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocKey}.flux"; Flags: uninsdeletevalue createvalueifdoesntexist

; Setup the Open With Prog ID
Root: HKCU; Subkey: "Software\Classes\.adf\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}.amiga.fd"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.dms\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}.amiga.fd"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.hda\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}.amiga.hd"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.hdf\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}.amiga.hd"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.img\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}.ibmpc"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.ima\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}.ibmpc"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.st\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}.atarist"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.scp\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}.flux"; ValueData: ""; Flags: uninsdeletevalue

; Setup their default description
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.fd"; ValueType: string; ValueName: ""; ValueData: "Amiga Floppy Disk Image"; Flags: uninsdeletekey; Permissions: users-modify
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.hd"; ValueType: string; ValueName: ""; ValueData: "Amiga Hard Disk Image"; Flags: uninsdeletekey; Permissions: users-modify
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.ibmpc"; ValueType: string; ValueName: ""; ValueData: "IBM PC Disk Image"; Flags: uninsdeletekey; Permissions: users-modify
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.atarist"; ValueType: string; ValueName: ""; ValueData: "ST Disk Image"; Flags: uninsdeletekey; Permissions: users-modify
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.flux"; ValueType: string; ValueName: ""; ValueData: "Flux Level Disk Image"; Flags: uninsdeletekey; Permissions: users-modify

; And default icon
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.fd\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},1" 
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.hd\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},1" 
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.ibmpc\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0" 
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.atarist\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},3"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.flux\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},4"

; And add the "Mount" action
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.fd\shell"; ValueType: string; ValueName: ""; ValueData: "mount"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.fd\shell\mount"; ValueType: string; ValueName: ""; ValueData: "&Mount Disk"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.fd\shell\mount"; ValueType: string; ValueName: "icon"; ValueData: "{app}\{#MyAppExeName},1"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.fd\shell\mount\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.hd\shell"; ValueType: string; ValueName: ""; ValueData: "mount"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.hd\shell\mount"; ValueType: string; ValueName: ""; ValueData: "&Mount Disk"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.hd\shell\mount"; ValueType: string; ValueName: "icon"; ValueData: "{app}\{#MyAppExeName},1"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.amiga.hd\shell\mount\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.ibmpc\shell"; ValueType: string; ValueName: ""; ValueData: "mount"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.ibmpc\shell\mount"; ValueType: string; ValueName: ""; ValueData: "&Mount Disk"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.ibmpc\shell\mount"; ValueType: string; ValueName: "icon"; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.ibmpc\shell\mount\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.atarist\shell"; ValueType: string; ValueName: ""; ValueData: "mount"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.atarist\shell\mount"; ValueType: string; ValueName: ""; ValueData: "&Mount Disk"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.atarist\shell\mount"; ValueType: string; ValueName: "icon"; ValueData: "{app}\{#MyAppExeName},3"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.atarist\shell\mount\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.flux\shell"; ValueType: string; ValueName: ""; ValueData: "mount"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.flux\shell\mount"; ValueType: string; ValueName: ""; ValueData: "&Mount Disk"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.flux\shell\mount"; ValueType: string; ValueName: "icon"; ValueData: "{app}\{#MyAppExeName},4"
Root: HKCU; Subkey: "Software\Classes\{#MyAppAssocKey}.flux\shell\mount\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""



[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{userstartup}\My Program"; Filename: "{app}\{#MyAppExeName}"

[Run]
Filename: "https://robsmithdev.co.uk/diskflashback_intro"; Description: "View Getting Started Guide"; Flags: shellexec  postinstall runascurrentuser

[Code]

var CancelWithoutPrompt: boolean;
var page_option : TInputOptionWizardPage;
var AmigaForeverFolder, WinUAEFolder:String;
var AmigaForeverIndex, WinUAEIndex:Integer;

Procedure CloseApplication;
Var W:HWND;
    A:Integer;
Begin
     W:=FindWindowByWindowName('DiskFlashback Tray Control');
     If (W<>0) then SendMessage(W,$400+10,20,30);
     For A:=1 To 10 Do
     Begin
          W:=FindWindowByWindowName('DiskFlashback Tray Control');
          IF W<>0 Then Sleep(100);
     End;
End;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  case CurUninstallStep of
    usUninstall:
      begin
        CloseApplication();
      end;
  end;
end;

procedure InitializeWizard();
Var A,Counter:Integer;
    _FloppyBridgeVersion, _AmigaForeverVersion, _WinUAEVersion, Tmp:String;
    FloppyBridgeVersion,AmigaForeverVersion, WinUAEVersion:Int64;
begin
    ExtractTemporaryFile('FloppyBridge_x64.dll');
    GetVersionNumbersString(ExpandConstant('{tmp}\FloppyBridge_x64.dll'), _FloppyBridgeVersion);
    DeleteFile(ExpandConstant('{tmp}\FloppyBridge_x64.dll'));
    StrToVersion(_FloppyBridgeVersion, FloppyBridgeVersion);
    AmigaForeverIndex := -1;
    WinUAEIndex := -1;
    Counter:=0;

    if RegQueryStringValue(HKCR, 'Cloanto.RetroPlatform.lha\shell\open\command', '', AmigaForeverFolder) then
    begin     
        A:=Pos('" ', AmigaForeverFolder);
        AmigaForeverFolder := trim(Copy(AmigaForeverFolder,1,A));
        If (Copy(AmigaForeverFolder,1,1) = '"') and (Copy(AmigaForeverFolder,length(AmigaForeverFolder),1)= '"') Then
            AmigaForeverFolder:=Copy(AmigaForeverFolder,2,Length(AmigaForeverFolder)-2);
        AmigaForeverFolder:=ExtractFilePath(AmigaForeverFolder)+'WinUAE\Plugins\';
        If (Length(AmigaForeverFolder)<1) then AmigaForeverFolder:='C:\Program Files (x86)\Cloanto\Amiga Forever\WinUAE\plugins';
    end;

    if RegQueryStringValue(HKCR, 'WinUAE\shell\open\command', '', WinUAEFolder) then
    begin     
        A:=Pos('" ', WinUAEFolder);
        WinUAEFolder := trim(Copy(WinUAEFolder,1,A));
        If (Copy(WinUAEFolder,1,1) = '"') and (Copy(WinUAEFolder,length(WinUAEFolder),1)= '"') Then
            WinUAEFolder:=Copy(WinUAEFolder,2,Length(WinUAEFolder)-2);
        WinUAEFolder:=ExtractFilePath(WinUAEFolder)+'Plugins\';
        If (Length(WinUAEFolder)<1) then WinUAEFolder:='c:\program files\winuae\plugins\';
    end;

    page_option := CreateInputOptionPage(3,'FloppyBridge Plugin','DiskFlashback includes the latest version of the FloppyBridge plugin ('+_FloppyBridgeVersion+').','Install/update the plugin in the following products:',False,True);
    if (length(WinUAEFolder)>0) Then
    Begin
         GetVersionNumbersString(WinUAEFolder + 'FloppyBridge_x64.dll', _WinUAEVersion);
         StrToVersion(_WinUAEVersion, WinUAEVersion);
         if (ComparePackedVersion(WinUAEVersion, FloppyBridgeVersion)<0) then
         begin
             WinUAEIndex:=Counter; Inc(Counter);
             If WinUAEVersion<1 Then 
                page_option.Add('Install FloppyBridge for WinUAE')
             Else page_option.Add('Update WinUAE FloppyBridge Plugin (current version: '+_WinUAEVersion+')');
             page_option.values[Counter-1] := true;
         end Else WinUAEFolder:='';
    End;

    if (length(AmigaForeverFolder)>0) Then
    Begin
         GetVersionNumbersString(AmigaForeverFolder + 'FloppyBridge_x64.dll', _AmigaForeverVersion);
         StrToVersion(_AmigaForeverVersion, AmigaForeverVersion);
         if (ComparePackedVersion(AmigaForeverVersion, FloppyBridgeVersion)<0) then
         begin
             AmigaForeverIndex:=Counter; Inc(Counter);
             If AmigaForeverVersion<1 Then 
                page_option.Add('Install FloppyBridge for Amiga Forever')
             Else page_option.Add('Update Amiga Forever FloppyBridge Plugin (current version: '+_AmigaForeverVersion+')');
             page_option.values[Counter-1] := true;
         end Else AmigaForeverFolder:='';
    End;

    If (Counter=0) Then page_option.Free;
    CloseApplication;
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if CurPageID=wpInstalling then
      Confirm := not CancelWithoutPrompt;
end;

procedure InstallDokan;
var dokanSys,installDokanVersion:string;
    installedVersion,newVersion:Int64;
    Res:Integer;
    _installedVersion,_newVersion:String;
begin
     dokanSys:=ExpandConstant('{sys}\drivers\dokan2.sys');
     installDokanVersion:=ExpandConstant('{app}\dokansetup.exe');
     if (FileExists(dokanSys)) Then
     Begin
         GetVersionNumbersString(dokanSys, _installedVersion);
         GetVersionNumbersString(installDokanVersion, _newVersion);
         StrToVersion(_installedVersion, installedVersion);
         StrToVersion(_newVersion, newVersion);         

         Res:=ComparePackedVersion(installedVersion, newVersion);
         if (Res>=0) Then Exit;
         if (MsgBox('DiskFlashback uses Dokan to provide a virtual file system.'#13#10'The version you have installed is too old (V'+_installedVersion+').'#13#10#13#10'Please uninstall that version of "Donan Library" from your system first, restart, and then try again.'#13#10#13#10'Open Installed Apps control panel?', mbConfirmation, MB_YESNO) = IDYES) Then
             ShellExecAsOriginalUser('open','ms-settings:appsfeatures','','',SW_SHOW,ewNoWait,res);         
         CancelWithoutPrompt := true;
         WizardForm.Close;
         Exit;
     End;
     Exec(installDokanVersion,'/install','',SW_SHOW,ewWaitUntilTerminated,res);    
     // 1603 is previous version isnt rebooted yet
     if (res=1603) or (res>0) then
     Begin
          CancelWithoutPrompt := true;
          WizardForm.Close;
          Exit;
     End;  
end;

Procedure InstallPluginTo(TargetFolder:String);
Begin
     FileCopy(ExpandConstant('{app}\FloppyBridge.dll'),TargetFolder+'FloppyBridge.dll', false); 
     FileCopy(ExpandConstant('{app}\FloppyBridge_x64.dll'),TargetFolder+'FloppyBridge_x64.dll', false); 
End;

Procedure CopyFloppyBridge;
Begin
     If AmigaForeverIndex>=0 Then
          if (page_option.Values[AmigaForeverIndex]) Then
              InstallPluginTo(AmigaForeverFolder);

     If WinUAEIndex>=0 Then
          if (page_option.Values[WinUAEIndex]) Then
              InstallPluginTo(WinUAEFolder);
End;

procedure CurStepChanged(CurStep: TSetupStep);
var ResultCode: Integer;
begin
  if CurStep = ssPostInstall Then CopyFloppyBridge;
  if CurStep = ssDone then ExecAsOriginalUser (ExpandConstant('{app}\{#MyAppName}'), '', '', SW_SHOW, ewNoWait, ResultCode);
end;