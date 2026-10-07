#include "Globals.h"
#include "Register.h"
#include <objbase.h>
#include <strsafe.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#define TEXTSERVICE_CLSID_KEY L"CLSID\\"

#ifndef ILOT_UNINSTALL
#define ILOT_UNINSTALL 0x00000001
#endif
// InstallLayoutOrTip flag: apply to the .Default user profile, which backs the
// secure desktop (logon / lock screen). Not defined in public headers.
#ifndef ILOT_DEFUSER4
#define ILOT_DEFUSER4 0x00000004
#endif

typedef BOOL (WINAPI *InstallLayoutOrTipFn)(LPCWSTR psz, DWORD dwFlags);

// Forward declarations for internal helpers (order of definition matters).
static HRESULT RegisterServer(HMODULE hModule, REFCLSID rclsid, const wchar_t *pszFriendlyName,
                             const wchar_t *pszPrefix, const wchar_t *pszThreadModel);
static HRESULT UnregisterServer(REFCLSID rclsid, const wchar_t *pszPrefix);
static HRESULT UnregisterProfiles();
static HRESULT RegisterCategories();

static BOOL CallInstallLayoutOrTip(LPCWSTR psz, DWORD dwFlags)
{
    HMODULE hInput = LoadLibraryW(L"input.dll");
    if (hInput == NULL)
    {
        return FALSE;
    }

    InstallLayoutOrTipFn pfn = reinterpret_cast<InstallLayoutOrTipFn>(
        GetProcAddress(hInput, "InstallLayoutOrTip"));
    BOOL ok = FALSE;
    if (pfn != NULL)
    {
        ok = pfn(psz, dwFlags);
    }
    FreeLibrary(hInput);
    return ok;
}

static HRESULT BuildTipLayoutString(REFCLSID clsid, REFGUID guidProfile, LANGID langid,
                                    wchar_t *pszOut, size_t cchOut)
{
    wchar_t szClsid[64] = {};
    wchar_t szProfile[64] = {};
    if (StringFromGUID2(clsid, szClsid, ARRAYSIZE(szClsid)) == 0 ||
        StringFromGUID2(guidProfile, szProfile, ARRAYSIZE(szProfile)) == 0)
    {
        return E_FAIL;
    }

    return StringCchPrintfW(pszOut, cchOut, L"0x%04X:%s%s", langid, szClsid, szProfile);
}

static HRESULT InstallOrRemoveTip(REFCLSID clsid, REFGUID guidProfile, LANGID langid, DWORD dwFlags)
{
    wchar_t szTip[128] = {};
    HRESULT hr = BuildTipLayoutString(clsid, guidProfile, langid, szTip, ARRAYSIZE(szTip));
    if (FAILED(hr))
    {
        return hr;
    }

    if (!CallInstallLayoutOrTip(szTip, dwFlags))
    {
        return E_FAIL;
    }
    return S_OK;
}

// 读取 HKLM\<pszKey> 下的 LangId；键或值不存在、类型不对时返回 0。
static LANGID ReadLangIdFromKey(LPCWSTR pszKey)
{
    HKEY hKey = NULL;
    DWORD lang = 0;
    DWORD cb = sizeof(lang);
    DWORD type = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, pszKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(hKey, L"LangId", NULL, &type, reinterpret_cast<LPBYTE>(&lang), &cb) != ERROR_SUCCESS ||
            type != REG_DWORD)
        {
            lang = 0;
        }
        RegCloseKey(hKey);
    }
    return static_cast<LANGID>(lang);
}

// 返回 HKLM 中记录的 LANGID；没有记录（全新安装）时返回 0。
// 先读当前键，读不到再读旧产品名 KeyboardMethod 的键（从 1.1.x 升级、尚未重新注册时）。
static LANGID ReadStoredLangIdRaw()
{
    LANGID lang = ReadLangIdFromKey(g_wszInstallKey);
    if (lang == 0)
    {
        lang = ReadLangIdFromKey(g_wszLegacyInstallKey);
    }
    return lang;
}

