#include "Globals.h"
#include "TextService.h"
#include "Compartment.h"
#include <imm.h>

#ifndef IME_CMODE_ALPHANUMERIC
#define IME_CMODE_ALPHANUMERIC 0x0000
#endif
#ifndef IME_SMODE_NONE
#define IME_SMODE_NONE 0x0000
#endif

// 切换输入法瞬间，给对方 TIP（如搜狗）留出完整收尾时间，
// 避免我们在对方 Deactivate 过程中反复写入 compartment 干扰其 UI 清理。
// 只用于 OnChange 触发的重写；激活/焦点回调的显式施加不受此限制。
// 窗口内被忽略的 OnChange 会在窗口结束后通过线程定时器补写一次。
constexpr ULONGLONG K_TRANSITION_IGNORE_MS = 300;

// TIP 是 apartment 线程模型、每个线程管理器一个实例，回调总在同一线程上。
// 线程定时器的回调没有上下文参数，用 thread_local 找回当前线程的活动实例。
static thread_local CTextService *t_pActiveService = NULL;

CTextService::CTextService()
    : m_cRef(1)
    , m_pThreadMgr(NULL)
    , m_tfClientId(TF_CLIENTID_NULL)
    , m_dwThreadMgrEventSinkCookie(TF_INVALID_COOKIE)
    , m_dwThreadFocusCookie(TF_INVALID_COOKIE)
    , m_dwOpenCloseCookie(TF_INVALID_COOKIE)
    , m_dwConversionCookie(TF_INVALID_COOKIE)
    , m_dwSentenceCookie(TF_INVALID_COOKIE)
    , m_fApplyingState(FALSE)
    , m_uReapplyTimer(0)
    , m_ullLastActivateTick(0)
    , m_fSavedOpenClose(FALSE)
    , m_dwSavedOpenClose(0)
    , m_fSavedConversion(FALSE)
    , m_dwSavedConversion(0)
    , m_fSavedSentence(FALSE)
    , m_dwSavedSentence(0)
{
    InterlockedIncrement(&g_cRefDll);
}

CTextService::~CTextService()
{
    Deactivate();
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void **ppvObject)
{
    if (ppvObject == NULL)
    {
        return E_POINTER;
    }

    *ppvObject = NULL;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
        *ppvObject = static_cast<ITfTextInputProcessorEx *>(this);
    }
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    {
        *ppvObject = static_cast<ITfThreadMgrEventSink *>(this);
    }
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    {
        *ppvObject = static_cast<ITfThreadFocusSink *>(this);
    }
    else if (IsEqualIID(riid, IID_ITfCompartmentEventSink))
    {
        *ppvObject = static_cast<ITfCompartmentEventSink *>(this);
    }
    else
    {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CTextService::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CTextService::Release()
{
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
    {
        delete this;
    }
    return cRef;
}

STDMETHODIMP CTextService::Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    return ActivateEx(pThreadMgr, tfClientId, 0);
}

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD dwFlags)
{
    UNREFERENCED_PARAMETER(dwFlags);

    if (pThreadMgr == NULL)
    {
        return E_INVALIDARG;
    }

    Deactivate();

    m_pThreadMgr = pThreadMgr;
    m_pThreadMgr->AddRef();
    m_tfClientId = tfClientId;
    m_ullLastActivateTick = GetTickCount64();
    t_pActiveService = this;

    HRESULT hr = _InitThreadMgrEventSink();
    if (SUCCEEDED(hr))
    {
        hr = _InitThreadFocusSink();
    }
    if (SUCCEEDED(hr))
    {
        hr = _InitCompartmentSinks();
    }

    if (FAILED(hr))
    {
        Deactivate();
        return hr;
    }

    // 保存进入前的 compartment 状态,Deactivate 时恢复,
    // 避免下一个输入法继承我们强制的"英文/关闭"状态。
    m_fSavedOpenClose = SUCCEEDED(GetCompartmentDWORD(m_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &m_dwSavedOpenClose));
    m_fSavedConversion = SUCCEEDED(GetCompartmentDWORD(m_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &m_dwSavedConversion));
    m_fSavedSentence = SUCCEEDED(GetCompartmentDWORD(m_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE, &m_dwSavedSentence));

    _ApplyAlwaysEnglishState();
    return S_OK;
}

