# AlwaysEnglish-IME

AlwaysEnglish-IME is an "always English" Text Services Framework (TSF) text input processor for Windows 10/11 x64. It shows up as an input method under the language you choose, but it does not intercept keystrokes: keys go straight to Windows and the application, so you always type English. It makes no network connections, has no telemetry, and runs no background process. Licensed under GPL-3.0.

AlwaysEnglish-IME（早期版本名为 KeyboardMethod）是一个面向 Windows 10/11 x64 的 Text Services Framework（TSF）文本输入处理器（TIP）。它以输入法 Profile 的形式挂载到用户选择的语言下，但不实现拼音、组字、候选、按键转换或输入法 UI；启用后，按键继续由 Windows 和应用按美式 QWERTY 键盘处理。

它解决的问题是：在中文或其他指定语言下保留一个输入法入口，同时始终直接输入英文字符。它不是自定义键盘布局，也不是传统 IMM32 `.ime`。

产品需求和验收口径见 [伪输入法-永远英文TIP-需求说明.md](./伪输入法-永远英文TIP-需求说明.md)。本文记录当前代码的实现、构建和维护约束；两者不一致时，应先确认需求，再修改实现和本文。

## 安全与隐私

- 不注册 `ITfKeyEventSink`，也不使用 `SetWindowsHookEx` 等键盘钩子；按键直接由 Windows 和当前应用处理，本输入法既不截获也不记录按键内容。
- 不联网、无遥测，不收集或上传任何数据。
- 没有常驻进程、后台服务或计划任务；DLL 只在启用了本输入法的进程内由 TSF 按需加载。
- 它是系统级输入法 DLL：安装和卸载需要管理员权限，会写入 `HKLM`（COM 注册、TSF Profile 与类别、安装状态）并修改语言列表（当前用户，可选 `.Default`）。未签名的输入法 DLL 和安装包可能被部分杀毒软件误报。

## 当前行为

- 注册为进程内 COM Server 和 TSF 键盘 TIP。
- 安装时可设置显示名、目标语言和图标。
- Profile 不设置替代键盘布局（`hklSubstitute = 0`）；按键由当前线程的键盘布局直接产生字符，US QWERTY 依赖用户该语言下的键盘布局本身是美式键盘。
- 不注册 `ITfKeyEventSink`，不截获键盘事件。
- 不创建 Composition、Candidate UI 或 Edit Session。
- 激活、窗口重新获焦或相关状态变化时，尝试维持：IME 关闭、字母数字转换、无句子转换。
- 输入法切换后的前 300 ms 不写 compartment，避免干扰上一输入法的退出清理。
- 使用 Windows 自带语言栏或 `Win+Space` 切换，不注册全局快捷键。
- 不联网、无遥测、无常驻进程、无配置文件。

这里的“直出”来自“不处理按键”，不是把按键拦下后重新插入字符。因此不要为了增加“英文输入”而引入 Key Event Sink 或字符 Edit Session；那会改变当前设计并增加兼容性风险。

## 运行结构

```text
Windows / 应用
    |
    | 激活 TSF Profile
    v
AlwaysEnglishIME.dll（进程内 COM DLL）
    |
    +-- CClassFactory：创建 CTextService，维护 DLL 引用计数
    |
    +-- CTextService：监听线程、焦点和 compartment 变化
    |       |
    |       +-- OPENCLOSE = 0
    |       +-- CONVERSION = IME_CMODE_ALPHANUMERIC
    |       +-- SENTENCE = IME_SMODE_NONE
    |
    +-- Register：注册 COM、TSF Profile、TIP 类别及语言列表

键盘事件 --------------------------> Windows / 当前应用直接处理
```

`CTextService` 实现以下接口：

- `ITfTextInputProcessorEx`：负责激活和停用。
- `ITfThreadMgrEventSink`：在文档焦点变化时恢复英文状态。
- `ITfThreadFocusSink`：在线程重新获得焦点时恢复英文状态。
- `ITfCompartmentEventSink`：相关状态被外部修改时尝试恢复。

## 代码地图

