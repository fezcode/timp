// smtc.c — Windows System Media Transport Controls (the OS "now playing" session).
//
// Why this exists: without an SMTC session Windows has no idea Timp is a media
// app. Nothing shows in the volume/media flyout, and the keyboard's transport
// keys go to whichever app *did* register one — which is why Timp previously had
// to grab them with a global low-level hook, stealing (and doubling up on) keys
// that belonged to whatever else was playing. Registering here fixes both: the
// shell shows the track, and the OS routes the transport keys to us only while
// we're the session it considers current.
//
// Everything WinRT lives on one dedicated MTA thread. The UI thread only ever
// touches the small "pending" struct below under a critical section, so no COM
// object is ever touched from two apartments.
#ifdef _WIN32

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <asyncinfo.h>
#include <windows.foundation.h>
#include <windows.storage.streams.h>
#include <windows.media.h>
#include <systemmediatransportcontrolsinterop.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "smtc.h"
#include "mediakeys.h"
#include "art.h"

// ---------- short names for the widl C-ABI mouthfuls ----------
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControls                 Smtc;
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControls2                Smtc2;
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControlsDisplayUpdater   Updater;
typedef __x_ABI_CWindows_CMedia_CIMusicDisplayProperties                       MusicProps;
typedef __x_ABI_CWindows_CMedia_CIMusicDisplayProperties2                      MusicProps2;
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControlsTimelineProperties TimelineProps;
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControlsButtonPressedEventArgs BtnArgs;
typedef __x_ABI_CWindows_CMedia_CSystemMediaTransportControlsButton            BtnId;
typedef __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs     BtnSink;
typedef __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgsVtbl BtnSinkVtbl;
typedef __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference        StreamRef;
typedef __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReferenceStatics StreamRefStatics;
typedef __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream                 RandomStream;
typedef __x_ABI_CWindows_CStorage_CStreams_CIOutputStream                       OutputStream;
typedef __x_ABI_CWindows_CStorage_CStreams_CIDataWriter                         DataWriter;
typedef __x_ABI_CWindows_CStorage_CStreams_CIDataWriterFactory                  DataWriterFactory;
typedef __FIAsyncOperation_1_UINT32                                            AsyncUInt32;

// Spelled out locally so this file needs neither INITGUID nor -luuid.
static const GUID kIID_IUnknown      = { 0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46} };
static const GUID kIID_AsyncInfo     = { 0x00000036,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46} };
static const GUID kIID_IAgileObject  = { 0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90} };
static const GUID kIID_Interop       = { 0xddb0472d,0xc911,0x4a1f,{0x86,0xd9,0xdc,0x3d,0x71,0xa9,0x5f,0x5a} };
static const GUID kIID_Smtc          = { 0x99fa3ff4,0x1742,0x42a6,{0x90,0x2e,0x08,0x7d,0x41,0xf9,0x65,0xec} };
static const GUID kIID_Smtc2         = { 0xea98d2f6,0x7f3c,0x4af2,{0xa5,0x86,0x72,0x88,0x98,0x08,0xef,0xb1} };
static const GUID kIID_TimelineProps = { 0x5125316a,0xc3a2,0x475b,{0x85,0x07,0x93,0x53,0x4d,0xc8,0x8f,0x15} };
static const GUID kIID_MusicProps2   = { 0x00368462,0x97d3,0x44b9,{0xb0,0x0f,0x00,0x8a,0xfc,0xef,0xaf,0x18} };
static const GUID kIID_BtnSink       = { 0x0557e996,0x7b23,0x5bae,{0xaa,0x81,0xea,0x0d,0x67,0x11,0x43,0xa4} };
static const GUID kIID_StreamRefStat = { 0x857309dc,0x3fbf,0x4e7d,{0x98,0x6f,0xef,0x3b,0x1a,0x07,0xa9,0x64} };
static const GUID kIID_RandomStream  = { 0x905a0fe1,0xbc53,0x11df,{0x8c,0x49,0x00,0x1e,0x4f,0xc6,0x86,0xda} };
static const GUID kIID_WriterFactory = { 0x338c67c2,0x8b84,0x4c2b,{0x9c,0x50,0x7b,0x87,0x67,0x84,0x7a,0x1f} };

