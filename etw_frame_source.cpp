// ============================================================================
//  StutterScope — ETW kare kaynagi
// ============================================================================
#ifdef _WIN32

#include "etw_frame_source.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define INITGUID          // DxgKrnl GUID'ini burada TANIMLIYORUZ, sadece bildirmiyoruz
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace ss {
namespace {

// ----------------------------------------------------------------------------
//  Microsoft-Windows-DxgKrnl
//  {802EC45A-1E99-4B83-9920-87C98277BA9D}
// ----------------------------------------------------------------------------
const GUID kDxgKrnlGuid = {
    0x802ec45a, 0x1e99, 0x4b83, {0x99, 0x20, 0x87, 0xc9, 0x82, 0x77, 0xba, 0x9d}
};

// DxgKrnl anahtar sozcukleri (manifestten). Present ve PresentHistory bize
// yeter; digerlerini acmak olay hacmini gereksiz buyutur ve olay dusurur.
constexpr ULONGLONG kKeywordPresent = 0x1;

// Present olaylarini ayirt etmek icin manifest Task degeri.
// DxgKrnl manifestinde Present task'i 10'dur; opcode'lar start/stop ayrimini
// yapar. Task numarasi Windows surumleri arasinda sabit kalmistir, ama yine de
// dogrulamayi olay ADINDAN da yapiyoruz (asagiya bakin) — tek bir sabite
// guvenmek kirilgan olurdu.
constexpr USHORT kTaskPresent = 10;

// ----------------------------------------------------------------------------
//  Yakalama durumu
// ----------------------------------------------------------------------------
struct CaptureState {
    TRACEHANDLE  sessionHandle = 0;
    TRACEHANDLE  consumerHandle = INVALID_PROCESSTRACE_HANDLE;
    std::string  sessionName;

    uint32_t     targetPid = 0;

    // Surec basina son Present zaman damgasi (100ns birimi)
    std::map<uint32_t, uint64_t> lastPresentQpc;

    // ONEMLI: kareler SUREC BASINA ayri tutulur.
    // Onceki surumde hepsi tek listeye yaziliyordu; DWM, tarayici ve oyun ayni
    // akista karisinca kare sureleri ic ice geciyor ve FPS tamamen anlamsiz
    // cikiyordu (masaustunde 10 saniyede 13.641 "kare" gibi). Artik her surecin
    // kendi serisi var, en cok kare ureten surec oyun kabul edilip o seri
    // motora veriliyor.
    std::map<uint32_t, std::vector<FrameSample>> framesByPid;

    uint64_t     rawEvents  = 0;

    // QPC -> mikrosaniye cevirimi icin
    uint64_t     qpcFrequency = 10'000'000;  // varsayilan: 100ns tik
    uint64_t     firstQpc     = 0;
};

CaptureState*     g_state = nullptr;
std::atomic<bool> g_stopRequested{false};

// ----------------------------------------------------------------------------
//  Yardimcilar
// ----------------------------------------------------------------------------
std::string win32Error(ULONG code) {
    char* msg = nullptr;
    const DWORD n = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&msg), 0, nullptr);
    std::string out = "hata " + std::to_string(code);
    if (n > 0 && msg) {
        std::string text(msg, n);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        out += ": " + text;
    }
    if (msg) LocalFree(msg);
    return out;
}

// EVENT_TRACE_PROPERTIES degisken boyutlu: yapinin ardindan oturum adi gelir.
std::vector<uint8_t> makeSessionProperties(const std::string& name) {
    const size_t bufSize = sizeof(EVENT_TRACE_PROPERTIES) + (name.size() + 1) * 2 + 2;
    std::vector<uint8_t> buf(bufSize, 0);

    auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
    p->Wnode.BufferSize    = static_cast<ULONG>(bufSize);
    p->Wnode.Flags         = WNODE_FLAG_TRACED_GUID;
    p->Wnode.ClientContext = 1;                      // QPC zaman damgasi
    p->LogFileMode         = EVENT_TRACE_REAL_TIME_MODE;
    p->LoggerNameOffset    = sizeof(EVENT_TRACE_PROPERTIES);

    // Present olaylari hizli akar; kucuk tampon = dusen olay.
    p->BufferSize          = 128;   // KB
    p->MinimumBuffers      = 16;
    p->MaximumBuffers      = 64;
    p->FlushTimer          = 1;

    return buf;
}

