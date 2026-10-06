#ifndef DistDir
  #define DistDir "..\out\build\x64-Release\bin\Release"
#endif

#define MyAppName "KeyboardMethod"
#define MyAppPublisher "bsgy"
#define MyAppVersion "1.1.1"
#define MyAppId "{{FC452B85-19F4-47E5-AD40-FD523298A8C7}"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\out\installer
OutputBaseFilename=KeyboardMethod-Setup
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\resources\icon.ico
UninstallDisplayIcon={app}\icon.ico
UninstallDisplayName={#MyAppName}
CloseApplications=no
RestartIfNeededByRun=no
SetupLogging=yes

[Languages]
Name: "chinesesimp"; MessagesFile: "Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
; 许可页顶部补充一行中文说明，正文仍为 GPL-3.0 官方英文原文
chinesesimp.LicenseLabel3=本软件是基于 GPL-3.0 协议开源的免费软件。%n请仔细阅读下列许可协议。在继续安装前您必须同意这些协议条款。
english.LicenseLabel3=This is free software released under the GPL-3.0 license.%nPlease read the following License Agreement. You must accept the terms of this agreement before continuing with the installation.

[Files]
; uninsrestartdelete: 卸载时 DLL 若仍被进程加载，排队到重启后删除，卸载程序会自动提示重启
Source: "{#DistDir}\KeyboardMethod.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#DistDir}\imeinst.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\resources\icon.ico"; DestDir: "{app}"; Flags: ignoreversion; AfterInstall: CopyCustomIcon

; 注意: 没有 [Run] 段。register -> install-tip -> install-tip-default 全部在 [Code] 的
; CurStepChanged(ssPostInstall) 中按顺序执行并检查退出码。
; ([Run] 会在 ssPostInstall 之前执行，放在那里会导致 install-tip 先于 register 运行。)

[UninstallRun]
; 卸载程序以提权后的账号运行，[UninstallRun] 不支持 runasoriginaluser：
; uninstall-tip 只能清理"运行卸载的这个账号"的语言列表(以及 .Default)。
; 若当初是用另一个管理员账号代装，原用户语言列表中的条目需由该用户在系统设置中自行移除；
; unregister 之后该 TIP 已不再注册，不会再被加载。
; uninstall-tip 传 0：由 imeinst/DLL 在卸载时从 HKLM\Software\bsgy\KeyboardMethod 读取安装时保存的 LangId
; (读不到时回退 0804)，而不是在卸载程序启动时由 [Code] 预先读取。
Filename: "{app}\imeinst.exe"; Parameters: "uninstall-tip 0"; Flags: waituntilterminated runhidden; RunOnceId: "UninstallTip"
Filename: "{app}\imeinst.exe"; Parameters: "unregister"; Flags: waituntilterminated runhidden; RunOnceId: "UnregisterTip"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
var
  OptionsPage: TWizardPage;
  NameEdit: TNewEdit;
  LangCombo: TNewComboBox;
  AddTipCheck: TNewCheckBox;
  AddTipDefaultCheck: TNewCheckBox;
  IconEdit: TNewEdit;
  HintLabel: TNewStaticText;
  LangIds: array of Integer;
  // 安装后步骤失败时的自定义退出码(0 = 成功)，通过 GetCustomSetupExitCode 返回
  PostInstallExitCode: Integer;

// 安全解析十六进制字符串,失败返回 False 而非抛异常(避免注册表脏数据打崩选项页)
function SafeStrToInt(const S: String; var Value: Integer): Boolean;
var
  I: Integer;
begin
  Result := False;
  Value := 0;
  if Length(S) = 0 then
    Exit;
  for I := 1 to Length(S) do
  begin
    case S[I] of
      '0'..'9', 'a'..'f', 'A'..'F': ;
    else
      Exit;
    end;
  end;
  try
    Value := StrToInt('$' + S);
    Result := True;
  except
    Result := False;
  end;
end;

function GetCmd(const Key, Default: String): String;
var
  I: Integer;
  S, Prefix: String;
begin
  Result := Default;
  Prefix := '/' + Key + '=';
  for I := 1 to ParamCount do
  begin
    S := ParamStr(I);
    if CompareText(Copy(S, 1, Length(Prefix)), Prefix) = 0 then
    begin
      Result := Copy(S, Length(Prefix) + 1, MaxInt);
      Exit;
    end;
  end;
end;

function HasCmd(const Key: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/' + Key) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function LangCaption(Id: Integer; const Tag: String): String;
begin
  case Id of
    $0804: Result := '中文（简体，中国）';
    $0404: Result := '中文（繁体，台湾）';
    $0C04: Result := '中文（繁体，香港）';
    $0409: Result := '英语（美国）';
    $0809: Result := '英语（英国）';
    $0411: Result := '日语';
    $0412: Result := '韩语';
    $0407: Result := '德语';
    $040C: Result := '法语';
    $0410: Result := '意大利语';
    $0C0A: Result := '西班牙语';
    $0419: Result := '俄语';
    $0416: Result := '葡萄牙语（巴西）';
    $0816: Result := '葡萄牙语';
  else
    if Tag <> '' then
      Result := Tag
    else
      Result := Format('%04x', [Id]);
  end;
end;

function LocaleNameToLCID(LocaleName: String; dwFlags: DWORD): DWORD;
  external 'LocaleNameToLCID@kernel32.dll stdcall';

function Pad8(I: Integer): String;
begin
  Result := IntToStr(I);
  while Length(Result) < 8 do
    Result := '0' + Result;
end;

procedure AddLangId(Id: Integer; const Tag: String);
var
  I: Integer;
begin
  if Id = 0 then
    Exit;
  if (Id shr 10) = 0 then
    Exit;
  for I := 0 to GetArrayLength(LangIds) - 1 do
    if LangIds[I] = Id then
      Exit;
  I := GetArrayLength(LangIds);
  SetArrayLength(LangIds, I + 1);
  LangIds[I] := Id;
  LangCombo.Items.Add(LangCaption(Id, Tag));
end;

procedure FillLanguageCombo;
var
  I, DefaultIndex, LangId: Integer;
  Data: String;
  Names: TArrayOfString;
  Lcid: DWORD;
begin
  LangCombo.Items.Clear;
  SetArrayLength(LangIds, 0);

  I := 0;
  while I <= 31 do
  begin
    if not RegQueryStringValue(HKCU, 'Software\Microsoft\CTF\SortOrder\Language', Pad8(I), Data) then
      Break;
    if SafeStrToInt(Data, LangId) then
      AddLangId(LangId, '');
    I := I + 1;
  end;

  I := 1;
  while I <= 32 do
  begin
    if not RegQueryStringValue(HKCU, 'Keyboard Layout\Preload', IntToStr(I), Data) then
      Break;
    if SafeStrToInt(Data, LangId) then
      AddLangId(LangId, '');
    I := I + 1;
  end;

  if RegGetSubkeyNames(HKCU, 'Control Panel\International\User Profile', Names) then
  begin
    for I := 0 to GetArrayLength(Names) - 1 do
    begin
      if (CompareText(Names[I], 'Languages') = 0) or (CompareText(Names[I], 'UserLocale') = 0) then
        Continue;
      Lcid := LocaleNameToLCID(Names[I], 0);
      if Lcid = 0 then
        Lcid := LocaleNameToLCID(Names[I], $08000000);
      AddLangId(Integer(Lcid and $FFFF), Names[I]);
    end;
  end;

  DefaultIndex := 0;
  for I := 0 to GetArrayLength(LangIds) - 1 do
  begin
    if LangIds[I] = $0804 then
    begin
      DefaultIndex := I;
      Break;
    end;
  end;
  if LangCombo.Items.Count > 0 then
    LangCombo.ItemIndex := DefaultIndex;
end;

procedure BrowseIconClick(Sender: TObject);
var
  FileName: String;
begin
  FileName := IconEdit.Text;
  if GetOpenFileName('选择图标', FileName, '', '图标文件 (*.ico)|*.ico|所有文件 (*.*)|*.*', 'ico') then
    IconEdit.Text := FileName;
end;

procedure InitializeWizard;
var
  NameLabel, LangLabel, IconLabel: TNewStaticText;
  BrowseButton: TNewButton;
  I: Integer;
  CmdName, CmdLang, CmdIcon: String;
  CmdLangId: Integer;
  CmdLangFound: Boolean;
begin
  OptionsPage := CreateCustomPage(wpSelectDir, '输入法选项', '设置显示名、目标语言和图标。');

  NameLabel := TNewStaticText.Create(OptionsPage);
  NameLabel.Parent := OptionsPage.Surface;
  NameLabel.Caption := '显示名（必填）';
  NameLabel.Top := 0;

  NameEdit := TNewEdit.Create(OptionsPage);
  NameEdit.Parent := OptionsPage.Surface;
  NameEdit.Top := NameLabel.Top + NameLabel.Height + 4;
  NameEdit.Width := OptionsPage.SurfaceWidth;
  NameEdit.Text := '{#MyAppName}';

  LangLabel := TNewStaticText.Create(OptionsPage);
  LangLabel.Parent := OptionsPage.Surface;
  LangLabel.Caption := '目标语言（必选，仅列出系统已添加的语言）';
  LangLabel.Top := NameEdit.Top + NameEdit.Height + 12;

  LangCombo := TNewComboBox.Create(OptionsPage);
  LangCombo.Parent := OptionsPage.Surface;
  LangCombo.Top := LangLabel.Top + LangLabel.Height + 4;
  LangCombo.Width := OptionsPage.SurfaceWidth;
  LangCombo.Style := csDropDownList;

  AddTipCheck := TNewCheckBox.Create(OptionsPage);
  AddTipCheck.Parent := OptionsPage.Surface;
  AddTipCheck.Top := LangCombo.Top + LangCombo.Height + 12;
  AddTipCheck.Width := OptionsPage.SurfaceWidth;
  AddTipCheck.Caption := '加入当前用户语言列表（不自动切换到本输入法）';
  AddTipCheck.Checked := True;

  AddTipDefaultCheck := TNewCheckBox.Create(OptionsPage);
  AddTipDefaultCheck.Parent := OptionsPage.Surface;
  AddTipDefaultCheck.Top := AddTipCheck.Top + AddTipCheck.Height + 8;
  AddTipDefaultCheck.Width := OptionsPage.SurfaceWidth;
  AddTipDefaultCheck.Caption := '登录/锁屏界面可用（也会影响新创建用户的默认语言列表）';
  AddTipDefaultCheck.Checked := False;

  IconLabel := TNewStaticText.Create(OptionsPage);
  IconLabel.Parent := OptionsPage.Surface;
  IconLabel.Caption := '自定义图标（可选，.ico；留空使用默认）';
  IconLabel.Top := AddTipDefaultCheck.Top + AddTipDefaultCheck.Height + 12;

  IconEdit := TNewEdit.Create(OptionsPage);
  IconEdit.Parent := OptionsPage.Surface;
  IconEdit.Top := IconLabel.Top + IconLabel.Height + 4;
  IconEdit.Width := OptionsPage.SurfaceWidth - 88;

  BrowseButton := TNewButton.Create(OptionsPage);
  BrowseButton.Parent := OptionsPage.Surface;
  BrowseButton.Caption := '浏览...';
  BrowseButton.Top := IconEdit.Top - 1;
  BrowseButton.Left := IconEdit.Left + IconEdit.Width + 8;
  BrowseButton.Width := 80;
  BrowseButton.OnClick := @BrowseIconClick;

  HintLabel := TNewStaticText.Create(OptionsPage);
  HintLabel.Parent := OptionsPage.Surface;
  HintLabel.Caption := '按下 Win + 空格 即可在多输入法之间切换。如果您对<多输入法>尚不了解，请咨询您常用的 AI 助手';
  HintLabel.Top := IconEdit.Top + IconEdit.Height + 16;
  HintLabel.Width := OptionsPage.SurfaceWidth;
  HintLabel.WordWrap := True;

  FillLanguageCombo;

  // 静默安装时若语言列表为空,不能默默用 0804 装——退出码非 0 让调用方知道失败
  if (GetArrayLength(LangIds) = 0) and WizardSilent then
  begin
    Log('静默安装失败: 系统语言列表为空,无法选择目标语言');
    Abort;
  end;

  CmdName := GetCmd('NAME', '');
  if CmdName <> '' then
    NameEdit.Text := CmdName;
  CmdLang := GetCmd('LANGID', '');
  if CmdLang <> '' then
  begin
    CmdLangFound := False;
    if SafeStrToInt(CmdLang, CmdLangId) then
    begin
      for I := 0 to GetArrayLength(LangIds) - 1 do
        if LangIds[I] = CmdLangId then
        begin
          LangCombo.ItemIndex := I;
          CmdLangFound := True;
        end;
    end;
    if not CmdLangFound then
    begin
      Log('/LANGID=' + CmdLang + ' 无效或不在系统已安装语言列表中');
      if WizardSilent then
      begin
        // 静默安装不能悄悄换成别的语言：直接退出，退出码非 0
        Abort;
      end
      else
        MsgBox('命令行参数 /LANGID=' + CmdLang + ' 无效，或该语言未在系统中添加。' + #13#10 +
               '请在下一页的下拉框中手动选择目标语言。', mbInformation, MB_OK);
    end;
  end;
  CmdIcon := GetCmd('ICON', '');
  if CmdIcon <> '' then
    IconEdit.Text := CmdIcon;
  if HasCmd('NOTIP') then
    AddTipCheck.Checked := False;
  if HasCmd('NOTIPDEFAULT') then
    AddTipDefaultCheck.Checked := False;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Name: String;
begin
  Result := True;
  if CurPageID <> OptionsPage.ID then
    Exit;
  Name := Trim(NameEdit.Text);
  if Name = '' then
  begin
    MsgBox('请填写显示名。', mbError, MB_OK);
    Result := False;
    Exit;
  end;
  if Pos('"', Name) > 0 then
  begin
    MsgBox('显示名不能包含引号。', mbError, MB_OK);
    Result := False;
    Exit;
  end;
  if Length(Name) > 64 then
  begin
    MsgBox('显示名过长。', mbError, MB_OK);
    Result := False;
    Exit;
  end;
  if LangCombo.ItemIndex < 0 then
  begin
    MsgBox('请选择目标语言。若列表为空，请先在 Windows 设置中添加语言。', mbError, MB_OK);
    Result := False;
    Exit;
  end;
  if (Trim(IconEdit.Text) <> '') and (not FileExists(Trim(IconEdit.Text))) then
  begin
    MsgBox('找不到所选图标文件。', mbError, MB_OK);
    Result := False;
  end;
end;

function GetDisplayName(Value: String): String;
begin
  Result := Trim(NameEdit.Text);
  if Result = '' then
    Result := '{#MyAppName}';
end;

function GetLangHex(Value: String): String;
begin
  if (LangCombo.ItemIndex >= 0) and (LangCombo.ItemIndex < GetArrayLength(LangIds)) then
    Result := Format('%04x', [LangIds[LangCombo.ItemIndex]])
  else
    Result := '0804';
end;

function ShouldInstallTip: Boolean;
begin
  Result := AddTipCheck.Checked;
end;

function ShouldInstallTipDefault: Boolean;
begin
  Result := AddTipDefaultCheck.Checked;
end;

// 运行 imeinst.exe 并返回其退出码；无法启动时返回 -1。
// AsOriginalUser=True 时以未提权的原用户身份运行(用于修改原用户自己的语言列表)。
function RunImeInst(const Params, Status: String; AsOriginalUser: Boolean): Integer;
var
  ResultCode: Integer;
  Started: Boolean;
begin
  WizardForm.StatusLabel.Caption := Status;
  Log('imeinst ' + Params);
  if AsOriginalUser then
    Started := ExecAsOriginalUser(ExpandConstant('{app}\imeinst.exe'), Params, '', SW_HIDE,
                                  ewWaitUntilTerminated, ResultCode)
  else
    Started := Exec(ExpandConstant('{app}\imeinst.exe'), Params, '', SW_HIDE,
                    ewWaitUntilTerminated, ResultCode);
  if not Started then
    Result := -1
  else
    Result := ResultCode;
  Log('imeinst 退出码: ' + IntToStr(Result));
end;

// 安装后步骤：register -> install-tip -> install-tip-default，严格按顺序，逐一检查退出码。
// 此时文件已复制、卸载信息已定稿，失败无法回滚(Abort 在 ssPostInstall 不生效)，
// 所以只如实报告错误，并让 Setup 以非 0 退出码结束。
procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
  LangHex: String;
begin
  if CurStep <> ssPostInstall then
    Exit;

  PostInstallExitCode := 0;
  LangHex := GetLangHex('');

  Code := RunImeInst('register "' + GetDisplayName('') + '" ' + LangHex + ' 0',
                     '正在注册输入法...', False);
  if Code <> 0 then
  begin
    PostInstallExitCode := 10;
    SuppressibleMsgBox('输入法注册失败 (imeinst 退出码 ' + IntToStr(Code) + ')。' + #13#10 +
                       '可能的原因: DLL 缺失、COM 注册被拒、目标语言无效。' + #13#10#13#10 +
                       '文件已复制但输入法不可用。请查看安装日志，修复后重新运行安装程序，' +
                       '或在"应用"中卸载本程序。', mbError, MB_OK, IDOK);
    Exit;  // 未注册时加入语言列表没有意义
  end;

  if ShouldInstallTip then
  begin
    Code := RunImeInst('install-tip ' + LangHex, '正在加入语言列表...', True);
    if Code <> 0 then
    begin
      PostInstallExitCode := 11;
      SuppressibleMsgBox('输入法已注册，但加入当前用户语言列表失败 (退出码 ' + IntToStr(Code) + ')。' + #13#10 +
                         '可以在 Windows 设置 -> 时间和语言 -> 语言 中手动添加该输入法。',
                         mbError, MB_OK, IDOK);
    end;
  end;

  if ShouldInstallTipDefault then
  begin
    Code := RunImeInst('install-tip-default ' + LangHex, '正在为锁屏和登录界面启用输入法...', False);
    if Code <> 0 then
    begin
      if PostInstallExitCode = 0 then
        PostInstallExitCode := 12;
      SuppressibleMsgBox('输入法已注册，但为登录/锁屏界面启用失败 (退出码 ' + IntToStr(Code) + ')。' + #13#10 +
                         '这不影响当前用户使用。', mbError, MB_OK, IDOK);
    end;
  end;
end;

// 安装后步骤失败时让 Setup 返回非 0 退出码，便于静默安装的调用方判断。
function GetCustomSetupExitCode: Integer;
begin
  Result := PostInstallExitCode;
end;

procedure CopyCustomIcon;
var
  Src, Dest: String;
begin
  Src := Trim(IconEdit.Text);
  if (Src <> '') and FileExists(Src) then
  begin
    Dest := ExpandConstant('{app}\icon.ico');
    CopyFile(Src, Dest, False);
  end;
end;

// ---- 卸载前检测 DLL 是否被占用 ----
// 卸载程序是 32 位进程，句柄按 Longint 声明即可；失败值 INVALID_HANDLE_VALUE = -1。
function CreateFile(lpFileName: String; dwDesiredAccess, dwShareMode: DWORD;
  lpSecurityAttributes: Cardinal; dwCreationDisposition, dwFlagsAndAttributes: DWORD;
  hTemplateFile: Cardinal): Longint;
  external 'CreateFileW@kernel32.dll stdcall';
function CloseHandle(hObject: Longint): BOOL;
  external 'CloseHandle@kernel32.dll stdcall';

const
  KM_GENERIC_WRITE = $40000000;
  KM_FILE_SHARE_READ = $00000001;
  KM_OPEN_EXISTING = 3;
  KM_INVALID_HANDLE = -1;
  KM_ERROR_SHARING_VIOLATION = 32;

// 尝试以写权限打开 DLL，若被进程加载则会因共享冲突失败
function IsDllInUse(const DllPath: String): Boolean;
var
  hFile: Longint;
begin
  Result := False;
  if not FileExists(DllPath) then
    Exit;
  hFile := CreateFile(DllPath, KM_GENERIC_WRITE, KM_FILE_SHARE_READ, 0, KM_OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL, 0);
  if hFile = KM_INVALID_HANDLE then
  begin
    // DLLGetLastError 返回上一次 external 调用后的 GetLastError，比再 external 一次 GetLastError 可靠
    if DLLGetLastError = KM_ERROR_SHARING_VIOLATION then
      Result := True;
  end
  else
    CloseHandle(hFile);
end;

function InitializeUninstall(): Boolean;
var
  DllPath: String;
begin
  Result := True;
  DllPath := ExpandConstant('{app}\KeyboardMethod.dll');
  if IsDllInUse(DllPath) then
  begin
    if UninstallSilent then
    begin
      // 静默卸载不提问：继续卸载，被占用的 DLL 由 uninsrestartdelete 排队到重启后删除
      Log('KeyboardMethod.dll 正在被占用，静默卸载继续，DLL 将在重启后删除');
      Exit;
    end;
    // SuppressibleMsgBox: 静默卸载(/SUPPRESSMSGBOXES)时默认按"是"继续，不会卡住
    if SuppressibleMsgBox('检测到 KeyboardMethod.dll 正在被某些程序占用（可能正在使用该输入法）。' + #13#10#13#10 +
              '建议关闭所有正在使用本输入法的程序后再继续，' + #13#10 +
              '否则 DLL 会在重启后才被删除，这些程序在退出前会继续使用旧版本。' + #13#10#13#10 +
              '是否继续卸载？', mbConfirmation, MB_YESNO, IDYES) <> IDYES then
    begin
      Result := False;
    end;
  end;
end;
