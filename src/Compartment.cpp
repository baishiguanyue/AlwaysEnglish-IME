#include "Compartment.h"
#include <oleauto.h>

HRESULT SetCompartmentDWORD(ITfThreadMgr *pThreadMgr, TfClientId clientId, REFGUID guidCompartment, DWORD dwValue)
{
    if (pThreadMgr == NULL)
    {
        return E_INVALIDARG;
    }

    DWORD dwCurrent = 0;
    if (SUCCEEDED(GetCompartmentDWORD(pThreadMgr, guidCompartment, &dwCurrent)) && dwCurrent == dwValue)
    {
        return S_OK;
    }

    ITfCompartmentMgr *pCompMgr = NULL;
    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfCompartmentMgr, (void **)&pCompMgr);
    if (FAILED(hr))
    {
        return hr;
    }

    ITfCompartment *pComp = NULL;
    hr = pCompMgr->GetCompartment(guidCompartment, &pComp);
    pCompMgr->Release();
    if (FAILED(hr) || pComp == NULL)
    {
        return FAILED(hr) ? hr : E_FAIL;
    }

    VARIANT var;
    VariantInit(&var);
    var.vt = VT_I4;
    var.lVal = static_cast<LONG>(dwValue);
    hr = pComp->SetValue(clientId, &var);
    pComp->Release();
    return hr;
}

HRESULT GetCompartmentDWORD(ITfThreadMgr *pThreadMgr, REFGUID guidCompartment, DWORD *pdwValue)
{
    if (pThreadMgr == NULL || pdwValue == NULL)
    {
        return E_INVALIDARG;
    }

    *pdwValue = 0;

    ITfCompartmentMgr *pCompMgr = NULL;
    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfCompartmentMgr, (void **)&pCompMgr);
    if (FAILED(hr))
    {
        return hr;
    }

    ITfCompartment *pComp = NULL;
    hr = pCompMgr->GetCompartment(guidCompartment, &pComp);
    pCompMgr->Release();
    if (FAILED(hr) || pComp == NULL)
    {
        return FAILED(hr) ? hr : E_FAIL;
    }

    VARIANT var;
    VariantInit(&var);
    hr = pComp->GetValue(&var);
    pComp->Release();
    if (FAILED(hr))
    {
        return hr;
    }

    if (var.vt == VT_I4)
    {
        *pdwValue = static_cast<DWORD>(var.lVal);
        hr = S_OK;
    }
    else if (var.vt == VT_EMPTY)
    {
        *pdwValue = 0;
        hr = S_FALSE;
    }
    else
    {
        hr = E_UNEXPECTED;
    }

    VariantClear(&var);
    return hr;
}

HRESULT AdviseCompartmentSink(ITfThreadMgr *pThreadMgr, REFGUID guidCompartment, ITfCompartmentEventSink *pSink, DWORD *pdwCookie)
{
    if (pThreadMgr == NULL || pSink == NULL || pdwCookie == NULL)
    {
        return E_INVALIDARG;
    }

    *pdwCookie = TF_INVALID_COOKIE;

    ITfCompartmentMgr *pCompMgr = NULL;
    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfCompartmentMgr, (void **)&pCompMgr);
    if (FAILED(hr))
    {
        return hr;
    }

    ITfCompartment *pComp = NULL;
    hr = pCompMgr->GetCompartment(guidCompartment, &pComp);
    pCompMgr->Release();
    if (FAILED(hr) || pComp == NULL)
    {
        return FAILED(hr) ? hr : E_FAIL;
    }

    ITfSource *pSource = NULL;
    hr = pComp->QueryInterface(IID_ITfSource, (void **)&pSource);
    pComp->Release();
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pSource->AdviseSink(IID_ITfCompartmentEventSink, pSink, pdwCookie);
    pSource->Release();
    return hr;
}

HRESULT UnadviseCompartmentSink(ITfThreadMgr *pThreadMgr, REFGUID guidCompartment, DWORD *pdwCookie)
{
    if (pdwCookie == NULL || *pdwCookie == TF_INVALID_COOKIE)
    {
        return S_OK;
    }

    if (pThreadMgr == NULL)
    {
        *pdwCookie = TF_INVALID_COOKIE;
        return S_OK;
    }

    ITfCompartmentMgr *pCompMgr = NULL;
    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfCompartmentMgr, (void **)&pCompMgr);
    if (FAILED(hr))
    {
        *pdwCookie = TF_INVALID_COOKIE;
        return hr;
    }

    ITfCompartment *pComp = NULL;
    hr = pCompMgr->GetCompartment(guidCompartment, &pComp);
    pCompMgr->Release();
    if (FAILED(hr) || pComp == NULL)
    {
        *pdwCookie = TF_INVALID_COOKIE;
        return FAILED(hr) ? hr : E_FAIL;
    }

    ITfSource *pSource = NULL;
    hr = pComp->QueryInterface(IID_ITfSource, (void **)&pSource);
    pComp->Release();
    if (SUCCEEDED(hr) && pSource != NULL)
    {
        pSource->UnadviseSink(*pdwCookie);
        pSource->Release();
    }

    *pdwCookie = TF_INVALID_COOKIE;
    return S_OK;
}