STDMETHODIMP CTextService::Deactivate()
{
    if (m_pThreadMgr == NULL)
    {
        return S_OK;
    }

    _CancelDeferredReapply();
    if (t_pActiveService == this)
    {
        t_pActiveService = NULL;
    }

    // 先撤掉 compartment sink，再恢复进入前的状态：否则恢复写入会同步回调 OnChange，
    // 立刻又被 _ApplyAlwaysEnglishState 改回英文，恢复形同虚设。
    // clientId 在 m_tfClientId 清空前一直有效，与 sink 无关。
    _UninitCompartmentSinks();

    if (m_fSavedOpenClose)
    {
        SetCompartmentDWORD(m_pThreadMgr, m_tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, m_dwSavedOpenClose);
    }
    if (m_fSavedConversion)
    {
        SetCompartmentDWORD(m_pThreadMgr, m_tfClientId, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, m_dwSavedConversion);
    }
    if (m_fSavedSentence)
    {
        SetCompartmentDWORD(m_pThreadMgr, m_tfClientId, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE, m_dwSavedSentence);
    }
    m_fSavedOpenClose = m_fSavedConversion = m_fSavedSentence = FALSE;

    _UninitThreadFocusSink();
    _UninitThreadMgrEventSink();

    m_tfClientId = TF_CLIENTID_NULL;
    m_pThreadMgr->Release();
    m_pThreadMgr = NULL;
    return S_OK;
}

STDMETHODIMP CTextService::OnInitDocumentMgr(ITfDocumentMgr *pDim)
{
    UNREFERENCED_PARAMETER(pDim);
    return S_OK;
}

STDMETHODIMP CTextService::OnUninitDocumentMgr(ITfDocumentMgr *pDim)
{
    UNREFERENCED_PARAMETER(pDim);
    return S_OK;
}

STDMETHODIMP CTextService::OnSetFocus(ITfDocumentMgr *pDimFocus, ITfDocumentMgr *pDimPrevFocus)
{
    UNREFERENCED_PARAMETER(pDimFocus);
    UNREFERENCED_PARAMETER(pDimPrevFocus);
    _ApplyAlwaysEnglishState();
    return S_OK;
}

STDMETHODIMP CTextService::OnPushContext(ITfContext *pContext)
{
    UNREFERENCED_PARAMETER(pContext);
    return S_OK;
}

STDMETHODIMP CTextService::OnPopContext(ITfContext *pContext)
{
    UNREFERENCED_PARAMETER(pContext);
    return S_OK;
}

STDMETHODIMP CTextService::OnSetThreadFocus()
{
    _ApplyAlwaysEnglishState();
    return S_OK;
}

STDMETHODIMP CTextService::OnKillThreadFocus()
{
    return S_OK;
}

STDMETHODIMP CTextService::OnChange(REFGUID rguid)
{
    if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) ||
        IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) ||
        IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE))
    {
        _ApplyAlwaysEnglishState(TRUE);
    }
    return S_OK;
}

HRESULT CTextService::_InitThreadMgrEventSink()
{
    ITfSource *pSource = NULL;
    HRESULT hr = m_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pSource->AdviseSink(IID_ITfThreadMgrEventSink,
                             static_cast<ITfThreadMgrEventSink *>(this),
                             &m_dwThreadMgrEventSinkCookie);
    pSource->Release();
    if (FAILED(hr))
    {
        m_dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
    }
    return hr;
}

HRESULT CTextService::_UninitThreadMgrEventSink()
{
    if (m_pThreadMgr == NULL || m_dwThreadMgrEventSinkCookie == TF_INVALID_COOKIE)
    {
        return S_OK;
    }

    ITfSource *pSource = NULL;
    if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource)))
    {
        pSource->UnadviseSink(m_dwThreadMgrEventSinkCookie);
        pSource->Release();
    }
    m_dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
    return S_OK;
}

HRESULT CTextService::_InitThreadFocusSink()
{
    ITfSource *pSource = NULL;
    HRESULT hr = m_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pSource->AdviseSink(IID_ITfThreadFocusSink,
                             static_cast<ITfThreadFocusSink *>(this),
                             &m_dwThreadFocusCookie);
    pSource->Release();
    if (FAILED(hr))
    {
        m_dwThreadFocusCookie = TF_INVALID_COOKIE;
    }
    return hr;
}

HRESULT CTextService::_UninitThreadFocusSink()
{
    if (m_pThreadMgr == NULL || m_dwThreadFocusCookie == TF_INVALID_COOKIE)
    {
        return S_OK;
    }

    ITfSource *pSource = NULL;
    if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource)))
    {
        pSource->UnadviseSink(m_dwThreadFocusCookie);
        pSource->Release();
    }
    m_dwThreadFocusCookie = TF_INVALID_COOKIE;
    return S_OK;
}

