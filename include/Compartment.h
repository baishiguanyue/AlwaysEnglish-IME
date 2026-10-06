#pragma once

#include <msctf.h>

HRESULT SetCompartmentDWORD(ITfThreadMgr *pThreadMgr, TfClientId clientId, REFGUID guidCompartment, DWORD dwValue);
HRESULT GetCompartmentDWORD(ITfThreadMgr *pThreadMgr, REFGUID guidCompartment, DWORD *pdwValue);
HRESULT AdviseCompartmentSink(ITfThreadMgr *pThreadMgr, REFGUID guidCompartment, ITfCompartmentEventSink *pSink, DWORD *pdwCookie);
HRESULT UnadviseCompartmentSink(ITfThreadMgr *pThreadMgr, REFGUID guidCompartment, DWORD *pdwCookie);