// Olayin gercekten bir "Present" olayi olup olmadigini iki yoldan dogrular:
// once ucuz olan Task karsilastirmasi, sonra gerekirse TDH ile olay adi.
// Tek bir sabite guvenmemek icin: manifest task numaralari Windows surumleri
// arasinda degisirse ad kontrolu yakalar.
bool isPresentEvent(const EVENT_RECORD* rec) {
    if (rec->EventHeader.EventDescriptor.Task == kTaskPresent) return true;

    // Task eslesmedi — adi sor. Bu yol pahalidir, bu yuzden sonucu
    // olay ID'sine gore onbellekleriz.
    static std::map<USHORT, bool> cache;
    const USHORT id = rec->EventHeader.EventDescriptor.Id;
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;

    ULONG size = 0;
    bool  result = false;
    if (TdhGetEventInformation(const_cast<EVENT_RECORD*>(rec), 0, nullptr,
                               nullptr, &size) == ERROR_INSUFFICIENT_BUFFER) {
        std::vector<uint8_t> buf(size);
        auto* info = reinterpret_cast<TRACE_EVENT_INFO*>(buf.data());
        if (TdhGetEventInformation(const_cast<EVENT_RECORD*>(rec), 0, nullptr,
                                   info, &size) == ERROR_SUCCESS &&
            info->TaskNameOffset != 0) {
            const wchar_t* task =
                reinterpret_cast<const wchar_t*>(buf.data() + info->TaskNameOffset);
            result = (wcsstr(task, L"Present") != nullptr);
        }
    }
    cache[id] = result;
    return result;
}

// ----------------------------------------------------------------------------
//  Olay geri cagirimi
// ----------------------------------------------------------------------------
void WINAPI onEvent(EVENT_RECORD* rec) {
    if (!g_state || g_stopRequested.load(std::memory_order_relaxed)) return;
    if (!IsEqualGUID(rec->EventHeader.ProviderId, kDxgKrnlGuid)) return;

    // Yalnizca "baslangic" tarafini sayiyoruz; her karede hem start hem stop
    // gelirse kare sayisi ikiye katlanir.
    const UCHAR op = rec->EventHeader.EventDescriptor.Opcode;
    if (op != EVENT_TRACE_TYPE_START && op != EVENT_TRACE_TYPE_INFO) return;

    if (!isPresentEvent(rec)) return;

    const uint32_t pid = rec->EventHeader.ProcessId;
    if (g_state->targetPid != 0 && pid != g_state->targetPid) return;

    const uint64_t qpc = static_cast<uint64_t>(rec->EventHeader.TimeStamp.QuadPart);
    if (g_state->firstQpc == 0) g_state->firstQpc = qpc;

    ++g_state->rawEvents;

    auto it = g_state->lastPresentQpc.find(pid);
    if (it != g_state->lastPresentQpc.end() && qpc > it->second) {
        const uint64_t deltaTicks = qpc - it->second;
        const double   deltaMs =
            (static_cast<double>(deltaTicks) * 1000.0) /
            static_cast<double>(g_state->qpcFrequency);

        // 0'dan buyuk ve 10 sn'den kisa olmayan degerler cop; core.cpp'deki
        // CSV okuyucusu ile ayni eleme.
        if (deltaMs > 0.0 && deltaMs < 10000.0) {
            FrameSample s;
            s.frameTimeMs = deltaMs;
            s.timestampUs = ((qpc - g_state->firstQpc) * 1'000'000ull) /
                            g_state->qpcFrequency;
            g_state->framesByPid[pid].push_back(s);
        }
    }
    g_state->lastPresentQpc[pid] = qpc;
}

