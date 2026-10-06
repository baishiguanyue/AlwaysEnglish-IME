#include "Globals.h"
#include <strsafe.h>

const wchar_t g_wszClassName[] = L"KeyboardMethod";
const wchar_t g_wszInstallKey[] = L"Software\\bsgy\\KeyboardMethod";
const wchar_t g_wszSidecarIconName[] = L"icon.ico";
HMODULE g_hInst = NULL;
LONG g_cRefDll = 0;

BOOL GetSidecarIconPath(wchar_t *pszPath, size_t cchPath)
{
    if (pszPath == NULL || cchPath == 0 || g_hInst == NULL)
    {
        return FALSE;
    }

    DWORD n = GetModuleFileNameW(g_hInst, pszPath, static_cast<DWORD>(cchPath));
    if (n == 0 || n >= cchPath)
    {
        return FALSE;
    }

    wchar_t *slash = wcsrchr(pszPath, L'\\');
    if (slash == NULL)
    {
        return FALSE;
    }

    slash[1] = L'\0';
    return SUCCEEDED(StringCchCatW(pszPath, cchPath, g_wszSidecarIconName));
}