// 卸载/加入列表等需要一个具体 LANGID 的场景：没有记录时回落到默认 0x0804。
static LANGID ReadStoredLangId()
{
    LANGID lang = ReadStoredLangIdRaw();
    return lang ? lang : g_langidProfile;
}

static HRESULT StoreInstallState(LPCWSTR pszDisplayName, LANGID langid)
{
    HKEY hKey = NULL;
    LONG st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, g_wszInstallKey, 0, NULL, REG_OPTION_NON_VOLATILE,
                              KEY_WRITE, NULL, &hKey, NULL);
    if (st != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(st);
    }

    DWORD dw = langid;
    st = RegSetValueExW(hKey, L"LangId", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&dw), sizeof(dw));
    if (st == ERROR_SUCCESS && pszDisplayName != NULL)
    {
        st = RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE *>(pszDisplayName),
                            static_cast<DWORD>((wcslen(pszDisplayName) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(hKey);
    if (st == ERROR_SUCCESS)
    {
        // 新键写入成功后再删除旧产品名下的键（迁移完成）；失败时保留旧记录供卸载读取。
        SHDeleteKeyW(HKEY_LOCAL_MACHINE, g_wszLegacyInstallKey);
    }
    return HRESULT_FROM_WIN32(st);
}

static void DeleteInstallState()
{
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, g_wszInstallKey);
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, g_wszLegacyInstallKey);
}

static HRESULT UnregisterCategoriesFor(REFCLSID clsid)
{
    ITfCategoryMgr *pCatMgr = NULL;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                                  IID_ITfCategoryMgr, (void **)&pCatMgr);
    if (FAILED(hr))
    {
        return hr;
    }

    static const GUID *const kCategories[] = {
        &GUID_TFCAT_TIP_KEYBOARD,
        &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
        &GUID_TFCAT_TIPCAP_COMLESS,
        &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    };

    for (size_t i = 0; i < ARRAYSIZE(kCategories); ++i)
    {
        pCatMgr->UnregisterCategory(clsid, *kCategories[i], clsid);
    }

    pCatMgr->Release();
    return S_OK;
}

// Drop one language profile from TSF and the language list. Leaves COM, categories,
// and other language profiles intact — used when switching the installed LANGID.
static HRESULT RemoveLanguageProfileOnly(REFCLSID clsid, REFGUID guidProfile, LANGID langid)
{
    InstallOrRemoveTip(clsid, guidProfile, langid, ILOT_UNINSTALL);
    InstallOrRemoveTip(clsid, guidProfile, langid, ILOT_UNINSTALL | ILOT_DEFUSER4);

    ITfInputProcessorProfileMgr *pMgr = NULL;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfileMgr, (void **)&pMgr);
    if (SUCCEEDED(hr) && pMgr != NULL)
    {
        pMgr->UnregisterProfile(clsid, langid, guidProfile, 0);
        pMgr->Release();
    }

    ITfInputProcessorProfiles *pProfiles = NULL;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          IID_ITfInputProcessorProfiles, (void **)&pProfiles);
    if (SUCCEEDED(hr) && pProfiles != NULL)
    {
        pProfiles->RemoveLanguageProfile(clsid, langid, guidProfile);
        pProfiles->Release();
    }
    return S_OK;
}

static HRESULT UnregisterProfileFor(REFCLSID clsid, REFGUID guidProfile, LANGID langid)
{
    // Remove the tip from the current user language list and from the .Default
    // profile (secure desktop / logon screen). Both are best effort: the entry
    // may be absent, and .Default removal requires elevation.
    InstallOrRemoveTip(clsid, guidProfile, langid, ILOT_UNINSTALL);
    InstallOrRemoveTip(clsid, guidProfile, langid, ILOT_UNINSTALL | ILOT_DEFUSER4);

    ITfInputProcessorProfileMgr *pMgr = NULL;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfileMgr, (void **)&pMgr);
    if (SUCCEEDED(hr) && pMgr != NULL)
    {
        pMgr->UnregisterProfile(clsid, langid, guidProfile, 0);
        pMgr->Release();
    }

    ITfInputProcessorProfiles *pProfiles = NULL;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          IID_ITfInputProcessorProfiles, (void **)&pProfiles);
    if (SUCCEEDED(hr) && pProfiles != NULL)
    {
        pProfiles->RemoveLanguageProfile(clsid, langid, guidProfile);
        pProfiles->Unregister(clsid);
        pProfiles->Release();
    }

    UnregisterCategoriesFor(clsid);
    UnregisterServer(clsid, TEXTSERVICE_CLSID_KEY);
    return S_OK;
}