ULONG WINAPI onBuffer(EVENT_TRACE_LOGFILEA* log) {
    if (log) {
        // ProcessTrace her tampon sonunda burayi cagirir; durdurma isteginin
        // gorulecegi tek nokta budur.
    }
    return g_stopRequested.load(std::memory_order_relaxed) ? FALSE : TRUE;
}

// Onceki calistirmadan kalmis yetim oturumu kapatir. Bu YAPILMAZSA ikinci
// calistirma ERROR_ALREADY_EXISTS ile patlar ve kullanici sebebini anlamaz.
void killOrphanSession(const std::string& name) {
    auto buf = makeSessionProperties(name);
    ControlTraceA(0, name.c_str(),
                  reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data()),
                  EVENT_TRACE_CONTROL_STOP);
}

} // namespace

// ============================================================================
bool isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                                        sizeof(elevation), &size) != 0;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

void stopEtwCapture() {
    g_stopRequested.store(true, std::memory_order_relaxed);
}

// ============================================================================
EtwCaptureResult runEtwCapture(const EtwCaptureOptions& opts) {
    EtwCaptureResult result;
    result.info.kind = "etw";

    if (!isElevated()) {
        result.error =
            "Gercek zamanli ETW oturumu acmak icin yonetici hakki gerekiyor. "
            "Programi 'Yonetici olarak calistir' ile baslatin.";
        return result;
    }

    CaptureState state;
    state.sessionName = opts.sessionName;
    state.targetPid   = opts.processId;

    LARGE_INTEGER freq{};
    if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0)
        state.qpcFrequency = static_cast<uint64_t>(freq.QuadPart);

    g_state = &state;
    g_stopRequested.store(false, std::memory_order_relaxed);

    // --- 1) Varsa yetim oturumu kapat, sonra yenisini ac ---
    killOrphanSession(opts.sessionName);

    auto props = makeSessionProperties(opts.sessionName);
    ULONG rc = StartTraceA(&state.sessionHandle, opts.sessionName.c_str(),
                           reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()));
    if (rc != ERROR_SUCCESS) {
        result.error = "ETW oturumu acilamadi (" + win32Error(rc) + ")";
        g_state = nullptr;
        return result;
    }

    // --- 2) DxgKrnl saglayicisini ac ---
    rc = EnableTraceEx2(state.sessionHandle, &kDxgKrnlGuid,
                        EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                        TRACE_LEVEL_INFORMATION,
                        kKeywordPresent, 0, 0, nullptr);
    if (rc != ERROR_SUCCESS) {
        result.error = "DxgKrnl saglayicisi acilamadi (" + win32Error(rc) + ")";
        ControlTraceA(state.sessionHandle, nullptr,
                      reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()),
                      EVENT_TRACE_CONTROL_STOP);
        g_state = nullptr;
        return result;
    }

    // --- 3) Tuketiciyi ac ---
    EVENT_TRACE_LOGFILEA log{};
    log.LoggerName          = const_cast<char*>(opts.sessionName.c_str());
    log.ProcessTraceMode    = PROCESS_TRACE_MODE_REAL_TIME |
                              PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = onEvent;
    log.BufferCallback      = onBuffer;

    state.consumerHandle = OpenTraceA(&log);
    if (state.consumerHandle == INVALID_PROCESSTRACE_HANDLE) {
        result.error = "ETW tuketicisi acilamadi (" +
                       win32Error(GetLastError()) + ")";
        ControlTraceA(state.sessionHandle, nullptr,
                      reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()),
                      EVENT_TRACE_CONTROL_STOP);
        g_state = nullptr;
        return result;
    }

    // --- 4) Sureli yakalama icin zamanlayici ipligi ---
    HANDLE timerThread = nullptr;
    if (opts.seconds > 0) {
        struct TimerArg { uint32_t sec; };
        static uint32_t s_seconds;
        s_seconds = opts.seconds;
        timerThread = CreateThread(nullptr, 0,
            [](LPVOID) -> DWORD {
                const uint32_t total = s_seconds;
                for (uint32_t i = 0; i < total * 10; ++i) {
                    if (g_stopRequested.load(std::memory_order_relaxed)) return 0;
                    Sleep(100);
                }
                stopEtwCapture();
                return 0;
            }, nullptr, 0, nullptr);
    }

    // --- 5) Olaylari isle (bloklar) ---
    rc = ProcessTrace(&state.consumerHandle, 1, nullptr, nullptr);
    if (rc != ERROR_SUCCESS && rc != ERROR_CANCELLED) {
        result.error = "ETW olaylari islenemedi (" + win32Error(rc) + ")";
    }

    // --- 6) Temizlik: SIRA ONEMLI ---
    // Once tuketici kapanir, sonra oturum durur. Ters sirada yaparsan
    // ProcessTrace hala aktif tampona bakarken oturum yok olur.
    g_stopRequested.store(true, std::memory_order_relaxed);
    if (timerThread) {
        WaitForSingleObject(timerThread, 2000);
        CloseHandle(timerThread);
    }
    CloseTrace(state.consumerHandle);

    auto stopProps = makeSessionProperties(opts.sessionName);
    auto* sp = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(stopProps.data());
    ControlTraceA(state.sessionHandle, nullptr, sp, EVENT_TRACE_CONTROL_STOP);
    result.lostEvents = sp->EventsLost;

    // --- 7) Sonucu topla ---
    result.rawEvents = state.rawEvents;

    // Zaman tabani disari veriliyor: DPC toplayicisi ayri bir oturumda
    // calisiyor ve iki tarafi hizalamadan takilma-DPC eslesmesi yapilamaz.
    result.firstQpc  = state.firstQpc;
    result.qpcFreq   = state.qpcFrequency;

    // Hangi surecin kareleri motora gidecek?
    // Kullanici PID verdiyse o; vermediyse EN COK KARE URETEN surec.
    // Butun surecleri birlestirmek YANLIS olur: DWM saniyede 60, oyun 144,
    // tarayici 30 kare uretir; hepsi tek listede toplanirsa ortaya cikan
    // "kare suresi" hicbir uygulamaya ait olmayan bir kurgudur.
    uint32_t chosen = state.targetPid;
    if (chosen == 0 && !state.framesByPid.empty()) {
        auto top = std::max_element(
            state.framesByPid.begin(), state.framesByPid.end(),
            [](const auto& a, const auto& b) {
                return a.second.size() < b.second.size();
            });
        chosen = top->first;
    }

    size_t otherProcesses = 0, otherFrames = 0;
    for (const auto& kv : state.framesByPid) {
        if (kv.first == chosen) continue;
        ++otherProcesses;
        otherFrames += kv.second.size();
    }

    auto pick = state.framesByPid.find(chosen);
    if (pick != state.framesByPid.end()) result.frames = std::move(pick->second);
    result.info.processId = chosen;

    result.info.note = std::to_string(result.frames.size()) + " kare (surec " +
                       std::to_string(chosen) + "), " +
                       std::to_string(result.rawEvents) + " Present olayi";
    if (otherProcesses > 0) {
        result.info.note += "; " + std::to_string(otherProcesses) +
                            " diger surecin " + std::to_string(otherFrames) +
                            " karesi analiz disi birakildi";
    }
    if (result.lostEvents > 0) {
        result.info.note += ", " + std::to_string(result.lostEvents) +
                            " olay dusuruldu (tampon kucuk)";
    }

    if (result.frames.empty() && result.error.empty()) {
        // NOT: Masaustu bos dururken de olay AKAR (DWM surekli sunum yapar;
        // olculdu: 10 saniyede ~13.700 Present olayi). Bu yuzden hic olay
        // gelmemesi normal bir durum degil, gercek bir sorun isaretidir.
        result.error = "Hic Present olayi gelmedi. DxgKrnl saglayicisi ya "
                       "engellenmis ya da baska bir oturum tarafindan "
                       "kilitlenmis olabilir. 'logman query -ets' ile acik "
                       "oturumlari kontrol edin.";
    }

    g_state = nullptr;
    return result;
}

} // namespace ss

#endif // _WIN32