// ---------- combase, resolved at run time ----------
// Loading these by hand (rather than linking -lruntimeobject) keeps timp.exe
// launchable on a Windows without WinRT: we just fall back to the key hook.
typedef HRESULT (WINAPI *PFnRoInitialize)(RO_INIT_TYPE);
typedef void    (WINAPI *PFnRoUninitialize)(void);
typedef HRESULT (WINAPI *PFnRoGetActivationFactory)(HSTRING, REFIID, void **);
typedef HRESULT (WINAPI *PFnRoActivateInstance)(HSTRING, IInspectable **);
typedef HRESULT (WINAPI *PFnWindowsCreateString)(const WCHAR *, UINT32, HSTRING *);
typedef HRESULT (WINAPI *PFnWindowsDeleteString)(HSTRING);

static PFnRoInitialize           p_RoInitialize;
static PFnRoUninitialize         p_RoUninitialize;
static PFnRoGetActivationFactory p_RoGetActivationFactory;
static PFnRoActivateInstance     p_RoActivateInstance;
static PFnWindowsCreateString    p_WindowsCreateString;
static PFnWindowsDeleteString    p_WindowsDeleteString;

// GetProcAddress hands back a generic FARPROC; the hop through void(*)(void) is
// the sanctioned way to retype it without tripping -Wcast-function-type.
#define GETFN(type, mod, name) ((type)(void (*)(void))GetProcAddress((mod), (name)))

static bool load_combase(void) {
    if (p_RoInitialize) return true;
    HMODULE m = LoadLibraryW(L"combase.dll");
    if (!m) return false;
    p_RoGetActivationFactory = GETFN(PFnRoGetActivationFactory, m, "RoGetActivationFactory");
    p_RoActivateInstance     = GETFN(PFnRoActivateInstance,     m, "RoActivateInstance");
    p_RoUninitialize         = GETFN(PFnRoUninitialize,         m, "RoUninitialize");
    p_WindowsCreateString    = GETFN(PFnWindowsCreateString,    m, "WindowsCreateString");
    p_WindowsDeleteString    = GETFN(PFnWindowsDeleteString,    m, "WindowsDeleteString");
    PFnRoInitialize init     = GETFN(PFnRoInitialize,           m, "RoInitialize");
    if (!init || !p_RoGetActivationFactory || !p_RoActivateInstance ||
        !p_WindowsCreateString || !p_WindowsDeleteString || !p_RoUninitialize) return false;
    p_RoInitialize = init;
    return true;
}

#define HS(lit, out) p_WindowsCreateString((lit), (UINT32)(sizeof(lit) / sizeof(WCHAR) - 1), (out))

// UTF-8 → HSTRING. Empty/NULL yields a NULL HSTRING, which WinRT reads as "".
static HRESULT hstring_utf8(const char *s, HSTRING *out) {
    *out = NULL;
    if (!s || !*s) return S_OK;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 1) return S_OK;
    WCHAR *w = (WCHAR *)malloc(sizeof(WCHAR) * (size_t)n);
    if (!w) return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    HRESULT hr = p_WindowsCreateString(w, (UINT32)(n - 1), out);
    free(w);
    return hr;
}

// ---------- ButtonPressed sink ----------
// A hand-rolled COM delegate: SMTC calls Invoke on an RPC thread, so all we do
// there is drop the action in a slot the UI thread polls.
typedef struct { const BtnSinkVtbl *lpVtbl; LONG ref; } Sink;

static volatile LONG g_action = MK_NONE;

