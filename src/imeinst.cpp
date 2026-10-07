#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <stdlib.h>
#include <msctf.h>

typedef HRESULT (WINAPI *PFN_RegisterTextService)(LPCWSTR, LANGID, DWORD);
typedef HRESULT (WINAPI *PFN_UnregisterTextService)(void);
typedef HRESULT (WINAPI *PFN_InstallUserTip)(LANGID);
typedef HRESULT (WINAPI *PFN_InstallUserTipDefault)(LANGID);
typedef HRESULT (WINAPI *PFN_UninstallUserTip)(LANGID);

static int Fail(HRESULT hr)
{
    return FAILED(hr) ? static_cast<int>(hr) : 1;
}

static BOOL SiblingDllPath(wchar_t *psz, size_t cch)
{
    DWORD n = GetModuleFileNameW(NULL, psz, static_cast<DWORD>(cch));
    if (n == 0 || n >= cch)
    {
        return FALSE;
    }
    wchar_t *slash = wcsrchr(psz, L'\\');
    if (slash == NULL)
    {
        return FALSE;
    }
    slash[1] = L'\0';
    return SUCCEEDED(StringCchCatW(psz, cch, L"AlwaysEnglishIME.dll"));
}

static LANGID ParseLangId(const wchar_t *psz)
{
    if (psz == NULL || psz[0] == L'\0')
    {
        return 0;
    }
    const wchar_t *p = psz;
    if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X'))
    {
        p += 2;
    }
    return static_cast<LANGID>(wcstoul(p, NULL, 16));
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL || argc < 2)
    {
        if (argv != NULL)
        {
            LocalFree(argv);
        }
        return 1;
    }

    wchar_t szDll[1024] = {};
    if (!SiblingDllPath(szDll, ARRAYSIZE(szDll)))
    {
        LocalFree(argv);
        return 1;
    }

    HMODULE hDll = LoadLibraryExW(szDll, NULL,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hDll == NULL)
    {
        hDll = LoadLibraryW(szDll);
    }
    if (hDll == NULL)
    {
        LocalFree(argv);
        return 1;
    }

    HRESULT hr = E_INVALIDARG;
    if (_wcsicmp(argv[1], L"register") == 0 && argc >= 5)
    {
        auto pfn = reinterpret_cast<PFN_RegisterTextService>(
            GetProcAddress(hDll, "RegisterTextService"));
        LANGID langid = ParseLangId(argv[3]);
        DWORD flags = (ParseLangId(argv[4]) != 0) ? 0x00000001 : 0;
        hr = pfn ? pfn(argv[2], langid, flags) : E_POINTER;
    }
    else if (_wcsicmp(argv[1], L"unregister") == 0)
    {
        auto pfn = reinterpret_cast<PFN_UnregisterTextService>(
            GetProcAddress(hDll, "UnregisterTextService"));
        hr = pfn ? pfn() : E_POINTER;
    }
    else if (_wcsicmp(argv[1], L"install-tip") == 0 && argc >= 3)
    {
        auto pfn = reinterpret_cast<PFN_InstallUserTip>(
            GetProcAddress(hDll, "InstallUserTip"));
        hr = pfn ? pfn(ParseLangId(argv[2])) : E_POINTER;
    }
    else if (_wcsicmp(argv[1], L"install-tip-default") == 0 && argc >= 3)
    {
        // Must run elevated: writes the tip into the .Default profile that
        // backs the secure desktop (logon / lock screen).
        auto pfn = reinterpret_cast<PFN_InstallUserTipDefault>(
            GetProcAddress(hDll, "InstallUserTipDefault"));
        hr = pfn ? pfn(ParseLangId(argv[2])) : E_POINTER;
    }
    else if (_wcsicmp(argv[1], L"uninstall-tip") == 0 && argc >= 3)
    {
        auto pfn = reinterpret_cast<PFN_UninstallUserTip>(
            GetProcAddress(hDll, "UninstallUserTip"));
        hr = pfn ? pfn(ParseLangId(argv[2])) : E_POINTER;
    }

    FreeLibrary(hDll);
    LocalFree(argv);
    return SUCCEEDED(hr) ? 0 : Fail(hr);
}