static HRESULT ResolveIconPath(wchar_t *pszPath, size_t cchPath, ULONG *pcch)
{
    if (GetSidecarIconPath(pszPath, cchPath) && PathFileExistsW(pszPath))
    {
        *pcch = static_cast<ULONG>(wcslen(pszPath));
        return S_OK;
    }

    DWORD n = GetModuleFileNameW(g_hInst, pszPath, static_cast<DWORD>(cchPath));
    if (n == 0 || n >= cchPath)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *pcch = n;
    return S_OK;
}

static HRESULT RegisterProfilesEx(const wchar_t *pszName, LANGID langid)
{
    wchar_t szIconPath[1024] = {};
    ULONG cchIcon = 0;
    HRESULT hr = ResolveIconPath(szIconPath, ARRAYSIZE(szIconPath), &cchIcon);
    if (FAILED(hr))
    {
        return hr;
    }

    const ULONG cchName = static_cast<ULONG>(wcslen(pszName));

    ITfInputProcessorProfileMgr *pMgr = NULL;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          IID_ITfInputProcessorProfileMgr, (void **)&pMgr);
    if (SUCCEEDED(hr) && pMgr != NULL)
    {
        hr = pMgr->RegisterProfile(
            g_clsidTextService,
            langid,
            g_guidProfile,
            pszName,
            cchName,
            szIconPath,
            cchIcon,
            0,
            0,
            0,
            FALSE,
            0);
        pMgr->Release();
    }
    else
    {
        ITfInputProcessorProfiles *pProfiles = NULL;
        hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                              IID_ITfInputProcessorProfiles, (void **)&pProfiles);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = pProfiles->Register(g_clsidTextService);
        if (SUCCEEDED(hr))
        {
            hr = pProfiles->AddLanguageProfile(
                g_clsidTextService,
                langid,
                g_guidProfile,
                pszName,
                cchName,
                szIconPath,
                cchIcon,
                0);
        }
        pProfiles->Release();
    }

    return hr;
}

static HRESULT RegisterServer(HMODULE hModule, REFCLSID rclsid, const wchar_t *pszFriendlyName,
                             const wchar_t *pszPrefix, const wchar_t *pszThreadModel)
{
    wchar_t szCLSID[64] = {};
    wchar_t szInprocServer32[1024] = {};

    StringFromGUID2(rclsid, szCLSID, ARRAYSIZE(szCLSID));

    DWORD dwResult = GetModuleFileName(hModule, szInprocServer32, ARRAYSIZE(szInprocServer32));
    if (dwResult == 0 || dwResult >= ARRAYSIZE(szInprocServer32))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    wchar_t szKey[512] = {};
    StringCchCopy(szKey, ARRAYSIZE(szKey), pszPrefix);
    StringCchCat(szKey, ARRAYSIZE(szKey), szCLSID);

    HKEY hKey = NULL;
    HRESULT hr = HRESULT_FROM_WIN32(RegCreateKeyEx(HKEY_CLASSES_ROOT, szKey, 0, NULL,
                                                    REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL));
    if (FAILED(hr))
    {
        return hr;
    }

    hr = HRESULT_FROM_WIN32(RegSetValueEx(hKey, NULL, 0, REG_SZ, (const BYTE *)pszFriendlyName,
                                          (DWORD)((wcslen(pszFriendlyName) + 1) * sizeof(wchar_t))));

    HKEY hSubKey = NULL;
    if (SUCCEEDED(hr))
    {
        hr = HRESULT_FROM_WIN32(RegCreateKeyEx(hKey, L"InprocServer32", 0, NULL,
                                                REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hSubKey, NULL));
    }
    if (SUCCEEDED(hr))
    {
        hr = HRESULT_FROM_WIN32(RegSetValueEx(hSubKey, NULL, 0, REG_SZ, (const BYTE *)szInprocServer32,
                                              (DWORD)((wcslen(szInprocServer32) + 1) * sizeof(wchar_t))));
        if (SUCCEEDED(hr))
        {
            hr = HRESULT_FROM_WIN32(RegSetValueEx(hSubKey, L"ThreadingModel", 0, REG_SZ,
                                                  (const BYTE *)pszThreadModel,
                                                  (DWORD)((wcslen(pszThreadModel) + 1) * sizeof(wchar_t))));
        }
        RegCloseKey(hSubKey);
    }

    RegCloseKey(hKey);
    return hr;
}