| 路径 | 职责 |
|---|---|
| `src/dllmain.cpp` | DLL 生命周期、四个标准 COM 导出入口 |
| `src/ClassFactory.cpp` | COM Class Factory 和 DLL 锁计数 |
| `src/TextService.cpp` | TSF 激活、事件订阅、永远英文状态维护 |
| `src/Compartment.cpp` | TSF compartment 的读写和事件订阅辅助函数 |
| `src/Register.cpp` | COM/TSF 注册、语言列表安装卸载、安装状态持久化 |
| `src/Globals.cpp` | 模块句柄、全局引用计数、图标路径辅助函数 |
| `src/imeinst.cpp` | 安装器调用 DLL 导出函数的命令行桥接程序 |
| `AlwaysEnglishIME.def.in` | DLL 导出函数列表 |
| `resources/AlwaysEnglishIME.rc` | DLL 版本信息和内嵌图标 |
| `installer/AlwaysEnglishIME.iss` | Inno Setup 安装界面和安装/卸载流程 |
| `build.bat` | CMake Release x64 构建并调用安装器构建 |
| `scripts/Remove-DefaultUserTip.ps1` | 手动清理 `.Default` Profile 中本输入法残留的独立脚本 |

## 注册与安装流程

安装包需要管理员权限，默认安装到 `Program Files\AlwaysEnglish-IME`（从 KeyboardMethod 升级时也使用新目录，见下文“从 KeyboardMethod 升级”）。安装选项页允许用户：

1. 输入显示名，默认 `AlwaysEnglish-IME`，最多 64 个字符且不能包含双引号。
2. 从系统当前已添加的语言中选择目标语言；存在简体中文 `0x0804` 时默认选中。
3. 选择是否加入当前用户语言列表，默认勾选。
4. 可选一个 `.ico` 文件覆盖默认的 sidecar 图标。

安装时的调用顺序：

1. 提权后的安装器复制 `AlwaysEnglishIME.dll`、`imeinst.exe` 和 `icon.ico`。
2. `imeinst register <显示名> <LANGID> 0` 注册 COM、TIP 类别和 Profile，并把显示名和 LANGID 保存到 `HKLM\Software\bsgy\AlwaysEnglishIME`。注册成功后，安装器删除安装目录中旧版本遗留的 `KeyboardMethod.dll`（如有）。
3. 若勾选加入语言列表，安装器以原始非提权用户运行 `imeinst install-tip <LANGID>`，修改当前用户语言列表。
4. 若勾选“登录/锁屏界面可用”（默认不勾，`/NOTIPDEFAULT` 可强制关闭），安装器提权运行 `imeinst install-tip-default <LANGID>`，把 TIP 加入 `.Default` Profile（同时影响新建用户的默认语言列表）。

以上三步都在安装脚本 `[Code]` 的 `CurStepChanged(ssPostInstall)` 中按顺序执行并检查退出码。此时已无法回滚：任一步失败会弹出错误提示（`/SUPPRESSMSGBOXES` 可抑制），Setup 以非 0 退出码结束（10 注册失败、11 加入当前用户列表失败、12 `.Default` 失败）。静默安装时若系统语言列表为空或 `/LANGID=` 无效，安装直接中止。

卸载时先从运行卸载的账号和 `.Default` 语言列表移除 TIP，再注销 Profile、类别和 COM Server，最后删除安装目录（包括从旧版本升级后残留的 `KeyboardMethod.dll`）；DLL 若仍被进程占用，会排队到重启后删除并提示重启。卸载程序以提权账号运行，若当初由另一个管理员账号代装，原用户语言列表中的条目需由该用户在系统设置中自行移除（TIP 已注销，不会再被加载）。旧测试版本使用过占位 CLSID/Profile GUID，注册和卸载流程会尽力清理这些旧项。

若覆盖安装时取消了“登录/锁屏界面可用”，或卸载后登录/锁屏界面仍出现本输入法，可用独立脚本清理 `.Default` Profile 中的残留（安装器不会自动做这一步）：

```powershell
# 只列出将要删除的条目（默认，不修改任何内容，无需管理员）
powershell -ExecutionPolicy Bypass -File scripts\Remove-DefaultUserTip.ps1
# 实际删除（需在管理员 PowerShell 中运行；可加 -WhatIf 预览）
powershell -ExecutionPolicy Bypass -File scripts\Remove-DefaultUserTip.ps1 -Apply
```

脚本先调用 `InstallLayoutOrTip(ILOT_UNINSTALL | ILOT_DEFUSER4)`，再按本输入法的 CLSID/Profile GUID（含旧测试 GUID）清理 `HKEY_USERS\.DEFAULT` 下剩余的 `User Profile`、`CTF\SortOrder\AssemblyItem`、`CTF\Assemblies`、`CTF\TIP` 条目，不会触碰其他输入法。

