// The COM half of OutlookDesktop.h: asks the desktop Outlook for the default
// calendar's occurrences in a range. Plain IDispatch late binding, so the build
// needs no Outlook type library and no ActiveQt — only ole32/oleaut32, which
// every Windows has. Read-only: nothing here sets anything on an item.

#include "cal/OutlookDesktop.h"
#include "cal/OutlookWatch.h"

#include <QTimer>

#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

#include <objbase.h>
#include <ocidl.h>
#include <oleauto.h>
#include <windows.h>

namespace heap::cal {

namespace {

// COM for this thread. Apartment-threaded, which is what Outlook's object
// model expects; balanced on scope exit.
struct ComScope {
  HRESULT hr;

  ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {
  }

  ~ComScope() {
    if(SUCCEEDED(hr)) {
      CoUninitialize();
    }
  }

  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;
};

struct Var {
  VARIANT v;

  Var() {
    VariantInit(&v);
  }

  ~Var() {
    VariantClear(&v);
  }

  Var(const Var&) = delete;
  Var& operator=(const Var&) = delete;
};

// An owned IDispatch*.
class Disp {
 public:
  Disp() = default;

  explicit Disp(IDispatch* p) : m_p(p) {
  }

  ~Disp() {
    reset();
  }

  Disp(const Disp&) = delete;
  Disp& operator=(const Disp&) = delete;

  Disp(Disp&& o) noexcept : m_p(o.m_p) {
    o.m_p = nullptr;
  }

  Disp& operator=(Disp&& o) noexcept {
    if(this != &o) {
      reset();
      m_p = o.m_p;
      o.m_p = nullptr;
    }
    return *this;
  }

  void reset() {
    if(m_p != nullptr) {
      m_p->Release();
      m_p = nullptr;
    }
  }

  [[nodiscard]] IDispatch* get() const {
    return m_p;
  }

  explicit operator bool() const {
    return m_p != nullptr;
  }

 private:
  IDispatch* m_p = nullptr;
};

QString hrText(HRESULT hr) {
  return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

// obj.name(args…) or obj.name, by flags. Args are given first to last.
HRESULT invoke(IDispatch* obj, const wchar_t* name, WORD flags, VARIANT* result, std::initializer_list<const VARIANT*> args = {}) {
  if(obj == nullptr) {
    return E_POINTER;
  }
  DISPID id = 0;
  auto* n = const_cast<LPOLESTR>(name);
  HRESULT hr = obj->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
  if(FAILED(hr)) {
    return hr;
  }
  // DISPPARAMS wants the arguments last to first.
  std::vector<VARIANT> rev;
  rev.reserve(args.size());
  for(auto it = std::rbegin(args); it != std::rend(args); ++it) {
    rev.push_back(**it);
  }
  DISPPARAMS params{};
  params.cArgs = static_cast<UINT>(rev.size());
  params.rgvarg = rev.empty() ? nullptr : rev.data();
  DISPID named = DISPID_PROPERTYPUT;
  if((flags & DISPATCH_PROPERTYPUT) != 0) {
    params.cNamedArgs = 1;
    params.rgdispidNamedArgs = &named;
  }
  return obj->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result, nullptr, nullptr);
}

Disp getObject(IDispatch* obj, const wchar_t* name, WORD flags = DISPATCH_PROPERTYGET, std::initializer_list<const VARIANT*> args = {}) {
  Var r;
  if(FAILED(invoke(obj, name, flags, &r.v, args)) || r.v.vt != VT_DISPATCH || r.v.pdispVal == nullptr) {
    return {};
  }
  IDispatch* p = r.v.pdispVal;
  p->AddRef();
  return Disp(p);
}

QString getString(IDispatch* obj, const wchar_t* name) {
  Var r;
  if(FAILED(invoke(obj, name, DISPATCH_PROPERTYGET, &r.v)) || FAILED(VariantChangeType(&r.v, &r.v, 0, VT_BSTR)) || r.v.bstrVal == nullptr) {
    return {};
  }
  return QString::fromWCharArray(r.v.bstrVal, static_cast<qsizetype>(SysStringLen(r.v.bstrVal)));
}

bool getBool(IDispatch* obj, const wchar_t* name) {
  Var r;
  if(FAILED(invoke(obj, name, DISPATCH_PROPERTYGET, &r.v)) || FAILED(VariantChangeType(&r.v, &r.v, 0, VT_BOOL))) {
    return false;
  }
  return r.v.boolVal != VARIANT_FALSE;
}

int getInt(IDispatch* obj, const wchar_t* name) {
  Var r;
  if(FAILED(invoke(obj, name, DISPATCH_PROPERTYGET, &r.v)) || FAILED(VariantChangeType(&r.v, &r.v, 0, VT_I4))) {
    return -1;
  }
  return static_cast<int>(r.v.lVal);
}

// Outlook's DATE is local wall-clock time.
QDateTime getDate(IDispatch* obj, const wchar_t* name) {
  Var r;
  if(FAILED(invoke(obj, name, DISPATCH_PROPERTYGET, &r.v)) || FAILED(VariantChangeType(&r.v, &r.v, 0, VT_DATE))) {
    return {};
  }
  SYSTEMTIME st{};
  if(VariantTimeToSystemTime(r.v.date, &st) == 0) {
    return {};
  }
  return QDateTime(QDate(st.wYear, st.wMonth, st.wDay), QTime(st.wHour, st.wMinute, st.wSecond));
}

// Items.Restrict compares dates written the way the user's locale writes
// them — the documented form; an ISO string matches nothing on most locales.
QString restrictDate(const QDateTime& dt) {
  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(dt.date().year());
  st.wMonth = static_cast<WORD>(dt.date().month());
  st.wDay = static_cast<WORD>(dt.date().day());
  st.wHour = static_cast<WORD>(dt.time().hour());
  st.wMinute = static_cast<WORD>(dt.time().minute());
  wchar_t date[80] = {};
  wchar_t time[80] = {};
  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 80, nullptr);
  GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time, 80);
  return QString::fromWCharArray(date) + QLatin1Char(' ') + QString::fromWCharArray(time);
}