static HRESULT STDMETHODCALLTYPE sink_qi(BtnSink *self, REFIID riid, void **out) {
    if (!out) return E_POINTER;
    if (IsEqualGUID(riid, &kIID_IUnknown) || IsEqualGUID(riid, &kIID_BtnSink) ||
        IsEqualGUID(riid, &kIID_IAgileObject)) {          // agile → no marshalling stub needed
        *out = self;
        InterlockedIncrement(&((Sink *)self)->ref);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sink_addref(BtnSink *self) {
    return (ULONG)InterlockedIncrement(&((Sink *)self)->ref);
}
static ULONG STDMETHODCALLTYPE sink_release(BtnSink *self) {
    LONG n = InterlockedDecrement(&((Sink *)self)->ref);
    if (n == 0) free(self);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE sink_invoke(BtnSink *self, Smtc *sender, BtnArgs *args) {
    (void)self; (void)sender;
    BtnId b;
    if (!args || FAILED(args->lpVtbl->get_Button(args, &b))) return S_OK;
    LONG act = MK_NONE;
    switch (b) {
        case SystemMediaTransportControlsButton_Play:     act = MK_PLAY;  break;
        case SystemMediaTransportControlsButton_Pause:    act = MK_PAUSE; break;
        case SystemMediaTransportControlsButton_Stop:     act = MK_STOP;  break;
        case SystemMediaTransportControlsButton_Next:     act = MK_NEXT;  break;
        case SystemMediaTransportControlsButton_Previous: act = MK_PREV;  break;
        default: break;
    }
    if (act != MK_NONE) InterlockedExchange(&g_action, act);
    return S_OK;
}
static const BtnSinkVtbl kSinkVtbl = { sink_qi, sink_addref, sink_release, sink_invoke };

// ---------- what the UI thread asks for ----------
static struct {
    CRITICAL_SECTION cs;
    HANDLE  wake, ready, thread;
    volatile LONG quit;
    bool    live;              // set by the worker before signalling `ready`

    bool    meta_dirty, state_dirty;
    char    title[256], artist[256], album[256], audio[1024];
    int     state;
    double  pos, dur;
} g;

// ---------- WinRT objects (worker thread only) ----------
static Smtc          *s_ctrl;
static Smtc2         *s_ctrl2;
static Updater       *s_upd;
static TimelineProps *s_line;
static Sink          *s_sink;
static EventRegistrationToken s_token;
static DataWriterFactory *s_writer_factory;
static StreamRefStatics  *s_stream_statics;

static bool   s_has_track;
static double s_pushed_pos = -1e9, s_pushed_dur = -1;

// ---------- artwork ----------
// The shell wants a stream it can open on its own later. A file:// URI would be
// the obvious way to point it at one, but RandomAccessStreamReference resolves
// only http/https/ms-appx/ms-appdata — so the picture goes into an in-memory
// stream instead, which also spares us a temp file to write, name and clean up.
static bool read_whole_file(const char *utf8_path, unsigned char **data, int *size) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, NULL, 0);
    if (n <= 0) return false;
    WCHAR *w = (WCHAR *)malloc(sizeof(WCHAR) * (size_t)n);
    if (!w) return false;
    MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, w, n);
    FILE *f = _wfopen(w, L"rb");
    free(w);
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 32L * 1024 * 1024) { fclose(f); return false; }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return false; }
    bool ok = fread(buf, 1, (size_t)sz, f) == (size_t)sz;
    fclose(f);
    if (!ok) { free(buf); return false; }
    *data = buf; *size = (int)sz;
    return true;
}

// The one async call in this file, and it stores into memory — so spinning on
// the status beats dragging in completion-handler boilerplate.
static void await_store(AsyncUInt32 *op) {
    IAsyncInfo *info = NULL;
    if (FAILED(op->lpVtbl->QueryInterface(op, &kIID_AsyncInfo, (void **)&info)) || !info) return;
    for (int i = 0; i < 400; i++) {
        AsyncStatus st = Started;
        if (FAILED(info->lpVtbl->get_Status(info, &st)) || st != Started) break;
        Sleep(5);
    }
    info->lpVtbl->Release(info);
}