`InstallLayoutOrTip` 通过 `input.dll` 动态调用。当前用户操作必须由原始用户执行，不能只在提权后的安装进程里执行，否则会修改错误的用户上下文。

## 从 KeyboardMethod 升级

1.1.x 及更早版本名为 KeyboardMethod（DLL 为 `KeyboardMethod.dll`，安装状态保存在 `HKLM\Software\bsgy\KeyboardMethod`）。新版本沿用相同的安装器 AppId、Text Service CLSID 和 Profile GUID，可以直接覆盖安装，无需先卸载：

- 系统中仍是同一个输入法条目，不会出现两个输入法。显示名和目标语言以本次安装选项页为准：安装器不会读取上一次的设置，默认显示名为 `AlwaysEnglish-IME`、目标语言默认选中简体中文；如需保留原显示名或原语言，请在选项页中手动填写或选择。
- 注册成功后删除旧的 `KeyboardMethod.dll`；若它仍被正在使用该输入法的程序占用，会在下次重启后删除。在此之前这些程序继续使用旧 DLL，新启动的程序使用新 DLL。
- 安装状态迁移到 `HKLM\Software\bsgy\AlwaysEnglishIME`：读取时新键优先、找不到再读旧键；注册成功后删除旧键，卸载时新旧两个键都会清理。
- 全新安装与升级均使用 `Program Files\AlwaysEnglish-IME`。旧目录 `Program Files\KeyboardMethod` 中的残留（含 `KeyboardMethod.dll`、旧卸载程序等）在注册成功后会被清理；若 `KeyboardMethod.dll` 仍被进程占用，则排队到下次重启后删除，安装器会提示重启。
## 固定标识与注册信息

以下标识位于 `include/Globals.h`，发布后不能随意修改，否则 Windows 会将新版本识别为另一个输入法，升级和卸载也可能留下旧项：

- Text Service CLSID：`{FC452B85-19F4-47E5-AD40-FD523298A8C7}`
- Profile GUID：`{0BF7DD25-41DC-400F-887D-0A80AC37F062}`
- 默认调试 LANGID：`0x0804`（中文，简体，中国）
- COM ThreadingModel：`Apartment`
- 安装器 AppId：`{FC452B85-19F4-47E5-AD40-FD523298A8C7}`（位于 `installer/AlwaysEnglishIME.iss`，与 CLSID 相同；从 KeyboardMethod 改名后保持不变）

主要注册内容包括：

- `HKCR\CLSID\{CLSID}`：进程内 COM Server 路径和线程模型。
- TSF Profile：显示名、目标 LANGID、Profile GUID 和图标。
- TSF Categories：键盘 TIP、input-mode compartment、COM-less 和 immersive support。
- `HKLM\Software\bsgy\AlwaysEnglishIME`：安装所用的显示名与 LANGID，供升级和卸载读取。旧版本使用的 `HKLM\Software\bsgy\KeyboardMethod` 仅作为读取回落，注册成功后删除，卸载时一并清理。

## 构建

依赖：

- Windows 10/11 x64
- Visual Studio 2022，包含 MSVC、Windows SDK 和 C++ CMake Tools
- CMake 3.20 或更高版本
- Inno Setup 6（仅生成安装包时需要）

完整构建：

```bat
build.bat
```

手动构建：

```bat
cmake -S . -B out/build/x64-Release -G "Visual Studio 17 2022" -A x64
cmake --build out/build/x64-Release --config Release
installer\build.bat
```

`build.bat` 会优先查找 PATH 中的 CMake，也会查找 Visual Studio 自带的 CMake。主要产物：

```text
out/build/x64-Release/bin/Release/AlwaysEnglishIME.dll
out/build/x64-Release/bin/Release/imeinst.exe
out/installer/AlwaysEnglish-IME-Setup.exe
```

Release 配置启用了 `/GS`、`/sdl`、CFG、ASLR、NX 和高熵 VA。正式分发前仍需对 DLL 和安装包进行可信 Authenticode 签名。

## 调试与辅助命令

`imeinst.exe` 必须与 `AlwaysEnglishIME.dll` 放在同一目录。它支持：