struct Bstr {
  VARIANT v;

  explicit Bstr(const QString& s) {
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(reinterpret_cast<const wchar_t*>(s.utf16()));
  }

  ~Bstr() {
    VariantClear(&v);
  }

  Bstr(const Bstr&) = delete;
  Bstr& operator=(const Bstr&) = delete;
};

VARIANT intVar(int i) {
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_I4;
  v.lVal = i;
  return v;
}

VARIANT boolVar(bool b) {
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_BOOL;
  v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
  return v;
}

constexpr int kOlFolderCalendar = 9;
constexpr int kOlMeetingCanceled = 5;
constexpr int kOlMeetingReceivedAndCanceled = 7;
// A runaway range (a daily series with no end) stops here rather than
// enumerating for minutes.
constexpr int kMaxItems = 5000;

}  // namespace

bool outlookDesktopAvailable() {
  const ComScope com;
  CLSID clsid{};
  return SUCCEEDED(CLSIDFromProgID(L"Outlook.Application", &clsid));
}

OutlookRead readOutlookCalendar(const QDateTime& from, const QDateTime& to) {
  OutlookRead out;
  const ComScope com;
  if(FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE) {
    out.error = QStringLiteral("COM ") + hrText(com.hr);
    return out;
  }
  CLSID clsid{};
  if(FAILED(CLSIDFromProgID(L"Outlook.Application", &clsid))) {
    out.error = QStringLiteral("Outlook is not installed");
    return out;
  }
  IDispatch* raw = nullptr;
  // A running Outlook is the one this returns; otherwise it starts one in
  // the background, which exits again once released.
  const HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, reinterpret_cast<void**>(&raw));
  if(FAILED(hr) || raw == nullptr) {
    out.error = QStringLiteral("Outlook did not answer (%1)").arg(hrText(hr));
    return out;
  }
  const Disp app(raw);

  const Bstr mapi(QStringLiteral("MAPI"));
  const Disp ns = getObject(app.get(), L"GetNamespace", DISPATCH_METHOD, {&mapi.v});
  const VARIANT calendarKind = intVar(kOlFolderCalendar);
  const Disp folder = getObject(ns.get(), L"GetDefaultFolder", DISPATCH_METHOD, {&calendarKind});
  const Disp items = getObject(folder.get(), L"Items");
  if(!items) {
    out.error = QStringLiteral("Outlook has no calendar to read");
    return out;
  }
  // Sort first, then expand series: the order Outlook documents, without
  // which IncludeRecurrences returns the masters only.
  const Bstr byStart(QStringLiteral("[Start]"));
  invoke(items.get(), L"Sort", DISPATCH_METHOD, nullptr, {&byStart.v});
  const VARIANT yes = boolVar(true);
  invoke(items.get(), L"IncludeRecurrences", DISPATCH_PROPERTYPUT, nullptr, {&yes});

  const Bstr filter(QStringLiteral("[Start] < '%1' AND [End] > '%2'").arg(restrictDate(to), restrictDate(from)));
  const Disp found = getObject(items.get(), L"Restrict", DISPATCH_METHOD, {&filter.v});
  if(!found) {
    out.error = QStringLiteral("Outlook refused the date filter");
    return out;
  }

  Disp item = getObject(found.get(), L"GetFirst", DISPATCH_METHOD);
  while(item && out.items.size() < kMaxItems) {
    const int status = getInt(item.get(), L"MeetingStatus");
    if(status != kOlMeetingCanceled && status != kOlMeetingReceivedAndCanceled) {
      OutlookItem it;
      it.subject = getString(item.get(), L"Subject");
      it.start = getDate(item.get(), L"Start");
      it.end = getDate(item.get(), L"End");
      it.allDay = getBool(item.get(), L"AllDayEvent");
      it.location = getString(item.get(), L"Location");
      it.body = getString(item.get(), L"Body");
      QString gid = getString(item.get(), L"GlobalAppointmentID");
      if(gid.isEmpty()) {
        gid = getString(item.get(), L"EntryID");
      }
      if(!gid.isEmpty() && it.start.isValid()) {
        it.key = gid.right(40) + QLatin1Char('-') + it.start.toString(QStringLiteral("yyyyMMddHHmm"));
        out.items.append(it);
      }
    }
    item = getObject(found.get(), L"GetNext", DISPATCH_METHOD);
  }
  out.ok = true;
  return out;
}

