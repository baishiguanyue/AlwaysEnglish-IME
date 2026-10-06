#include "Globals.h"
#include "ClassFactory.h"
#include "TextService.h"
#include <new>

CClassFactory::CClassFactory()
    : m_cRef(1)
{
    InterlockedIncrement(&g_cRefDll);
}

CClassFactory::~CClassFactory()
{
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CClassFactory::QueryInterface(REFIID riid, void **ppvObject)
{
    if (ppvObject == NULL)
    {
        return E_POINTER;
    }

    *ppvObject = NULL;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
    {
        *ppvObject = static_cast<IClassFactory *>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CClassFactory::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CClassFactory::Release()
{
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
    {
        delete this;
    }
    return cRef;
}

STDMETHODIMP CClassFactory::CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppvObject)
{
    if (ppvObject == NULL)
    {
        return E_POINTER;
    }

    *ppvObject = NULL;

    if (pUnkOuter != NULL)
    {
        return CLASS_E_NOAGGREGATION;
    }

    CTextService *pTextService = new (std::nothrow) CTextService();
    if (pTextService == NULL)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pTextService->QueryInterface(riid, ppvObject);
    pTextService->Release();

    return hr;
}

STDMETHODIMP CClassFactory::LockServer(BOOL fLock)
{
    if (fLock)
    {
        InterlockedIncrement(&g_cRefDll);
    }
    else
    {
        InterlockedDecrement(&g_cRefDll);
    }
    return S_OK;
}
