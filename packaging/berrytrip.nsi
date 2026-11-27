; BerryTrip Launcher installer (per-user, no admin, no downloads).
; Build: makensis /DAPP_VERSION=0.3.0 berrytrip.nsi
Unicode true
!include "MUI2.nsh"

!ifndef APP_VERSION
  !define APP_VERSION "0.0.0"
!endif

Name "BerryTrip Launcher"
BrandingText "BerryTrip Launcher ${APP_VERSION}"
OutFile "..\logs\BerryTrip-Setup-${APP_VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\BerryTrip"
InstallDirRegKey HKCU "Software\BerryTrip" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "BerryTrip Launcher"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "FileDescription" "BerryTrip Launcher installer"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "CompanyName" "BerryTrip"
VIAddVersionKey "LegalCopyright" "GPL-3.0; based on Prism Launcher"

!define MUI_ICON "..\program_info\berrytrip.ico"
!define MUI_UNICON "..\program_info\berrytrip.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\berrytrip.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Запустить BerryTrip"
!define MUI_FINISHPAGE_LINK "Исходный код и лицензия (GPL-3.0)"
!define MUI_FINISHPAGE_LINK_LOCATION "https://github.com/evilvalue/prism-berry"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "English"

Section "BerryTrip" SEC01
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "..\dist\*"
  File "..\LICENSE"
  File "..\NOTICE.md"
  File "..\launcher\berry\resources\MinecraftRus-COPYRIGHT.txt"

  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\BerryTrip"
  CreateShortcut "$SMPROGRAMS\BerryTrip\BerryTrip.lnk" "$INSTDIR\berrytrip.exe"
  CreateShortcut "$SMPROGRAMS\BerryTrip\Удалить BerryTrip.lnk" "$INSTDIR\uninstall.exe"
  CreateShortcut "$DESKTOP\BerryTrip.lnk" "$INSTDIR\berrytrip.exe"

  WriteRegStr HKCU "Software\BerryTrip" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "DisplayName" "BerryTrip Launcher"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "Publisher" "BerryTrip"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "DisplayIcon" "$INSTDIR\berrytrip.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "URLInfoAbout" "https://github.com/evilvalue/prism-berry"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  Delete "$SMPROGRAMS\BerryTrip\BerryTrip.lnk"
  Delete "$SMPROGRAMS\BerryTrip\Удалить BerryTrip.lnk"
  RMDir "$SMPROGRAMS\BerryTrip"
  Delete "$DESKTOP\BerryTrip.lnk"

  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BerryTrip"
  DeleteRegKey HKCU "Software\BerryTrip"

  RMDir /r "$INSTDIR"
SectionEnd