// ── Watching for changes (OutlookWatch) ──

namespace {

// The dispinterfaces Outlook raises its events on (msoutl.olb):
// ItemsEvents (ItemAdd 0xF001, ItemChange 0xF002, ItemRemove 0xF003) and
// ApplicationEvents_11 (Quit 0xF007).
constexpr GUID kDiidItemsEvents = {0x00063077, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kDiidApplicationEvents11 = {0x0006300E, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr DISPID kDispidQuit = 0xF007;
// How often a running Outlook is looked for, and an attached one checked.
constexpr int kWatchPollMs = 30 * 1000;

// The smallest sink Outlook accepts: an IDispatch that answers for one
// dispinterface and hands every call to `onEvent`. The arguments (the item
// that changed) are Outlook's; nothing here reads them.
class EventSink final : public IDispatch {
 public:
  EventSink(const GUID& iid, std::function<void(DISPID)> onEvent) : m_iid(iid), m_onEvent(std::move(onEvent)) {
  }

  EventSink(const EventSink&) = delete;
  EventSink& operator=(const EventSink&) = delete;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if(out == nullptr) {
      return E_POINTER;
    }
    if(IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch) || IsEqualIID(riid, m_iid)) {
      *out = static_cast<IDispatch*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&m_refs));
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const LONG left = InterlockedDecrement(&m_refs);
    if(left == 0) {
      delete this;
    }
    return static_cast<ULONG>(left);
  }

  HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override {
    if(count != nullptr) {
      *count = 0;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT /*index*/, LCID /*lcid*/, ITypeInfo** info) override {
    if(info != nullptr) {
      *info = nullptr;
    }
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID /*riid*/, LPOLESTR* /*names*/, UINT /*count*/, LCID /*lcid*/, DISPID* /*ids*/) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE Invoke(DISPID id,
                                   REFIID /*riid*/,
                                   LCID /*lcid*/,
                                   WORD /*flags*/,
                                   DISPPARAMS* /*params*/,
                                   VARIANT* result,
                                   EXCEPINFO* /*excep*/,
                                   UINT* /*argErr*/) override {
    if(result != nullptr) {
      VariantInit(result);
    }
    if(m_onEvent) {
      m_onEvent(id);
    }
    return S_OK;
  }

  // A call already on its way after the unsubscribe is ignored.
  void disarm() {
    m_onEvent = nullptr;
  }

 private:
  ~EventSink() = default;

  LONG m_refs = 1;
  GUID m_iid;
  std::function<void(DISPID)> m_onEvent;
};

// One Advise on an object's connection point, undone by reset().
class Advice {
 public:
  Advice() = default;

  ~Advice() {
    reset();
  }

  Advice(const Advice&) = delete;
  Advice& operator=(const Advice&) = delete;

  bool advise(IDispatch* source, const GUID& iid, IDispatch* sink) {
    reset();
    if(source == nullptr) {
      return false;
    }
    IConnectionPointContainer* container = nullptr;
    if(FAILED(source->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&container))) || container == nullptr) {
      return false;
    }
    const HRESULT found = container->FindConnectionPoint(iid, &m_point);
    container->Release();
    if(FAILED(found) || m_point == nullptr) {
      m_point = nullptr;
      return false;
    }
    if(FAILED(m_point->Advise(sink, &m_cookie))) {
      m_point->Release();
      m_point = nullptr;
      return false;
    }
    return true;
  }

  void reset() {
    if(m_point != nullptr) {
      static_cast<void>(m_point->Unadvise(m_cookie));
      m_point->Release();
      m_point = nullptr;
      m_cookie = 0;
    }
  }

 private:
  IConnectionPoint* m_point = nullptr;
  DWORD m_cookie = 0;
};