static HRESULT UnregisterServer(REFCLSID rclsid, const wchar_t *pszPrefix)
{
    wchar_t szCLSID[64] = {};
    StringFromGUID2(rclsid, szCLSID, ARRAYSIZE(szCLSID));

    wchar_t szKey[512] = {};
    StringCchCopy(szKey, ARRAYSIZE(szKey), pszPrefix);
    StringCchCat(szKey, ARRAYSIZE(szKey), szCLSID);

    SHDeleteKey(HKEY_CLASSES_ROOT, szKey);
    return S_OK;
}

static HRESULT UnregisterProfiles()
{
    LANGID langid = ReadStoredLangId();
    HRESULT hr = UnregisterProfileFor(g_clsidTextService, g_guidProfile, langid);
    if (langid != g_langidProfile)
    {
        UnregisterProfileFor(g_clsidTextService, g_guidProfile, g_langidProfile);
    }
    return hr;
}

static HRESULT RegisterCategories()
{
    ITfCategoryMgr *pCatMgr = NULL;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER,
                                  IID_ITfCategoryMgr, (void **)&pCatMgr);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pCatMgr->RegisterCategory(g_clsidTextService, GUID_TFCAT_TIP_KEYBOARD, g_clsidTextService);
    if (SUCCEEDED(hr))
    {
        pCatMgr->RegisterCategory(g_clsidTextService, GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT, g_clsidTextService);
        pCatMgr->RegisterCategory(g_clsidTextService, GUID_TFCAT_TIPCAP_COMLESS, g_clsidTextService);
        pCatMgr->RegisterCategory(g_clsidTextService, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, g_clsidTextService);
    }

    pCatMgr->Release();
    return hr;
}

HRESULT UnregisterLegacyPlaceholder()
{
    return UnregisterProfileFor(g_clsidTextServiceLegacy, g_guidProfileLegacy, g_langidProfile);
}

static HRESULT EnsureCOM(BOOL *pfOwn)
{
    *pfOwn = FALSE;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == S_OK)
    {
        *pfOwn = TRUE;
        return S_OK;
    }
    if (hr == S_FALSE || hr == RPC_E_CHANGED_MODE)
    {
        return S_OK;
    }
    return hr;
}