```text
imeinst register <显示名> <十六进制 LANGID> <是否立即加入列表: 0|1>
imeinst unregister
imeinst install-tip <十六进制 LANGID>
imeinst install-tip-default <十六进制 LANGID>
imeinst uninstall-tip <十六进制 LANGID，0 表示使用已保存的安装 LANGID>
```

注册、注销以及 `.Default` Profile 操作需要管理员权限。`install-tip` 应在目标普通用户的上下文中运行。

以上命令由安装包内部调用。安装和卸载只通过 `AlwaysEnglish-IME-Setup.exe` 及其卸载程序进行，仓库不再提供手动注册脚本；DLL 中保留的 `DllRegisterServer`/`DllUnregisterServer` 仅为标准 COM 导出。

`scripts/Remove-DefaultUserTip.ps1` 用于清理 `.Default` Profile（登录/锁屏界面、新建用户默认列表）中本输入法的残留项，见上文“注册与安装流程”。

## 验证清单

构建验证：

- Release x64 的 DLL、`imeinst.exe` 和安装包均成功生成。
- DLL 导出 `DllCanUnloadNow`、`DllGetClassObject`、`DllRegisterServer`、`DllUnregisterServer` 及安装辅助函数。
- DLL 和安装包版本号一致；当前安装包版本为 `1.1.1`，DLL 资源版本为 `1.1.1.0`，发版前应确认两者同步。

功能验证建议分别在干净的 Windows 10 和 Windows 11 x64 虚拟机进行：

- 安装器只列出当前用户已经添加的语言。
- 自定义显示名、语言和图标正确出现在系统输入法列表/语言栏。
- 记事本、Word、Chrome/Edge 和现代应用文本框中均直接输入，无组字、候选窗或全角标点转换。
- Shift、Ctrl、Alt 等修饰键仍由应用正常接收。
- 从第三方输入法切入和切出时，对方候选窗能正常收尾。
- 锁屏/登录界面按预期出现或移除该 TIP。
- 覆盖安装到不同 LANGID 后，旧语言 Profile 不残留。
- 在已安装 KeyboardMethod 1.1.x 的系统上覆盖安装：输入法条目不重复，`KeyboardMethod.dll` 被删除（被占用时重启后删除），`HKCR\CLSID\{CLSID}\InprocServer32` 指向 `AlwaysEnglishIME.dll`，安装状态迁移到新键且旧键被删除。
- 卸载后系统设置中无失效项，安装目录和安装状态注册表项（含旧键）被清理。

## 已知边界

- 工程只生成 x64 DLL，不支持 32 位宿主进程、ARM64、Windows 7/8 或 Server Core。
- 当前没有自动化测试；正确性依赖 Release 构建和 Windows 实机/虚拟机集成验证。
- 激活和焦点回调会立即施加“关闭 + 英数”状态；只有 compartment `OnChange` 在激活后 300 ms 内不立即响应，而是在窗口结束后由线程定时器补写一次。离开本输入法时会恢复进入前的 compartment 值。修改这段逻辑前应重点回归与第三方输入法切换时的 UI 清理问题。
- `input.dll!InstallLayoutOrTip` 是运行时动态解析的 Windows 接口；失败时当前实现只返回通用错误，安装器没有细化诊断信息。
- 安装器版本（`installer/AlwaysEnglishIME.iss` 的 `MyAppVersion`）和 DLL 资源版本（`resources/AlwaysEnglishIME.rc`）需手动保持一致，发布流程尚未自动同步。

## 维护原则

- 保持实现最小：不增加按键钩子、候选 UI、输入模式、热键或后台进程，除非产品需求明确改变。
- COM 方法不得让 C++ 异常越过 ABI 边界；分配使用 `std::nothrow` 并返回 HRESULT。
- 新增事件订阅时必须在 `Deactivate` 中对称退订，失败路径也要释放接口和重置 cookie。
- 注册流程涉及机器级、当前用户和 `.Default` 三种上下文，修改时必须分别验证安装、升级和卸载。
- 正式发布前统一版本号、签名二进制，并在干净系统上验证安装与卸载残留。

## 许可证

本项目以 [GNU General Public License v3.0](LICENSE) 发布，完整条款见根目录 `LICENSE`。安装包会在安装向导中显示许可协议，并把 `LICENSE` 一并安装到程序目录。

`installer/Languages/ChineseSimplified.isl` 来自 Inno Setup 社区翻译，版权与许可归原译者所有，文件头保留了原始出处。