static RandomStream *new_memory_stream(void) {
    HSTRING cls = NULL;
    if (FAILED(HS(L"Windows.Storage.Streams.InMemoryRandomAccessStream", &cls))) return NULL;
    IInspectable *ins = NULL;
    HRESULT hr = p_RoActivateInstance(cls, &ins);
    p_WindowsDeleteString(cls);
    if (FAILED(hr) || !ins) return NULL;
    RandomStream *st = NULL;
    ins->lpVtbl->QueryInterface(ins, &kIID_RandomStream, (void **)&st);
    ins->lpVtbl->Release(ins);
    return st;
}

static StreamRef *art_stream_ref(const unsigned char *data, int size) {
    if (!s_stream_statics || !s_writer_factory) return NULL;
    RandomStream *stream = new_memory_stream();
    if (!stream) return NULL;

    OutputStream *out = NULL;
    if (SUCCEEDED(stream->lpVtbl->GetOutputStreamAt(stream, 0, &out)) && out) {
        DataWriter *w = NULL;
        if (SUCCEEDED(s_writer_factory->lpVtbl->CreateDataWriter(s_writer_factory, out, &w)) && w) {
            w->lpVtbl->WriteBytes(w, (UINT32)size, (BYTE *)data);
            AsyncUInt32 *op = NULL;
            if (SUCCEEDED(w->lpVtbl->StoreAsync(w, &op)) && op) {
                await_store(op);
                op->lpVtbl->Release(op);
            }
            // Hand the stream back before the writer goes away, or releasing the
            // writer closes it out from under the reference we're about to make.
            OutputStream *detached = NULL;
            w->lpVtbl->DetachStream(w, &detached);
            if (detached) detached->lpVtbl->Release(detached);
            w->lpVtbl->Release(w);
        }
        out->lpVtbl->Release(out);
    }
    stream->lpVtbl->Seek(stream, 0);

    StreamRef *ref = NULL;
    s_stream_statics->lpVtbl->CreateFromStream(s_stream_statics, stream, &ref);
    stream->lpVtbl->Release(stream);
    return ref;
}

static void publish_art(const char *audio_path) {
    unsigned char *data = NULL;
    int size = 0;
    if (audio_path && *audio_path && !art_load_encoded(audio_path, &data, &size)) {
        char cover[1024];
        if (art_find_dir_cover(audio_path, cover, (int)sizeof(cover)))
            read_whole_file(cover, &data, &size);
    }
    if (!data) {                       // no artwork → make sure the old one goes away
        s_upd->lpVtbl->put_Thumbnail(s_upd, NULL);
        return;
    }
    StreamRef *ref = art_stream_ref(data, size);
    free(data);
    if (!ref) return;
    s_upd->lpVtbl->put_Thumbnail(s_upd, ref);
    ref->lpVtbl->Release(ref);
}

// ---------- worker-thread work ----------
static void apply_meta(const char *title, const char *artist, const char *album, const char *audio) {
    if (!s_upd) return;
    s_upd->lpVtbl->ClearAll(s_upd);
    s_has_track = title && *title;
    if (!s_has_track) { s_upd->lpVtbl->Update(s_upd); return; }

    s_upd->lpVtbl->put_Type(s_upd, MediaPlaybackType_Music);

    // Fetched per update rather than cached: ClearAll swaps the property object
    // out from under a cached pointer, and writes to the stale one go nowhere.
    MusicProps *music = NULL;
    s_upd->lpVtbl->get_MusicProperties(s_upd, &music);
    if (music) {
        HSTRING h = NULL;
        if (SUCCEEDED(hstring_utf8(title, &h)))  { music->lpVtbl->put_Title(music, h);  p_WindowsDeleteString(h); }
        if (SUCCEEDED(hstring_utf8(artist, &h))) { music->lpVtbl->put_Artist(music, h); p_WindowsDeleteString(h); }
        MusicProps2 *music2 = NULL;
        music->lpVtbl->QueryInterface(music, &kIID_MusicProps2, (void **)&music2);
        if (music2) {
            if (SUCCEEDED(hstring_utf8(album, &h))) { music2->lpVtbl->put_AlbumTitle(music2, h); p_WindowsDeleteString(h); }
            music2->lpVtbl->Release(music2);
        }
        music->lpVtbl->Release(music);
    }
    publish_art(audio);
    s_upd->lpVtbl->Update(s_upd);
}