HRESULT WINAPI RegisterTextService(LPCWSTR pszDisplayName, LANGID langid, DWORD dwFlags)
{
    if (pszDisplayName == NULL || pszDisplayName[0] == L'\0' || langid == 0)
    {
        return E_INVALIDARG;
    }

    BOOL ownCom = FALSE;
    HRESULT hr = EnsureCOM(&ownCom);
    if (FAILED(hr))
    {
        return hr;
    }

    // 0 表示之前没有安装记录（全新安装）；非 0 表示是升级/重注册。
    const LANGID previousLang = ReadStoredLangIdRaw();
    const BOOL isUpgrade = (previousLang != 0);
    UnregisterLegacyPlaceholder();

    // CLSID 键和 TIP 类别与旧版本是同一份（CLSID 不变），升级时失败不能回滚它们，
    // 否则会把原本可用的旧安装一起删掉。只有全新安装失败时才清理本次写入的内容。
    BOOL serverRegistered = FALSE;
    BOOL categoriesRegistered = FALSE;

    hr = RegisterServer(g_hInst, g_clsidTextService, pszDisplayName,
                        TEXTSERVICE_CLSID_KEY, L"Apartment");
    if (SUCCEEDED(hr))
    {
        serverRegistered = TRUE;
        hr = RegisterCategories();
    }
    if (SUCCEEDED(hr))
    {
        categoriesRegistered = TRUE;
        hr = RegisterProfilesEx(pszDisplayName, langid);
    }

    if (SUCCEEDED(hr))
    {
        // 新 profile 注册成功后才移除旧 langid 的 profile，避免中途失败时两头都没了。
        if (isUpgrade && previousLang != langid)
        {
            RemoveLanguageProfileOnly(g_clsidTextService, g_guidProfile, previousLang);
        }

        // 记录失败要报告出来：卸载时依赖这里的 LangId 找到要移除的 profile。
        hr = StoreInstallState(pszDisplayName, langid);

        if (SUCCEEDED(hr) && (dwFlags & KM_REGISTER_INSTALL_TIP))
        {
            // Current user language list.
            InstallOrRemoveTip(g_clsidTextService, g_guidProfile, langid, 0);
            // .Default profile (logon / lock screen and new user profiles).
            // Best effort; requires elevation.
            InstallOrRemoveTip(g_clsidTextService, g_guidProfile, langid, ILOT_DEFUSER4);
        }
    }
    else if (!isUpgrade)
    {
        // 全新安装失败：逆序清理本次写入的内容（profile 注册本身已失败，无需移除）。
        if (categoriesRegistered)
        {
            UnregisterCategoriesFor(g_clsidTextService);
        }
        if (serverRegistered)
        {
            UnregisterServer(g_clsidTextService, TEXTSERVICE_CLSID_KEY);
        }
    }

    if (ownCom)
    {
        CoUninitialize();
    }
    return hr;
}

HRESULT WINAPI UnregisterTextService(void)
{
    BOOL ownCom = FALSE;
    HRESULT hr = EnsureCOM(&ownCom);
    if (FAILED(hr))
    {
        return hr;
    }

    UnregisterLegacyPlaceholder();
    hr = UnregisterProfiles();
    DeleteInstallState();

    if (ownCom)
    {
        CoUninitialize();
    }
    return hr;
}

HRESULT WINAPI InstallUserTip(LANGID langid)
{
    if (langid == 0)
    {
        langid = ReadStoredLangId();
    }

    BOOL ownCom = FALSE;
    HRESULT hr = EnsureCOM(&ownCom);
    if (FAILED(hr))
    {
        return hr;
    }

    // Current user language list only; this entry point is meant to run as the
    // original (non-elevated) user during setup.
    hr = InstallOrRemoveTip(g_clsidTextService, g_guidProfile, langid, 0);
    if (ownCom)
    {
        CoUninitialize();
    }
    return hr;
}

HRESULT WINAPI InstallUserTipDefault(LANGID langid)
{
    if (langid == 0)
    {
        langid = ReadStoredLangId();
    }

    BOOL ownCom = FALSE;
    HRESULT hr = EnsureCOM(&ownCom);
    if (FAILED(hr))
    {
        return hr;
    }

    // Add the tip to the .Default profile, which backs the secure desktop
    // (logon / lock screen). Requires elevation; callers must run elevated.
    hr = InstallOrRemoveTip(g_clsidTextService, g_guidProfile, langid, ILOT_DEFUSER4);
    if (ownCom)
    {
        CoUninitialize();
    }
    return hr;
}

HRESULT WINAPI UninstallUserTip(LANGID langid)
{
    if (langid == 0)
    {
        langid = ReadStoredLangId();
    }

    BOOL ownCom = FALSE;
    HRESULT hr = EnsureCOM(&ownCom);
    if (FAILED(hr))
    {
        return hr;
    }

    HRESULT hrUser = InstallOrRemoveTip(g_clsidTextService, g_guidProfile, langid, ILOT_UNINSTALL);
    // Also clear the .Default (secure desktop / logon screen) entry. Best
    // effort: it needs elevation and may be absent.
    InstallOrRemoveTip(g_clsidTextService, g_guidProfile, langid, ILOT_UNINSTALL | ILOT_DEFUSER4);
    hr = hrUser;
    if (ownCom)
    {
        CoUninitialize();
    }
    return hr;
}
