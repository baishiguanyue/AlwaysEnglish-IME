#include "Globals.h"
#include "ClassFactory.h"
#include "Register.h"
#include <new>

STDAPI DllRegisterServer(void);
STDAPI DllUnregisterServer(void);

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(lpReserved);

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
        break;

    case DLL_PROCESS_DETACH:
        g_hInst = NULL;
        break;
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
    return (g_cRefDll <= 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (ppv == NULL)
    {
        return E_POINTER;
    }

    *ppv = NULL;

    if (IsEqualCLSID(rclsid, g_clsidTextService))
    {
        CClassFactory *pFactory = new (std::nothrow) CClassFactory();
        if (pFactory == NULL)
        {
            return E_OUTOFMEMORY;
        }

        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllRegisterServer(void)
{
    UnregisterLegacyPlaceholder();
    return RegisterTextService(g_wszClassName, g_langidProfile, KM_REGISTER_INSTALL_TIP);
}

STDAPI DllUnregisterServer(void)
{
    return UnregisterTextService();
}