static void apply_state(int state) {
    if (!s_ctrl) return;
    // Stopped with nothing loaded is "Closed": that drops Timp out of the flyout
    // instead of parking an empty entry there.
    __x_ABI_CWindows_CMedia_CMediaPlaybackStatus st =
        (state == MK_PLAYING) ? MediaPlaybackStatus_Playing :
        (state == MK_PAUSED)  ? MediaPlaybackStatus_Paused  :
        s_has_track           ? MediaPlaybackStatus_Stopped : MediaPlaybackStatus_Closed;
    s_ctrl->lpVtbl->put_PlaybackStatus(s_ctrl, st);
    if (s_ctrl2) s_ctrl2->lpVtbl->put_PlaybackRate(s_ctrl2, state == MK_PLAYING ? 1.0 : 0.0);
}

static void apply_timeline(double pos, double dur) {
    if (!s_ctrl2 || !s_line) return;
    __x_ABI_CWindows_CFoundation_CTimeSpan zero = { 0 }, end, at;
    end.Duration = (INT64)(dur * 10000000.0);
    at.Duration  = (INT64)(pos * 10000000.0);
    s_line->lpVtbl->put_StartTime(s_line, zero);
    s_line->lpVtbl->put_MinSeekTime(s_line, zero);
    s_line->lpVtbl->put_EndTime(s_line, end);
    s_line->lpVtbl->put_MaxSeekTime(s_line, end);
    s_line->lpVtbl->put_Position(s_line, at);
    s_ctrl2->lpVtbl->UpdateTimelineProperties(s_ctrl2, s_line);
}

static bool create_controls(HWND hwnd) {
    HSTRING cls = NULL;
    if (FAILED(HS(L"Windows.Media.SystemMediaTransportControls", &cls))) return false;
    ISystemMediaTransportControlsInterop *interop = NULL;
    HRESULT hr = p_RoGetActivationFactory(cls, &kIID_Interop, (void **)&interop);
    p_WindowsDeleteString(cls);
    if (FAILED(hr) || !interop) return false;

    hr = interop->lpVtbl->GetForWindow(interop, hwnd, &kIID_Smtc, (void **)&s_ctrl);
    interop->lpVtbl->Release(interop);
    if (FAILED(hr) || !s_ctrl) return false;

    s_ctrl->lpVtbl->QueryInterface(s_ctrl, &kIID_Smtc2, (void **)&s_ctrl2);
    s_ctrl->lpVtbl->get_DisplayUpdater(s_ctrl, &s_upd);

    // Which buttons the shell offers. Next/Previous stay lit even on a one-track
    // queue — the handlers no-op, and greying them out mid-track just flickers.
    s_ctrl->lpVtbl->put_IsPlayEnabled(s_ctrl, TRUE);
    s_ctrl->lpVtbl->put_IsPauseEnabled(s_ctrl, TRUE);
    s_ctrl->lpVtbl->put_IsStopEnabled(s_ctrl, TRUE);
    s_ctrl->lpVtbl->put_IsNextEnabled(s_ctrl, TRUE);
    s_ctrl->lpVtbl->put_IsPreviousEnabled(s_ctrl, TRUE);
    s_ctrl->lpVtbl->put_IsEnabled(s_ctrl, TRUE);

    s_sink = (Sink *)calloc(1, sizeof(Sink));
    if (!s_sink) return false;
    s_sink->lpVtbl = &kSinkVtbl;
    s_sink->ref = 1;
    if (FAILED(s_ctrl->lpVtbl->add_ButtonPressed(s_ctrl, (BtnSink *)s_sink, &s_token))) return false;

    // Optional extras — a missing one only costs us the scrubber or the artwork.
    if (SUCCEEDED(HS(L"Windows.Media.SystemMediaTransportControlsTimelineProperties", &cls))) {
        IInspectable *ins = NULL;
        if (SUCCEEDED(p_RoActivateInstance(cls, &ins)) && ins) {
            ins->lpVtbl->QueryInterface(ins, &kIID_TimelineProps, (void **)&s_line);
            ins->lpVtbl->Release(ins);
        }
        p_WindowsDeleteString(cls);
    }
    if (SUCCEEDED(HS(L"Windows.Storage.Streams.DataWriter", &cls))) {
        p_RoGetActivationFactory(cls, &kIID_WriterFactory, (void **)&s_writer_factory);
        p_WindowsDeleteString(cls);
    }
    if (SUCCEEDED(HS(L"Windows.Storage.Streams.RandomAccessStreamReference", &cls))) {
        p_RoGetActivationFactory(cls, &kIID_StreamRefStat, (void **)&s_stream_statics);
        p_WindowsDeleteString(cls);
    }
    return true;
}

