#pragma once

#include <msctf.h>

class CTextService : public ITfTextInputProcessorEx,
                     public ITfThreadMgrEventSink,
                     public ITfThreadFocusSink,
                     public ITfCompartmentEventSink
{
public:
    CTextService();
    virtual ~CTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // ITfTextInputProcessor / ITfTextInputProcessorEx
    STDMETHODIMP Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    STDMETHODIMP ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD dwFlags);
    STDMETHODIMP Deactivate();

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *pDim);
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *pDim);
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *pDimFocus, ITfDocumentMgr *pDimPrevFocus);
    STDMETHODIMP OnPushContext(ITfContext *pContext);
    STDMETHODIMP OnPopContext(ITfContext *pContext);

    // ITfThreadFocusSink
    STDMETHODIMP OnSetThreadFocus();
    STDMETHODIMP OnKillThreadFocus();

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID rguid);

private:
    HRESULT _InitThreadMgrEventSink();
    HRESULT _UninitThreadMgrEventSink();
    HRESULT _InitThreadFocusSink();
    HRESULT _UninitThreadFocusSink();
    HRESULT _InitCompartmentSinks();
    HRESULT _UninitCompartmentSinks();
    // fRespectTransitionWindow: TRUE 仅用于 OnChange 触发的重写(切换瞬间对方 TIP 的抖动)；
    // 激活和焦点回调里的显式施加不受 300ms 窗口限制。
    HRESULT _ApplyAlwaysEnglishState(BOOL fRespectTransitionWindow = FALSE);
    void _ScheduleDeferredReapply();
    void _CancelDeferredReapply();
    static VOID CALLBACK _DeferredReapplyTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

    LONG m_cRef;
    ITfThreadMgr *m_pThreadMgr;
    TfClientId m_tfClientId;
    DWORD m_dwThreadMgrEventSinkCookie;
    DWORD m_dwThreadFocusCookie;
    DWORD m_dwOpenCloseCookie;
    DWORD m_dwConversionCookie;
    DWORD m_dwSentenceCookie;
    BOOL m_fApplyingState;
    UINT_PTR m_uReapplyTimer;         // 300ms 窗口内被忽略的 OnChange 的延后补写定时器(线程定时器，0 表示无)
    ULONGLONG m_ullLastActivateTick;  // 上次 ActivateEx 的 GetTickCount64()，用于忽略切换瞬间其他 TIP 的 compartment 抖动

    // 进入本输入法前三个 compartment 的原始值,Deactivate 时恢复,
    // 避免下一个输入法继承我们强制的"英文/关闭"状态。
    BOOL m_fSavedOpenClose;
    DWORD m_dwSavedOpenClose;
    BOOL m_fSavedConversion;
    DWORD m_dwSavedConversion;
    BOOL m_fSavedSentence;
    DWORD m_dwSavedSentence;
};
