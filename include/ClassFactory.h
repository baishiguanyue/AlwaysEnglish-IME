#pragma once

#include <unknwn.h>
#include "Globals.h"

class CClassFactory : public IClassFactory
{
public:
    CClassFactory();
    virtual ~CClassFactory();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IClassFactory methods
    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppvObject);
    STDMETHODIMP LockServer(BOOL fLock);

private:
    LONG m_cRef;
};