static void destroy_controls(void) {
    if (s_ctrl) {
        s_ctrl->lpVtbl->put_IsEnabled(s_ctrl, FALSE);
        s_ctrl->lpVtbl->remove_ButtonPressed(s_ctrl, s_token);
    }
    if (s_stream_statics) s_stream_statics->lpVtbl->Release(s_stream_statics);
    if (s_writer_factory) s_writer_factory->lpVtbl->Release(s_writer_factory);
    if (s_line)           s_line->lpVtbl->Release(s_line);
    if (s_upd)            s_upd->lpVtbl->Release(s_upd);
    if (s_ctrl2)          s_ctrl2->lpVtbl->Release(s_ctrl2);
    if (s_ctrl)           s_ctrl->lpVtbl->Release(s_ctrl);
    if (s_sink)           ((BtnSink *)s_sink)->lpVtbl->Release((BtnSink *)s_sink);
    s_stream_statics = NULL; s_writer_factory = NULL; s_line = NULL;
    s_upd = NULL; s_ctrl2 = NULL; s_ctrl = NULL; s_sink = NULL;
}

static void pump_pending(void) {
    char title[256], artist[256], album[256], audio[1024];
    int  state;
    double pos, dur;
    bool meta, st;

    EnterCriticalSection(&g.cs);
    meta = g.meta_dirty; st = g.state_dirty;
    g.meta_dirty = g.state_dirty = false;
    memcpy(title, g.title, sizeof title); memcpy(artist, g.artist, sizeof artist);
    memcpy(album, g.album, sizeof album); memcpy(audio, g.audio, sizeof audio);
    state = g.state; pos = g.pos; dur = g.dur;
    LeaveCriticalSection(&g.cs);

    if (meta) { apply_meta(title, artist, album, audio); s_pushed_pos = -1e9; }
    if (meta || st) apply_state(state);   // s_has_track may have just flipped
    // The timeline has no dirty flag: it moves continuously, so it rides the
    // worker's 1s tick and only goes out when it actually shifted.
    if (dur > 0 && (dur != s_pushed_dur || fabs(pos - s_pushed_pos) > 0.35)) {
        apply_timeline(pos, dur);
        s_pushed_pos = pos; s_pushed_dur = dur;
    }
}

static DWORD WINAPI smtc_thread(LPVOID param) {
    HWND hwnd = (HWND)param;
    HRESULT hr = p_RoInitialize(RO_INIT_MULTITHREADED);
    bool inited = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    if (inited && create_controls(hwnd)) g.live = true;
    SetEvent(g.ready);
    if (!g.live) {
        destroy_controls();
        if (SUCCEEDED(hr)) p_RoUninitialize();
        return 0;
    }

    while (!InterlockedCompareExchange(&g.quit, 0, 0)) {
        WaitForSingleObject(g.wake, 1000);
        pump_pending();
    }
    destroy_controls();
    if (SUCCEEDED(hr)) p_RoUninitialize();
    return 0;
}