HRESULT CTextService::_InitCompartmentSinks()
{
    HRESULT hr = AdviseCompartmentSink(m_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                       this, &m_dwOpenCloseCookie);
    if (FAILED(hr))
    {
        return hr;
    }

    // Conversion/sentence sinks are best-effort; open/close is the compatibility contract.
    AdviseCompartmentSink(m_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
                          this, &m_dwConversionCookie);
    AdviseCompartmentSink(m_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE,
                          this, &m_dwSentenceCookie);
    return S_OK;
}

HRESULT CTextService::_UninitCompartmentSinks()
{
    UnadviseCompartmentSink(m_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &m_dwOpenCloseCookie);
    UnadviseCompartmentSink(m_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &m_dwConversionCookie);
    UnadviseCompartmentSink(m_pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE, &m_dwSentenceCookie);
    return S_OK;
}

HRESULT CTextService::_ApplyAlwaysEnglishState(BOOL fRespectTransitionWindow)
{
    if (m_fApplyingState || m_pThreadMgr == NULL || m_tfClientId == TF_CLIENTID_NULL)
    {
        return S_OK;
    }

    // 切换瞬间（Activate 后 K_TRANSITION_IGNORE_MS 内）不立即响应 OnChange，
    // 给刚 Deactivate 的对方 TIP（如搜狗）留出收尾时间，避免互相写 compartment 形成事件风暴。
    // 被忽略的变化不会丢：窗口结束后由定时器补写一次。
    if (fRespectTransitionWindow &&
        GetTickCount64() - m_ullLastActivateTick < K_TRANSITION_IGNORE_MS)
    {
        _ScheduleDeferredReapply();
        return S_OK;
    }

    m_fApplyingState = TRUE;

    // Align with Microsoft Pinyin English mode: IME closed + alphanumeric conversion.
    // Values can be retuned after measuring the live Microsoft Pinyin compartments.
    HRESULT hrClose = SetCompartmentDWORD(m_pThreadMgr, m_tfClientId,
                                          GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, 0);
    HRESULT hrConv = SetCompartmentDWORD(m_pThreadMgr, m_tfClientId,
                                          GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
                                          IME_CMODE_ALPHANUMERIC);
    HRESULT hrSent = SetCompartmentDWORD(m_pThreadMgr, m_tfClientId,
                                          GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE,
                                          IME_SMODE_NONE);

#ifdef _DEBUG
    if (FAILED(hrClose) || FAILED(hrConv) || FAILED(hrSent))
    {
        char msg[256];
        sprintf_s(msg, "AlwaysEnglishIME: SetCompartmentDWORD failed: close=0x%08X conv=0x%08X sent=0x%08X\r\n",
                  hrClose, hrConv, hrSent);
        OutputDebugStringA(msg);
    }
#endif

    m_fApplyingState = FALSE;
    return S_OK;
}

void CTextService::_ScheduleDeferredReapply()
{
    if (m_uReapplyTimer != 0 || m_pThreadMgr == NULL)
    {
        return;
    }

    ULONGLONG elapsed = GetTickCount64() - m_ullLastActivateTick;
    UINT delay = (elapsed < K_TRANSITION_IGNORE_MS)
                     ? static_cast<UINT>(K_TRANSITION_IGNORE_MS - elapsed) + 10
                     : 10;
    // hwnd 为 NULL 的线程定时器：回调在本线程的消息循环里执行，与 TSF 回调同线程。
    m_uReapplyTimer = SetTimer(NULL, 0, delay, _DeferredReapplyTimerProc);
}

void CTextService::_CancelDeferredReapply()
{
    if (m_uReapplyTimer != 0)
    {
        KillTimer(NULL, m_uReapplyTimer);
        m_uReapplyTimer = 0;
    }
}

VOID CALLBACK CTextService::_DeferredReapplyTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(uMsg);
    UNREFERENCED_PARAMETER(dwTime);

    // 一次性定时器：无论是否还能找到实例都先杀掉。
    KillTimer(NULL, idEvent);

    // 只认本线程当前活动、且确实持有这个定时器的实例；
    // Deactivate 已清空 t_pActiveService，残留的 WM_TIMER 在这里变成空操作。
    CTextService *pService = t_pActiveService;
    if (pService == NULL || pService->m_uReapplyTimer != idEvent)
    {
        return;
    }

    pService->m_uReapplyTimer = 0;
    pService->AddRef();  // 防止施加过程中被释放
    pService->_ApplyAlwaysEnglishState();
    pService->Release();
}