// The windows Outlook has open; -1 when it no longer answers. A headless
// Outlook (started by a read, or by another program's automation) has none.
int explorerCount(IDispatch* app) {
  const Disp explorers = getObject(app, L"Explorers");
  return explorers ? getInt(explorers.get(), L"Count") : -1;
}

// The watch itself, living on the watch thread with COM initialised there.
class Watch {
 public:
  Watch(OutlookWatch* owner, QObject* context) : m_owner(owner), m_context(context) {
  }

  ~Watch() {
    detach();
  }

  Watch(const Watch&) = delete;
  Watch& operator=(const Watch&) = delete;

  void check() {
    if(m_app) {
      if(explorerCount(m_app.get()) > 0) {
        return;
      }
      detach();  // closed, crashed, or only a headless copy left
    }
    attach();
  }

  void detach() {
    m_itemsAdvice.reset();
    m_appAdvice.reset();
    releaseSink(m_itemsSink);
    releaseSink(m_appSink);
    m_items.reset();
    m_folder.reset();
    m_ns.reset();
    m_app.reset();
    if(m_live) {
      m_live = false;
      emit m_owner->liveChanged(false);
    }
  }

 private:
  static void releaseSink(EventSink*& sink) {
    if(sink != nullptr) {
      sink->disarm();
      sink->Release();
      sink = nullptr;
    }
  }

  void attach() {
    CLSID clsid{};
    if(FAILED(CLSIDFromProgID(L"Outlook.Application", &clsid))) {
      return;
    }
    // Only an Outlook that is already running: GetActiveObject never starts
    // one, where CoCreateInstance would.
    IUnknown* unknown = nullptr;
    if(FAILED(GetActiveObject(clsid, nullptr, &unknown)) || unknown == nullptr) {
      return;
    }
    IDispatch* raw = nullptr;
    const HRESULT hr = unknown->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&raw));
    unknown->Release();
    if(FAILED(hr) || raw == nullptr) {
      return;
    }
    Disp app(raw);
    // Holding on to a headless Outlook would keep it running for good.
    if(explorerCount(app.get()) < 1) {
      return;
    }
    const Bstr mapi(QStringLiteral("MAPI"));
    Disp ns = getObject(app.get(), L"GetNamespace", DISPATCH_METHOD, {&mapi.v});
    const VARIANT calendarKind = intVar(kOlFolderCalendar);
    Disp folder = getObject(ns.get(), L"GetDefaultFolder", DISPATCH_METHOD, {&calendarKind});
    // The Items object raises the events only while someone holds it.
    Disp items = getObject(folder.get(), L"Items");
    if(!items) {
      return;
    }
    m_itemsSink = new EventSink(kDiidItemsEvents, [this](DISPID /*id*/) {
      emit m_owner->calendarChanged();
    });
    if(!m_itemsAdvice.advise(items.get(), kDiidItemsEvents, m_itemsSink)) {
      releaseSink(m_itemsSink);
      return;
    }
    // Quit lets go at once, so Outlook is never kept from closing; without
    // it the half-minute check notices.
    m_appSink = new EventSink(kDiidApplicationEvents11, [this](DISPID id) {
      if(id == kDispidQuit) {
        QTimer::singleShot(0, m_context, [this]() {
          detach();
        });
      }
    });
    if(!m_appAdvice.advise(app.get(), kDiidApplicationEvents11, m_appSink)) {
      releaseSink(m_appSink);
    }
    m_app = std::move(app);
    m_ns = std::move(ns);
    m_folder = std::move(folder);
    m_items = std::move(items);
    m_live = true;
    emit m_owner->liveChanged(true);
    // Whatever changed while nobody listened (Outlook just started and is
    // still syncing, or was restarted) is read once now.
    emit m_owner->calendarChanged();
  }

  OutlookWatch* m_owner;
  QObject* m_context;
  Disp m_app;
  Disp m_ns;
  Disp m_folder;
  Disp m_items;
  EventSink* m_itemsSink = nullptr;
  EventSink* m_appSink = nullptr;
  Advice m_itemsAdvice;
  Advice m_appAdvice;
  bool m_live = false;
};

}  // namespace

void OutlookWatch::run() {
  // Single-threaded COM, as Outlook's object model expects; this thread's
  // event loop is the message pump its events arrive through.
  const ComScope com;
  if(FAILED(com.hr)) {
    return;
  }
  QTimer poll;
  poll.setInterval(kWatchPollMs);
  Watch watch(this, &poll);
  QObject::connect(&poll, &QTimer::timeout, &poll, [&watch]() {
    watch.check();
  });
  watch.check();
  poll.start();
  if(!isInterruptionRequested()) {
    exec();
  }
  poll.stop();
  watch.detach();
}

}  // namespace heap::cal