// Windows labels our media session by resolving the process AppUserModelID to a
// registered app; with nothing registered for ours it just prints the raw id
// ("Fezcode.Timp"). This is the documented desktop-app registration that gives
// the id a real display name. The id is read back from the process rather than
// spelled out again here, so it cannot drift from the one main() sets. The key
// is listed in forge.toml [uninstall].registry_keys so uninstall removes it.
__declspec(dllimport) HRESULT __stdcall GetCurrentProcessExplicitAppUserModelID(PWSTR *app_id);

static void register_display_name(void) {
    PWSTR id = NULL;
    if (FAILED(GetCurrentProcessExplicitAppUserModelID(&id)) || !id) return;
    WCHAR key[512];
    if (_snwprintf(key, 512, L"Software\\Classes\\AppUserModelId\\%ls", id) > 0) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
            static const WCHAR shown[] = L"Timp";
            RegSetValueExW(k, L"DisplayName", 0, REG_SZ, (const BYTE *)shown, sizeof shown);
            RegCloseKey(k);
        }
    }
    CoTaskMemFree(id);
}

// ---------- public API (UI thread) ----------
bool smtc_start(void *hwnd) {
    if (g.thread) return g.live;
    if (!hwnd || !load_combase()) return false;
    register_display_name();

    InitializeCriticalSection(&g.cs);
    g.wake  = CreateEventW(NULL, FALSE, FALSE, NULL);
    g.ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g.wake || !g.ready) return false;

    g.thread = CreateThread(NULL, 0, smtc_thread, hwnd, 0, NULL);
    if (!g.thread) return false;
    // Block briefly: the caller needs the verdict to decide whether to install
    // the legacy key hook instead.
    WaitForSingleObject(g.ready, 5000);
    if (!g.live) { smtc_stop(); return false; }
    return true;
}

void smtc_stop(void) {
    if (g.thread) {
        InterlockedExchange(&g.quit, 1);
        if (g.wake) SetEvent(g.wake);
        WaitForSingleObject(g.thread, 3000);
        CloseHandle(g.thread);
        g.thread = NULL;
    }
    if (g.wake)  { CloseHandle(g.wake);  g.wake = NULL; }
    if (g.ready) { CloseHandle(g.ready); g.ready = NULL; }
    g.live = false;
}

void smtc_now_playing(const char *title, const char *artist, const char *album, const char *audio_path) {
    if (!g.live) return;
    EnterCriticalSection(&g.cs);
    snprintf(g.title,  sizeof g.title,  "%s", title  ? title  : "");
    snprintf(g.artist, sizeof g.artist, "%s", artist ? artist : "");
    snprintf(g.album,  sizeof g.album,  "%s", album  ? album  : "");
    snprintf(g.audio,  sizeof g.audio,  "%s", audio_path ? audio_path : "");
    g.meta_dirty = true;
    LeaveCriticalSection(&g.cs);
    SetEvent(g.wake);
}

void smtc_set_state(int state) {
    if (!g.live) return;
    bool changed;
    EnterCriticalSection(&g.cs);
    changed = (state != g.state);
    g.state = state;
    if (changed) g.state_dirty = true;
    LeaveCriticalSection(&g.cs);
    if (changed) SetEvent(g.wake);
}

void smtc_set_timeline(double position, double duration) {
    if (!g.live) return;
    EnterCriticalSection(&g.cs);
    g.pos = position; g.dur = duration;
    LeaveCriticalSection(&g.cs);
}

int smtc_poll(void) { return (int)InterlockedExchange(&g_action, MK_NONE); }

#endif /* _WIN32 */
