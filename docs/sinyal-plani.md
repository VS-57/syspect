# Syspect — sinyal toplama uygulama planı

> Beş paralel araştırma ajanının bulgularının ve bunları çürütmeye çalışan
> bağımsız değerlendirmelerin sentezi (2026-07-26). Workflow: wf_cacbc3e4-a88.
> Hiçbir madde bu makinede test edilmeden koda alınmamalı; belgenin sonundaki
> "DOĞRULANMASI GEREKEN" bölümü bunu listeliyor.


# StutterScope v0.2 — Tek Uygulama Planı

> Bu plan beş araştırma bulgusunun ve bunlara karşı yazılan bağımsız değerlendirmelerin **birleştirilmiş, çürütülmüş iddialardan arındırılmış** halidir. Çekirdek sürücüsü gerektiren hiçbir adım yoktur. Belirsiz kalan her nokta §9'da ayrıca listelenmiştir.
>
> Plan yazılmadan önce `C:\dev\stutterscope` deposu okundu; aşağıdaki tüm dosya/satır referansları **doğrulanmıştır**. Değerlendirmelerin kendi içindeki üç olgu hatası §10.3'te düzeltilmiştir.

---

## 0. Yönetici özeti ve öncelik sırası

**En kritik bulgu, beş konudan hiçbiri değil: ürünün sinyal boru hattı hiç bağlı değil.**

`SignalSnapshot` üretim yolunda **hiçbir zaman doldurulmuyor**. Tek dedektör çağrı noktası:

```cpp
// frame_source.cpp:218
if (auto e = det.push(f, {})) r.events.push_back(*e);
//                        ^^ her zaman boş
```

`grep`, `SignalSnapshot`'ın yalnızca `core.cpp` (tüketici) ve `test_core.cpp` (test kurgusu) içinde geçtiğini gösterdi. Yani **KURAL 1 (DPC), 4 (VRAM), 5 (shader), 7 (hard fault), 8 (disk), 9, 10, 11, 12 bugüne kadar üretimde bir kez bile ateşlemedi.** DPC/disk/bellek kodu yazmak, bu kanal açılmadan hiçbir vakayı çözmez.

### Öncelik tablosu — hangi sinyal kaç forum vakası çözer?

| Öncelik | İş kalemi | Yönetici? | ETW slotu? | Risk | Çözdüğü vaka | Tahmini efor |
|---|---|---|---|---|---|---|
| **P0** | Sinyal kanalı + saat tabanı + durdurma yolu | — | — | Orta (mevcut kodu değiştirir) | **Hiçbiri tek başına, 5/5'in ön koşulu** | 2–3 gün |
| **P1** | Bellek Katman A (`NtQuerySystemInformation` + `GetPerformanceInfo`) | **Hayır** | Hayır | Düşük | **1**, kısmen 5 | 1 gün |
| **P2** | Depolama envanteri + **ters teşhis** (NVMe SMART, HDD tespiti, boş alan) | **Hayır** | Hayır | Düşük | **4, 5** | 1–1.5 gün |
| **P3** | ETW sistem logger yükseltmesi → `DISK_IO` + `MEMORY_HARD_FAULTS` + `THREAD` | Evet (zaten var) | 1 slot | Orta-Yüksek | **4, 5**, 1'i doğrular | 3–4 gün |
| **P4** | DPC + sürücü adres tablosu + **kontrol grubu** | Evet | (aynı slot) | **En yüksek** | **3** | 3–4 gün |
| — | Laptop güç limiti | — | — | — | **2 — HİÇBİR BULGU KAPSAMIYOR** | §9.7 |

**Stratejik karar:** DPC teknik olarak en ayrıntılı araştırılan konu, ama **en sona bırakılıyor**. Gerekçe: tek vaka (3) çözüyor, en yüksek olay hacmine sahip (ürünün birincil sinyali olan Present'i düşürme riski), ve kontrol grubu olmadan her takılmaya bir `.sys` yapıştırıp KURAL 1'i sahte %80'e oturtuyor — tasarım kuralı 3'ün doğrudan ihlali.

Buna karşılık **P1 + P2 birlikte, yönetici hakkı ve ETW olmadan, 5 vakanın 3'üne dokunuyor** ve toplam ~2.5 gün.

---

## 1. Mimari kararlar (ve elenen alternatifler)

### K1 — TEK ETW oturumu, TEK `ProcessTrace`

`ProcessTrace` tek çağrıda **yalnızca bir gerçek zamanlı handle** kabul eder; birden fazlası `ERROR_INVALID_PARAMETER` döner. "64 handle + kronolojik birleştirme" iddiası **çürütüldü** — 64 limiti yalnızca dosya (.etl) oturumları içindir.

Dolayısıyla iki seçenek kalıyor:

| | Tek oturum (SYSTEM_LOGGER_MODE + DxgKrnl) | İki oturum, iki iplik |
|---|---|---|
| Slot maliyeti | 1 | 2 (6 kullanılabilir slottan) |
| `g_state` / `isPresentEvent` static cache | Değişmez, tek iplik | **Veri yarışı** — state ayrılmalı |
| Zaman hizalama | Doğal | `ClientContext=1` + `RAW_TIMESTAMP` ile yine sorunsuz |
| Kanıt | PerfView `TraceEventSession.cs` bunu yapıyor | Kesin çalışır |

**Karar: tek oturum birincil, iki oturum hazır B planı.** Tek oturumun bu makinede test edilmemiş olması §9.1'de listelenmiştir; B planına düşmenin maliyeti düşüktür (`ss_ui.cpp:944` zaten detached thread kullanıyor).

> **Çürütülen:** "Çekirdek olayları DxgKrnl oturumuna EKLENEMEZ, iki oturum zorunlu." Bu kısıt **NT Kernel Logger'a özgüdür**. Win8+ özel adlı sistem logger'ları çoklanabilir ve manifest sağlayıcı taşıyabilir. Plana alınmadı.

### K2 — Klasik `EnableFlags` birincil, System Provider'lar opsiyonel

System Provider'lar (`SystemInterruptProviderGuid` + `EnableTraceEx2`) **Windows 10 SDK build 20348+** gerektirir. Hedef kitlenin büyük kısmı Win10 22H2 = build **19045**. "Win8+" ve "Win11 22H2+" nitelemelerinin **ikisi de yanlıştı**.

→ `EVENT_TRACE_PROPERTIES.EnableFlags` yolu **tek yol**. System Provider yolu v0.2'ye hiç girmiyor (kod ikizi bakım yükü, sıfır kazanç).

### K3 — Olaylar HER ZAMAN klasik MOF GUID'i taşır

MS dokümanı: *"the outputted events are not marked as being emitted from the individual system providers."* Callback'te `ProviderId`'yi System Provider GUID'leriyle karşılaştıran her kod **hiçbir şey yakalamaz** (session bulgusundaki kod örneği bu hatayı içeriyordu — plana alınmadı).

Filtrelenecek GUID'ler:

| Sinyal | MOF Task GUID | Opcode |
|---|---|---|
| DPC | `{ce1dbfb4-137e-4da6-87b0-3f59aa102cbc}` (PerfInfo) | 66, 68, 69 |
| Disk | `{3d6fa8d4-fe05-11d0-9dda-00c04fd7ba7c}` (DiskIo) | 10=Read, 11=Write |
| Hard fault | `{3d6fa8d3-fe05-11d0-9dda-00c04fd7ba7c}` (PageFault) | 32 |
| Thread (TID→PID) | `{3d6fa8d1-fe05-11d0-9dda-00c04fd7ba7c}` | 1,2,3,4 |
| Image (opsiyonel) | `{2cb15d1d-5fc1-11d2-abe1-00a0c911f518}` | 10,2,3,4 |

### K4 — Filtreleme GİRİŞTE, korelasyon ÇIKIŞTA

Callback ProcessTrace ipliğinde ve DPC hızında (20–100k/sn) koşar. Orada `std::map`, string kopyası, `TdhGetEventInformation` **yasak**. Sadece eşik üstü kayıt, önceden `reserve` edilmiş ring'e. Adres→ad çözümlemesi, pencere eşleştirmesi, sıralama: `ProcessTrace` döndükten **sonra**.

### K5 — `core.h` Windows'a bağımlı olamaz, ama DEĞİŞTİRİLEBİLİR

Tasarım kuralı 5 `core.h`'nin Windows API'sine bağımlı olmamasını söylüyor; değiştirilemeyeceğini değil. Düz skaler alan eklemek serbesttir ve §7.1'de gerekli.

---

## 2. FAZ P0 — Boru hattı (ön koşul, atlanamaz)

### P0.1 — `core.h`: ölçüldü/ölçülmedi ayrımı

`SignalSnapshot`'ta `0` hem "ölçtüm, yoktu" hem "hiç ölçemedim" demek. `kUnknownMinute` sentineli aynı gerekçeyle zaten var; aynısını sistem sinyalleri için yap.

```cpp
// core.h — SignalSnapshot içine, minutesSinceGameStart'ın hemen üstüne
    // Sistem sinyali kanali (DPC/disk/hard fault) bu olay icin GERCEKTEN
    // acildi mi? false ise dpcMaxMs/diskWaitMs/hardFaults'un 0 olmasi bir
    // OLCUM DEGIL, olcum yoklugudur. kUnknownMinute ile ayni gerekce.
    bool systemSignalsMeasured = false;
```

```cpp
// core.h — SystemInfo icine, telemetri blogunun altina
    // Oturum boyunca cekirdek ETW sinyalleri toplanabildi mi?
    bool etwSystemSignalsAvailable = false;

    // --- Bellek (Katman A, 1 Hz ornekleme; OTURUM seviyesi, olay seviyesi DEGIL)
    static constexpr uint64_t kUnknownBytes = 0;   // 0 = bilinmiyor (bayt icin gecerli)
    uint64_t commitPeakBytes        = kUnknownBytes;
    uint64_t commitLimitBytes       = kUnknownBytes;
    uint64_t physicalTotalBytes     = kUnknownBytes;
    uint64_t physicalAvailMinBytes  = kUnknownBytes;  // oturumdaki EN DUSUK
    uint64_t gameCommitPeakBytes    = kUnknownBytes;
    uint64_t gameNotResidentPeakBytes = kUnknownBytes;
    double   gameHardFaultPeakPerSec = -1.0;          // -1 = bilinmiyor
    bool     pagefileDisabled        = false;

    // --- Depolama envanteri (P2)
    enum class Tri { Unknown, No, Yes };
    Tri  gameDriveRotational   = Tri::Unknown;   // false varsayma! bkz. §4
    double gameDriveFreeRatio  = -1.0;
    bool gameDriveReliabilityDegraded = false;
    bool gameDriveSpareBelowThreshold = false;
    int  gameDriveWearPercentUsed     = -1;      // SADECE ENVANTER, teshise girmez
    bool userSuspectsDriveHealth      = false;   // ters teshis tetikleyicisi
```

> `Tri` enum'u `bool`'a tercih edildi: `IncursSeekPenalty` sorgusu başarısızsa `false` döndürmek "SSD" demektir ve en güçlü depolama sinyalini sessizce susturur.

### P0.2 — `etw_frame_source.h`: mutlak saat ve sinyal taşıyıcı

`EtwCaptureResult` bugün (satır 46–54) ne mutlak QPC tabanı ne de frekans taşıyor. `FrameSample.timestampUs` ise `firstQpc`'ye **göreli** (satır 167, 183) ve `firstQpc` `CaptureState`'te, `runEtwCapture`'ın yığın yerelinde (satır 239) — dönüşte yok oluyor. Pencere eşleştirmesi bu haliyle **imkânsız**.

```cpp
// etw_frame_source.h
namespace ss {

struct EtwClock {
    uint64_t qpcFrequency  = 0;      // 0 = bilinmiyor
    uint64_t baseQpc       = 0;      // timestampUs'un mutlak QPC karsiligi
    bool     rawTimestamps = false;  // PROCESS_TRACE_MODE_RAW_TIMESTAMP acik mi
    bool ok() const { return qpcFrequency > 0 && baseQpc > 0; }
};

struct DpcRecord      { uint64_t startQpc, endQpc, routine; uint16_t cpu; };
struct DiskRecord     { uint64_t startQpc, endQpc; uint32_t tid, pid, bytes, irpFlags;
                        uint8_t  diskNumber; bool isWrite; };
struct HardFaultRecord{ uint64_t startQpc, endQpc; uint32_t tid, pid, bytes; };

struct EtwCaptureResult {
    std::vector<FrameSample> frames;
    // frames ile AYNI INDEKS — o karenin Present anininin MUTLAK QPC'si.
    // core.h'ye dokunmadan mutlak zamana donmenin yolu budur.
    std::vector<uint64_t>    framePresentQpc;

    EtwClock                     clock;
    std::vector<DpcRecord>       dpcs;
    std::vector<DiskRecord>      disks;
    std::vector<HardFaultRecord> faults;
    bool                         signalsTruncated = false;  // ring tasti
    bool                         systemSignalsOn  = false;

    FrameSourceInfo info;
    std::string     error;
    uint64_t rawEvents = 0;
    uint64_t lostEvents = 0;            // EventsLost
    uint64_t realTimeBuffersLost = 0;   // RealTimeBuffersLost  <-- YENI
    uint64_t logBuffersLost = 0;        // LogBuffersLost       <-- YENI
    bool reliable() const { return lostEvents == 0 && realTimeBuffersLost == 0
                                && logBuffersLost == 0; }
    bool ok() const { return error.empty(); }
};
} // namespace ss
```

### P0.3 — `etw_frame_source.cpp`: RAW_TIMESTAMP + frekans kaynağı

**Mevcut kodda canlı (maskelenmiş) hata:** tüketici `PROCESS_TRACE_MODE_RAW_TIMESTAMP` kullanmıyor (satır 279–280), yani `EventHeader.TimeStamp` **100 ns FILETIME**; ama kod `QueryPerformanceFrequency`'ye bölüyor (satır 244–245, 176). Modern PC'de QPF = 10.000.000 olduğu için hata görünmüyor. `bcdedit /set useplatformclock true` (forumlarda yaygın "takılma çözümü") QPF'yi 3.579.545'e çeker → kare süreleri ~2.8× şişer, `StutterKind`'ın 50/500 ms mutlak eşikleri ve `thresholdFor`'daki +8 ms terimi kayar.

**Bu düzeltme, DPC planı tamamen reddedilse bile tek başına yapılmaya değer.**

```cpp
// makeSessionProperties() icinde — mevcut satirlarin YERINE
    // Oturumun KENDI kontrol GUID'i. MS: "Make sure Wnode.Guid is not set to
    // SystemTraceControlGuid. You must assign a new GUID to this member."
    static const GUID kSsSessionGuid =
        {0x7b5c2a10,0x1f3d,0x4a88,{0x9c,0x21,0x0e,0x77,0x4d,0x55,0xab,0x01}};
    p->Wnode.Guid          = kSsSessionGuid;
    p->Wnode.ClientContext = 1;                       // QPC — zaten boyleydi
    p->LogFileMode         = EVENT_TRACE_REAL_TIME_MODE;
    // NOT: EVENT_TRACE_USE_PAGED_MEMORY ASLA eklenmemeli — sistem logger'lari
    // sayfalanmis bellege olay yazamaz. (Bugun set edilmiyor, boyle kalsin.)
```

```cpp
// runEtwCapture — 3) Tuketiciyi ac
    log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME
                         | PROCESS_TRACE_MODE_EVENT_RECORD
                         | PROCESS_TRACE_MODE_RAW_TIMESTAMP;   // <-- SART
```

Frekans: `LogfileHeader.PerfFreq` **otoriter kaynak, ama gerçek zamanlı oturumda dolduğu garanti değil.** Sıfır gelirse `deltaMs = inf` olur ve `deltaMs < 10000.0` guard'ı (satır 180) TÜM kareleri eler → kullanıcı satır 380'deki "Hiç Present olayı gelmedi" mesajını görür, tamamen yanlış teşhis. Bu yüzden:

```cpp
// OpenTraceA'dan HEMEN SONRA, ProcessTrace'ten ONCE
uint64_t freq = 0;
if (log.LogfileHeader.PerfFreq.QuadPart > 0)
    freq = static_cast<uint64_t>(log.LogfileHeader.PerfFreq.QuadPart);

LARGE_INTEGER qpf{};
if (freq == 0 || freq < 1'000'000ull || freq > 1'000'000'000ull) {
    // PerfFreq okunamadi ya da sacma. RAW_TIMESTAMP + ClientContext=1 varken
    // canli oturumda QPF zaten dogru cevaptir.
    if (QueryPerformanceFrequency(&qpf) && qpf.QuadPart > 0)
        freq = static_cast<uint64_t>(qpf.QuadPart);
}
if (freq == 0) { result.error = "QPC frekansi okunamadi"; /* temizle, don */ }
state.qpcFrequency  = freq;
state.clock.qpcFrequency  = freq;
state.clock.rawTimestamps = true;
```

### P0.4 — Present delta'sını çevrimdışı hesapla

Bugün delta callback içinde hesaplanıyor ve satır 188 (`lastPresentQpc[pid] = qpc`) her olayda tabanı güncelliyor. ETW gerçek zamanlı teslimde **CPU başına tampon** kullanır ve global sıra garantisi yoktur; yüksek hacimli ikinci bir sağlayıcı eklenince sırasız gelen eski bir olay tabanı geriye çeker → bir sonraki kare şişer → motora **hayalî takılma** gider.

```cpp
// CaptureState icine
struct RawPresent { uint64_t qpc; uint32_t pid; };
std::vector<RawPresent> presents;      // baslangicta reserve(1<<20)

// onEvent icinde, delta mantiginin YERINE (satir 171-188 gider):
    st->presents.push_back({qpc, pid});
```

```cpp
// ProcessTrace DONDUKTEN SONRA (yeni yardimci fonksiyon)
void buildFrames(CaptureState& st, EtwCaptureResult& out) {
    std::stable_sort(st.presents.begin(), st.presents.end(),
                     [](const RawPresent& a, const RawPresent& b){ return a.qpc < b.qpc; });

    std::map<uint32_t, uint64_t> last;
    std::map<uint32_t, std::vector<std::pair<FrameSample,uint64_t>>> byPid;

    // TABAN saglayicidan BAGIMSIZ: ilk gelen olay. DPC/disk olaylari Present'ten
    // once akmaya baslar; taban ilk Present'e sabitlenirse (qpc - base) unsigned
    // underflow verir ve birlestirme sessizce bos doner.
    const uint64_t base = st.presents.empty() ? st.firstAnyQpc : st.presents.front().qpc;

    for (const auto& p : st.presents) {
        auto it = last.find(p.pid);
        if (it != last.end() && p.qpc > it->second) {
            const double ms = (double)(p.qpc - it->second) * 1000.0 / (double)st.qpcFrequency;
            if (ms > 0.0 && ms < 10000.0) {
                FrameSample s;
                s.frameTimeMs = ms;
                s.timestampUs = ((p.qpc - base) * 1'000'000ull) / st.qpcFrequency;
                byPid[p.pid].push_back({s, p.qpc});     // MUTLAK QPC de saklanir
            }
        }
        last[p.pid] = p.qpc;
    }
    // ... en cok kare ureten pid secilir (mevcut mantik, satir 342-350) ...
    // secilen pid icin: out.frames + out.framePresentQpc paralel doldurulur
    out.clock.baseQpc = base;
}
```

### P0.5 — Durdurma: `ControlTrace(STOP)` başka iplikten

Bugünkü tasarımda durdurma yalnızca `BufferCallback`'in `FALSE` dönmesiyle çalışıyor (satır 191–197). **Olay akmıyorsa buffer callback çağrılmaz → `ProcessTrace` asılı kalır.** Sınırsız modda (`kDurations[3] == 0`, `ss_ui.cpp:92`) tek çıkış yolu budur.

Ayrıca temizlik sırası (satır 319–331) `CloseTrace` → `ControlTrace(STOP)`. Rundown olayları STOP ile üretildiği için, ileride `IMAGE_LOAD` kullanılırsa o olaylar **hiç işlenmez**.

```cpp
// Zamanlayici/durdurucu iplik — timerThread'in YERINE
HANDLE stopper = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
    const uint32_t total = s_seconds;                  // 0 = sinirsiz
    for (uint32_t i = 0; total == 0 || i < total * 10; ++i) {
        if (g_stopRequested.load(std::memory_order_relaxed)) break;
        Sleep(100);
    }
    g_stopRequested.store(true, std::memory_order_relaxed);

    // ONCE sayaclari oku (STOP'tan sonra oturum yok olur), SONRA durdur.
    auto q = makeSessionProperties(g_sessionName);
    auto* qp = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(q.data());
    if (ControlTraceA(g_sessionHandle, nullptr, qp, EVENT_TRACE_CONTROL_QUERY)
        == ERROR_SUCCESS) {
        g_lostEvents          = qp->EventsLost;
        g_realTimeBuffersLost = qp->RealTimeBuffersLost;
        g_logBuffersLost      = qp->LogBuffersLost;
    }
    auto s = makeSessionProperties(g_sessionName);
    ControlTraceA(g_sessionHandle, nullptr,
                  reinterpret_cast<EVENT_TRACE_PROPERTIES*>(s.data()),
                  EVENT_TRACE_CONTROL_STOP);   // ProcessTrace bunun uzerine doner
    return 0;
}, nullptr, 0, nullptr);

// ... ProcessTrace(...) bloklar, oturum durunca tamponlari bosaltip doner ...
WaitForSingleObject(stopper, 5000);
CloseHandle(stopper);
CloseTrace(state.consumerHandle);      // ARTIK EN SONDA
```

`stopEtwCapture()` (satır 223) aynı kalır — bayrağı çevirir, durdurucu iplik gerisini yapar. `ss_ui.cpp:910` ve `ss_ui.cpp:1291` çağrı noktaları değişmez.

> **Not:** İki oturumlu B planına geçilirse `stopEtwCapture()` `ss::stopAllCaptures()` haline getirilmeli; `g_stopRequested` dosya-yereli bir atomik (satır 73) ve ikinci bir `.cpp` kendi kopyasını alır.

### P0.6 — Birleştirme fonksiyonu ve çağrı noktaları

`analyzeSource` bugün tespit ve teşhisi tek geçişte yapıyor (`frame_source.cpp:208–224`). Çevrimdışı korelasyon tanımı gereği tespitten sonra olur → teşhis **yeniden** çağrılmalı.

```cpp
// frame_source.h
struct AnalysisResult { /* mevcut */ };

// Sinyaller sonradan doldurulduktan sonra teshisi tazeler.
void rediagnose(AnalysisResult& r, const SystemInfo& sys);
```

```cpp
// frame_source.cpp
void rediagnose(AnalysisResult& r, const SystemInfo& sys) {
    r.diagnosis = diagnose(r.stats, r.events, sys);
}
```

Windows tarafında (yeni dosya `etw_signal_merge.cpp`, hedef `ss_etw`):

```cpp
namespace ss {
struct MergeReport { size_t eventsWithDpc = 0, eventsWithDisk = 0, eventsWithFault = 0;
                     std::string dpcSuspect;  double dpcControlRate = 0.0; };

MergeReport mergeSignals(AnalysisResult& r,
                         const EtwCaptureResult& cap,
                         SystemInfo& sys);
}
```

Çağrı noktaları (ikisi de güncellenecek):
- `ss_cli.cpp:456-457` → `analyzeSource` sonrası `mergeSignals(result, cap, sys); rediagnose(result, sys);`
- `ss_ui.cpp:1002-1003` → aynı sırayla, `sys` telemetriyle zenginleştirildikten sonra.

### P0.7 — `ss_cli` eksik kütüphaneler

`CMakeLists.txt:71-76`: `ss_cli` yalnızca `ss_core`, `shell32`, `ss_etw` bağlıyor. `ss_telem` ve `ss_probe` **bağlı değil** → CLI'da `medianDiskActivePct` / `p95DiskLatencyMs` hiç dolmuyor (`core.cpp:649-659` bunları okuyor), güç planı/sorunlu aygıt taraması da yok.

```cmake
# CMakeLists.txt:74-76 YERINE
if (WIN32)
    target_link_libraries(ss_cli PRIVATE shell32 ss_etw ss_telem ss_probe)
endif()
# ss_probe'a psapi (GetPerformanceInfo) eklenecek — P1
target_link_libraries(ss_probe PRIVATE powrprof setupapi cfgmgr32 psapi)
```

> `ss_ui` için bu sorun **yok**: `CMakeLists.txt:93` zaten `ss_telem ss_probe` bağlıyor, `ss_ui.cpp:1180` `Sampler`'ı örneklüyor, `ss_ui.cpp:987-990` alanları dolduruyor. (Bir değerlendirme bunun aksini iddia ediyordu — §10.3.)

---

## 3. FAZ P1 — Bellek Katman A (yönetici gerekmez)

**Vaka 1'i tek başına çözer.** ETW'ye, yönetici hakkına, sistem logger slotuna dokunmaz. Oyun sürecine `OpenProcess` yapılmaz.

Yeri: **`system_probe.cpp` / `system_probe.h`** (`ssprobe` namespace). Bu dosya zaten "tek seferlik envanter evi" ve başlığı Windows'tan bağımsız — ideal.

### P1.1 — Yapılar

```cpp
// system_probe.h — ssprobe namespace icine
struct MemorySnapshot {
    uint64_t commitTotal = 0, commitLimit = 0, commitPeak = 0;
    uint64_t physicalTotal = 0, physicalAvailable = 0;
    bool     lowMemoryFlag = false;   // cekirdegin KENDI bayragi
    bool     pagefileDisabled = false;
    bool     valid = false;
};

struct ProcMem {
    uint32_t     pid = 0;
    uint64_t     createTime = 0;       // PID geri donusumune karsi kimlik
    std::wstring image;
    uint32_t     hardFaultCount = 0;   // KUMULATIF
    uint64_t     commitBytes = 0;      // PagefileUsage  (ozel commit)
    uint64_t     wsPrivate   = 0;      // WorkingSetPrivateSize (ozel resident)
    uint64_t notResident() const { return commitBytes > wsPrivate
                                        ? commitBytes - wsPrivate : 0; }
};

bool readMemorySnapshot(MemorySnapshot& out);
bool readProcessMemory(std::vector<ProcMem>& out);
```

> **Manşet formülü düzeltmesi:** `PagefileUsage - WorkingSetSize` **yanlış**. `PagefileUsage` yalnızca **özel** commit; `WorkingSetSize` özel **+ paylaşımlı** resident. Farklı kümeler çıkarılıyor, büyük mapped doku working set'i olan bir oyunda `SIZE_T` alttan taşar. Doğru alan aynı yapıda mevcut: **`WorkingSetPrivateSize` @0x08**.
> Etiket de dürüst olmalı: bu değer **dokunulmamış commit'i de sayar**, tek başına sayfalama kanıtı değildir.

### P1.2 — `SYSTEM_PROCESS_INFORMATION` (x64, `sizeof == 0x100`)

`winternl.h`'deki public sürüm `HardFaultCount`'u `Reserved1[48]` içine gizler → kendi yapını bildir. **İsim çakışmasını önlemek için `SS_` öneki kullan** (`winternl.h` dahilse).

```cpp
// system_probe.cpp
#include <winternl.h>   // UNICODE_STRING / NTAPI
#include <psapi.h>      // GetPerformanceInfo
#include <cstddef>      // offsetof

struct SS_SYSTEM_PROCESS_INFORMATION {
    ULONG          NextEntryOffset;              // 0x00
    ULONG          NumberOfThreads;              // 0x04
    LARGE_INTEGER  WorkingSetPrivateSize;        // 0x08  <-- MANSET
    ULONG          HardFaultCount;               // 0x10  <-- HEDEF (Win7+)
    ULONG          NumberOfThreadsHighWatermark; // 0x14
    ULONGLONG      CycleTime;                    // 0x18
    LARGE_INTEGER  CreateTime, UserTime, KernelTime;  // 0x20 0x28 0x30
    UNICODE_STRING ImageName;                    // 0x38
    LONG           BasePriority;                 // 0x48
    HANDLE         UniqueProcessId;              // 0x50
    HANDLE         InheritedFromUniqueProcessId; // 0x58
    ULONG          HandleCount;                  // 0x60
    ULONG          SessionId;                    // 0x64
    ULONG_PTR      UniqueProcessKey;             // 0x68
    SIZE_T         PeakVirtualSize, VirtualSize; // 0x70 0x78
    ULONG          PageFaultCount;               // 0x80 (soft+hard — ISE YARAMAZ)
    SIZE_T         PeakWorkingSetSize;           // 0x88
    SIZE_T         WorkingSetSize;               // 0x90
    SIZE_T         QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;        // 0x98 0xA0
    SIZE_T         QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage;  // 0xA8 0xB0
    SIZE_T         PagefileUsage;                // 0xB8  <-- ozel commit
    SIZE_T         PeakPagefileUsage;            // 0xC0
    SIZE_T         PrivatePageCount;             // 0xC8
    LARGE_INTEGER  ReadOperationCount, WriteOperationCount, OtherOperationCount;
    LARGE_INTEGER  ReadTransferCount,  WriteTransferCount,  OtherTransferCount;
};
#ifdef _WIN64
static_assert(offsetof(SS_SYSTEM_PROCESS_INFORMATION, HardFaultCount)        == 0x10, "");
static_assert(offsetof(SS_SYSTEM_PROCESS_INFORMATION, WorkingSetPrivateSize) == 0x08, "");
static_assert(offsetof(SS_SYSTEM_PROCESS_INFORMATION, PagefileUsage)         == 0xB8, "");
static_assert(sizeof(SS_SYSTEM_PROCESS_INFORMATION) == 0x100, "");
#endif

using PfnNtQSI = LONG (NTAPI*)(ULONG, PVOID, ULONG, PULONG);
constexpr ULONG kSystemProcessInformation = 5;
constexpr LONG  kStatusInfoLengthMismatch = (LONG)0xC0000004L;

bool readProcessMemory(std::vector<ProcMem>& out) {
    static PfnNtQSI fn = reinterpret_cast<PfnNtQSI>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    if (!fn) return false;

    std::vector<uint8_t> buf(1u << 21);
    LONG st = 0;
    for (int i = 0; i < 6; ++i) {
        ULONG need = 0;
        st = fn(kSystemProcessInformation, buf.data(), (ULONG)buf.size(), &need);
        if (st >= 0) break;
        if (st != kStatusInfoLengthMismatch) return false;   // baska hata -> vazgec
        buf.resize(need ? need + (64u << 10) : buf.size() * 2);
    }
    if (st < 0) return false;

    out.clear();
    size_t off = 0;
    for (;;) {
        if (off + sizeof(SS_SYSTEM_PROCESS_INFORMATION) > buf.size()) break;  // sinir
        const auto* p = reinterpret_cast<const SS_SYSTEM_PROCESS_INFORMATION*>(
                            buf.data() + off);
        ProcMem m;
        m.pid            = (uint32_t)(uintptr_t)p->UniqueProcessId;
        m.createTime     = (uint64_t)p->CreateTime.QuadPart;
        m.hardFaultCount = p->HardFaultCount;
        m.commitBytes    = p->PagefileUsage;
        m.wsPrivate      = (uint64_t)p->WorkingSetPrivateSize.QuadPart;
        if (p->ImageName.Buffer && p->ImageName.Length)
            m.image.assign(p->ImageName.Buffer, p->ImageName.Length / sizeof(wchar_t));
        out.push_back(std::move(m));
        if (p->NextEntryOffset == 0) break;
        off += p->NextEntryOffset;
    }
    return true;
}
```

**Açılışta öz-kontrol** (yoksa çöp veriyi motora sokarsın):
1. `static_assert`'ler derleme zamanı.
2. Çalışma zamanı: kendi PID'ini listede bul; bulamazsan `false` dön.
3. İki örnek arasında kendi `HardFaultCount`'un monoton artıyor mu (azalıyorsa alanı hiç kullanma).

### P1.3 — Sistem tarafı

```cpp
bool readMemorySnapshot(MemorySnapshot& out) {
    PERFORMANCE_INFORMATION pi{}; pi.cb = sizeof(pi);
    if (!GetPerformanceInfo(&pi, sizeof(pi))) return false;
    const uint64_t ps = pi.PageSize;      // Commit*/Physical* SAYFA cinsinden,
                                          // PageSize BAYT cinsinden.
    out.commitTotal       = (uint64_t)pi.CommitTotal       * ps;
    out.commitLimit       = (uint64_t)pi.CommitLimit       * ps;
    out.commitPeak        = (uint64_t)pi.CommitPeak        * ps;
    out.physicalTotal     = (uint64_t)pi.PhysicalTotal     * ps;
    out.physicalAvailable = (uint64_t)pi.PhysicalAvailable * ps;

    // Cekirdegin KENDI dusuk-bellek bayragi. TRUE ise olculmus olgudur.
    // FALSE HICBIR SEY KANITLAMAZ: esik ~4GB basina 32MB, tavan 64MB — 16GB'lik
    // bir makinede sistem cokme noktasina gelmeden tetiklenmez. Bu yuzden
    // DISLAYICI KAPI olarak KULLANILMAZ, yalnizca destekleyici kanittir.
    static HANDLE h = CreateMemoryResourceNotification(LowMemoryResourceNotification);
    BOOL low = FALSE;
    if (h && h != INVALID_HANDLE_VALUE && QueryMemoryResourceNotification(h, &low))
        out.lowMemoryFlag = (low != FALSE);

    // Sayfa dosyasi tamamen kapaliysa (oyuncu forumlarinda yaygin "optimizasyon")
    // RAM tukenmesi sayfalama olarak DEGIL, tahsis hatasi/cokme olarak gorunur.
    // Ayri bir hukum gerektirir.
    out.pagefileDisabled = (out.commitLimit <= out.physicalTotal);
    out.valid = true;
    return true;
}
```

> **KULLANMA:** `GlobalMemoryStatusEx().dwMemoryLoad` — standby listesini boş sayar; bu makinede commit fiziksel RAM'i aşmışken %47 gösteriyordu.
> **KULLANMA:** `GetProcessMemoryInfo().PageFaultCount` — soft+hard karışık, ayrıca `OpenProcess` gerektirir.

### P1.4 — Örnekleme yeri

`sstelem::Sampler`'ın worker'ı (`telemetry.cpp:292-315`) zaten 1 Hz. Bellek okumaları **buraya** girer (ucuz, disk I/O yok):

```cpp
// telemetry.h — Sample icine
    double commitMb        = kUnknown;
    double physAvailMb     = kUnknown;
    double gameHardFaultPerSec = kUnknown;
    double gameNotResidentMb   = kUnknown;
```

Sonra `ss_ui.cpp:987-990` yanına özetleme, `ss_cli` için de aynısı (P0.7 sonrası mümkün).

**Kritik sınır:** 1 Hz örnekleme 50–500 ms'lik bir takılma penceresine hizalanamaz. Katman A **oturum seviyesi** hüküm destekler, olay seviyesi değil. Rapor metni bunu açıkça söylemeli, yoksa ürün ölçmediği bir kesinliği ima eder.

---

## 4. FAZ P2 — Depolama envanteri + ters teşhis (yönetici gerekmez)

**Vaka 4 ve 5'i çözer.** Bu fazın en değerli çıktısı bir teşhis değil, bir **red**: "SSD aşınması sebep değil, yeni SSD almayın."

### P2.1 — Kesin düzeltmeler (uydurma API'ler)

| Yanlış | Doğru |
|---|---|
| `StorageDeviceHealthProperty` | **YOKTUR** → `StorageDeviceProtocolSpecificProperty` = 50 |
| `STORAGE_DEVICE_HEALTH_DESCRIPTOR` | **YOKTUR** |
| `RETURN_SMART_ATTRIBUTE_VALUES` | **YOKTUR** (uydurma makro adı) → `READ_ATTRIBUTES` = 0xD0 |
| `MediaErrors` @176 | **@160**. @176 `ErrorInfoLogEntryCount`'tur ve sağlıklı diskte bile sürekli artar (test makinesinde 6452). Kullanıcıya "6452 medya hatası" göstermek ders kitabı yanlış-teşhistir. |

**Elle ofset yazma** — `#include <nvme.h>` (SDK 10.0.26100 `shared\nvme.h`, doğrulandı: mevcut) `NVME_HEALTH_INFO_LOG`'u doğrudan verir; @160/@176 tuzağı inşaat gereği ortadan kalkar.

### P2.2 — Sorgu (yönetici gerekmez, `dwDesiredAccess = 0`)

`IOCTL_STORAGE_QUERY_PROPERTY` = 0x002D1400, **`FILE_ANY_ACCESS`** ile tanımlı → erişimsiz handle yeterli.

```cpp
// system_probe.cpp
#include <winioctl.h>
#include <nvme.h>

bool queryNvmeHealth(HANDLE h, DriveHealth& out) {
    std::vector<uint8_t> buf(sizeof(STORAGE_PROPERTY_QUERY)
                           + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)
                           + NVME_MAX_LOG_SIZE, 0);

    auto* q = reinterpret_cast<PSTORAGE_PROPERTY_QUERY>(buf.data());
    q->PropertyId = StorageDeviceProtocolSpecificProperty;   // = 50
    q->QueryType  = PropertyStandardQuery;

    auto* sp = reinterpret_cast<PSTORAGE_PROTOCOL_SPECIFIC_DATA>(q->AdditionalParameters);
    sp->ProtocolType             = ProtocolTypeNvme;
    sp->DataType                 = NVMeDataTypeLogPage;
    sp->ProtocolDataRequestValue = NVME_LOG_PAGE_HEALTH_INFO;   // 0x02
    sp->ProtocolDataOffset       = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    sp->ProtocolDataLength       = sizeof(NVME_HEALTH_INFO_LOG); // 512

    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
                         buf.data(), (DWORD)buf.size(),
                         buf.data(), (DWORD)buf.size(), &got, nullptr))
        return false;   // Intel VMD / USB kopru / RAID -> NORMAL basarisizlik

    auto* d = reinterpret_cast<PSTORAGE_PROTOCOL_DATA_DESCRIPTOR>(buf.data());
    if (d->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR) ||
        d->Size    != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) return false;

    const auto& rd = d->ProtocolSpecificData;
    if (rd.ProtocolDataLength < sizeof(NVME_HEALTH_INFO_LOG)) return false;

    // TUZAK: ProtocolDataOffset, STORAGE_PROTOCOL_SPECIFIC_DATA'nin BASINA
    // goredir — tampon basina gore DEGIL.
    const auto* log = reinterpret_cast<const NVME_HEALTH_INFO_LOG*>(
        reinterpret_cast<const uint8_t*>(&d->ProtocolSpecificData) + rd.ProtocolDataOffset);

    out.criticalWarning = *reinterpret_cast<const uint8_t*>(&log->CriticalWarning);
    const int kelvin = log->Temperature[0] | (log->Temperature[1] << 8);  // LE, KELVIN
    const int c = kelvin - 273;
    out.tempCelsius     = (c > -20 && c < 150) ? c : DriveHealth::kUnknown;
    out.spareRemaining  = log->AvailableSpare;
    out.spareThreshold  = log->AvailableSpareThreshold;
    out.wearPercentUsed = log->PercentageUsed;      // 100'u ASABILIR, 255'te doyar
    out.mediaErrors     = le128to64(log->MediaErrors);
    out.critTempMinutes = log->CriticalCompositeTemperatureTime;
    out.isNvme = out.healthDataValid = true;
    return true;
}
```

Hedef aygıt: **oyunun exe yolundan birim handle'ı** (`\\.\D:`), sabit `PhysicalDrive0` **değil**.

```cpp
HANDLE openPassive(const std::wstring& dev) {
    return CreateFileW(dev.c_str(), 0,                    // erisim YOK -> admin YOK
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, 0, nullptr);
}
```

`IncursSeekPenalty` (HDD tespiti — **tek başına en güçlü depolama sinyali**, bedava):

```cpp
SystemInfo::Tri queryRotational(HANDLE h) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceSeekPenaltyProperty;   // = 7
    q.QueryType  = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR d{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                         &d, sizeof(d), &got, nullptr) || d.Size == 0)
        return SystemInfo::Tri::Unknown;               // false DONME!
    return d.IncursSeekPenalty ? SystemInfo::Tri::Yes : SystemInfo::Tri::No;
}
```

### P2.3 — `PercentageUsed` teşhise GİRMEZ

Gerekçe (dört ayrı sebep, hepsi ayakta):
1. NVMe spec'inde tanımı *"vendor specific estimate"* — yazılan TBW/garanti odometresi, gecikme metriği değil. Saatte bir güncellenir.
2. SATA'da hiç standart yok; CrystalDiskInfo'nun "%"si vendor'a özel normalize attribute'tan gelir. İki diskteki "%56" aynı şeyi ifade etmez.
3. Aşınma wear-leveling + over-provisioning + read-retry ile gizlenir; kademeli gecikme değil **arıza** olarak ortaya çıkar.
4. Forum vakalarındaki gerçek confound: disk %90+ dolu, DRAM'siz/QLC model, oyun HDD'de, 16 GB RAM. Korelasyon **yaştan** geliyor, aşınmadan değil.

**Aşınma, bu projede CPU sıcaklığının muadilidir** (`telemetry.h:8-12`, tasarım kuralı 1): envanter bilgisidir, omurga değil.

Sunum: ~~"Sağlık %94"~~ → **"Kullanılan ömür: %6 (üretici garanti sayacı — performans göstergesi DEĞİLDİR)"**.

### P2.4 — Ters teşhis (yeni ürün özelliği)

```cpp
// core.cpp — KURAL 8'in HEMEN ARDINA
// -------------------------------------------------------------------
// TERS TESHIS. Kullanici "SSD sagligim %56" diye geliyorsa ve disk
// beklemesi normalse, motor bunu AKTIF OLARAK reddetmeli.
// Gereksiz donanim alimini onlemek, dogru teshis kadar degerlidir.
// -------------------------------------------------------------------
if (sys.userSuspectsDriveHealth && sys.gameDriveWearPercentUsed >= 0) {
    const double dw = fractionOf(events, [](const StutterEvent& e) {
        return e.signals.systemSignalsMeasured && e.signals.diskWaitMs > 100.0;
    });
    const bool measured = sys.etwSystemSignalsAvailable;
    if (measured && dw < 0.10 &&
        !sys.gameDriveReliabilityDegraded && !sys.gameDriveSpareBelowThreshold) {
        d.notes.push_back(
            "SSD'nizin 'kullanilan omur' degeri %" +
            std::to_string(sys.gameDriveWearPercentUsed) +
            ". Bu deger uretici garantisi icin bir sayactir, HIZ GOSTERGESI DEGILDIR. "
            "Olcumlerde disk beklemesi normal, kritik uyari yok, yedek blok bol. "
            "Takilmanizin sebebi SSD asinmasi degil — yeni SSD almayin.");
    }
}
```

> `Diagnosis`'te bugün negatif/elenmiş bulgu için alan yok (`core.h:260-265`). `std::vector<std::string> notes;` eklenecek — `core.h` Windows'a bağımlı olmadığı için kural 5 ihlal edilmez.

### P2.5 — Çağrılma zamanı

**`Sampler::readNow` içinde ASLA çağrılmaz.** `CreateFile`+`DeviceIoControl` çoklu saniye stall üretebilir (HDD spin-up, derin IO kuyruğunun arkasındaki StorNVMe admin komutu) ve hemen ardından okunan PDH disk sayaçlarını + P3'ün ölçtüğü `diskWaitMs`'i **kirletir** — ölçmeye çalıştığı şeyi bozan bir ölçüm.

→ **Yakalama başında ve sonunda, capture worker ipliğinde, toplam iki kez.** `MediaErrors` ve `CriticalWarning` delta'sı bu iki okumadan çıkar.

`WarningCompositeTemperatureTime` / `CriticalCompositeTemperatureTime` birimi **dakika**dır; 60–300 s'lik bir yakalamada delta 0 veya 1 çıkar → **oturum-içi kanıt olarak kullanılamaz**, yalnızca ömür göstergesi olarak "geçmişte" etiketiyle sunulur.

---

## 5. FAZ P3 — ETW sistem logger: disk + hard fault

### P3.1 — Oturum yükseltmesi

```cpp
// makeSessionProperties() — P0.3'teki degisikliklere EK
    p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE
                   | EVENT_TRACE_SYSTEM_LOGGER_MODE;      // 0x02000000, Win8+

    p->EnableFlags = EVENT_TRACE_FLAG_DISK_IO              // 0x00000100
                   | EVENT_TRACE_FLAG_MEMORY_HARD_FAULTS   // 0x00002000
                   | EVENT_TRACE_FLAG_THREAD;              // 0x00000002 (TID->PID)

    // Tampon: sistem logger tamponlari SAYFALANMAZ ve CPU BASINADIR.
    SYSTEM_INFO si{}; GetSystemInfo(&si);
    p->BufferSize     = 128;                                          // KB
    p->MinimumBuffers = std::max(24ul, si.dwNumberOfProcessors * 2);
    p->MaximumBuffers = 256;                                          // ~32 MB tavan
    p->FlushTimer     = 1;
```

**Bilerek KAPALI:**
- `EVENT_TRACE_FLAG_DISK_IO_INIT` — `HighResResponseTime` zaten init→complete; hacmi 2× yapar.
- `EVENT_TRACE_FLAG_DISK_FILE_IO` / `FILE_IO` — hacmi Present'ten büyük.
- `EVENT_TRACE_FLAG_INTERRUPT` (ISR) — `SignalSnapshot`'ta ISR alanı yok.
- `EVENT_TRACE_FLAG_CSWITCH` — gereksiz, hacmi 3–5×.

**Oturum adı** `KERNEL_LOGGER_NAME` **OLMAMALI** (mevcut `"StutterScopeSession"` doğru). `Wnode.Guid` kendi GUID'imiz (P0.3).

### P3.2 — Slot ve hata mesajları

Sistem genelinde 8 sistem logger yuvası var, **ilk ikisi rezerve** (NT Kernel Logger + Circular Kernel Context Logger) → üçüncü taraflara **6** kalıyor. WPR, Xbox Game Bar, Defender, Sysmon, anti-cheat telemetrisi bunları yer.

```cpp
if (rc == ERROR_NO_SYSTEM_RESOURCES) {          // 1450
    result.error =
        "Windows'un sistem izleme yuvalari dolu (6 yuvadan hepsi kullanimda). "
        "Windows Performance Recorder, Xbox Game Bar overlay'i veya baska bir "
        "olcum araci acikssa kapatip tekrar deneyin.\n\n"
        "Acik oturumlari gormek icin YONETICI komut isteminde: logman query -ets";
        //                              ^^^^^^^^ yukseltilmemis konsolda BOS liste basar
}
else if (rc == ERROR_ACCESS_DENIED) { /* mevcut isElevated() mesaji */ }
else if (rc == ERROR_ALREADY_EXISTS) { /* killOrphanSession zaten kosuyor */ }
```

**Zarif bozulma:** `EnableFlags`'li `StartTrace` başarısızsa, `EnableFlags = 0` + `LogFileMode`'dan `SYSTEM_LOGGER_MODE` çıkarılarak **Present-only** moda düşülür ve `result.systemSignalsOn = false` işaretlenir. Sinyaller **boş kalır, 0 ile "doldurulmuş" sayılmaz**.

> **SESSİZ FAILURE UYARISI:** `EnableFlags` YALNIZCA sistem logger oturumlarında geçerlidir. `SYSTEM_LOGGER_MODE` konmazsa `StartTrace` **başarılı döner, tek bir DiskIo olayı gelmez, hata da alınmaz.** Bu yüzden §8'deki doğrulama adımı 3 (olay sayacı) zorunludur.

### P3.3 — Callback dalları (GUID kontrolü EN BAŞTA)

`onEvent`'in satır 154'teki `if (!IsEqualGUID(..., kDxgKrnlGuid)) return;` erken dönüşü ve satır 158–159'daki opcode kapısı (`INFO`/`START`) **her sistem olayını düşürür**. DPC opcode 68, DiskIo 10/11, hard fault 32 — hiçbiri bu kapıdan geçmez. Dispatch **satır 154'ten ÖNCE** olmalı.

Ayrıca `isPresentEvent` içindeki `static std::map<USHORT,bool> cache` (satır 126) yalnızca olay `Id`'sine göre anahtarlıyor, sağlayıcıya göre değil. Bugün güvenli çünkü GUID kontrolü önce geliyor — **bu sıra korunmalı**.

```cpp
void WINAPI onEvent(EVENT_RECORD* rec) {
    auto* st = g_state;
    if (!st) return;

    const uint64_t ts = (uint64_t)rec->EventHeader.TimeStamp.QuadPart;  // RAW QPC
    if (st->firstAnyQpc == 0) st->firstAnyQpc = ts;                     // saglayici-bagimsiz

    const GUID& p = rec->EventHeader.ProviderId;

    // --- 1) Thread: TID -> PID (Start/End/DCStart/DCEnd) --------------------
    if (IsEqualGUID(p, kThreadGuid)) {
        if (rec->UserDataLength >= 8) {
            const auto* d = static_cast<const ULONG*>(rec->UserData);
            const UCHAR op = rec->EventHeader.EventDescriptor.Opcode;
            if (op == 2 /*End*/ || op == 4 /*DCEnd*/) st->tid2pid.erase(d[1]);
            else                                      st->tid2pid[d[1]] = d[0];
            // TID geri donusumu: End'de SILMEZSEN 300 sn'lik yakalamada disk
            // I/O'su yanlis surece atfedilir.
        }
        return;
    }

    // --- 2) DiskIo Read/Write ----------------------------------------------
    if (IsEqualGUID(p, kDiskIoGuid)) {
        const UCHAR op = rec->EventHeader.EventDescriptor.Opcode;
        if (op != 10 && op != 11) return;                    // 12/13/14/15 ilgisiz
        if (rec->UserDataLength < sizeof(DiskIoTg1_x64)) return;
        const auto* d = static_cast<const DiskIoTg1_x64*>(rec->UserData);

        // Olay TAMAMLANMADA yazilir: TimeStamp = BITIS.
        if (d->HighResResponseTime >= ts) return;            // saglama
        const uint64_t start = ts - d->HighResResponseTime;
        if ((ts - start) < st->diskKeepTicks) return;        // GIRIS ESIGI: 10 ms

        DiskRecord r{};
        r.startQpc = start; r.endQpc = ts;
        r.tid = d->IssuingThreadId;      // EventHeader.ThreadId GUVENILMEZ
        r.bytes = d->TransferSize; r.irpFlags = d->IrpFlags;
        r.diskNumber = (uint8_t)d->DiskNumber; r.isWrite = (op == 11);
        st->disks.pushDropOldest(r);
        return;
    }

    // --- 3) Hard page fault -------------------------------------------------
    if (IsEqualGUID(p, kPageFaultGuid) &&
        rec->EventHeader.EventDescriptor.Opcode == 32) {
        if (rec->UserDataLength < sizeof(HardFault_x64)) return;
        const auto* d = static_cast<const HardFault_x64*>(rec->UserData);
        HardFaultRecord r{};
        r.endQpc = ts;
        // InitialTime'in SAAT ALANI BELGELENMEMIS (bkz. §9.4). Makul degilse
        // sure kullanilmaz, yalnizca SAYI kullanilir.
        r.startQpc = (d->InitialTime && d->InitialTime < ts) ? d->InitialTime : ts;
        r.tid = d->TThreadId; r.bytes = d->ByteCount;
        st->faults.pushDropOldest(r);
        return;
    }

    // --- 4) DPC (FAZ P4'te acilir) ------------------------------------------
    if (IsEqualGUID(p, kPerfInfoGuid)) { handleDpc(st, rec, ts); return; }

    // --- 5) Present (mevcut yol, DEGISMEZ) ----------------------------------
    if (!IsEqualGUID(p, kDxgKrnlGuid)) return;
    if (g_stopRequested.load(std::memory_order_relaxed)) return;
    const UCHAR op = rec->EventHeader.EventDescriptor.Opcode;
    if (op != EVENT_TRACE_TYPE_START && op != EVENT_TRACE_TYPE_INFO) return;
    if (!isPresentEvent(rec)) return;
    const uint32_t pid = rec->EventHeader.ProcessId;
    if (st->targetPid != 0 && pid != st->targetPid) return;
    ++st->rawEvents;
    st->presents.push_back({ts, pid});     // P0.4: delta CEVRIMDISI
}
```

### P3.4 — Payload yapıları (x64)

```cpp
#pragma pack(push, 1)
struct DiskIoTg1_x64 {                 // UserDataLength == 52
    uint32_t DiskNumber;               // @0
    uint32_t IrpFlags;                 // @4
    uint32_t TransferSize;             // @8
    uint32_t Reserved;                 // @12  (Win7'de QueueDepth) — TASAR
    int64_t  ByteOffset;               // @16
    uint64_t FileObject;               // @24
    uint64_t Irp;                      // @32
    uint64_t HighResResponseTime;      // @40  *** QPC TIK, ms DEGIL ***
    uint32_t IssuingThreadId;          // @48  Win8+
};
struct HardFault_x64 {                 // UserDataLength == 40
    uint64_t InitialTime;              // @0   (saat alani DOGRULANACAK)
    uint64_t ReadOffset;               // @8
    uint64_t VirtualAddress;           // @16
    uint64_t FileObject;               // @24
    uint32_t TThreadId;                // @32
    uint32_t ByteCount;                // @36
};
#pragma pack(pop)
static_assert(sizeof(DiskIoTg1_x64) == 52, "");
static_assert(sizeof(HardFault_x64) == 40, "");
```

`EVENT_HEADER_FLAG_32_BIT_HEADER` (0x0020) set ise x86 yerleşimi (44 bayt) — StutterScope x64 olduğu için o dalda kayıt **atlanır**, uydurulmaz.

### P3.5 — Ring tamponu (sınırsız mod tuzağı)

`ss_ui.cpp:92` → `kDurations[4] = {60, 180, 300, 0}`, `0 = sınırsız` ve arayüz bunu "siz durdurana kadar" diye sunuyor (`ss_ui.cpp:936-938`). "Süreli yakalamada ring'e gerek yok" premisi **bu kod tabanı için yanlış**.

```cpp
template <class T>
struct DropOldestRing {
    std::vector<T> v; size_t head = 0, cap = 0; bool wrapped = false;
    void init(size_t n) { cap = n; v.resize(n); }
    void pushDropOldest(const T& x) {
        v[head] = x; head = (head + 1) % cap;
        if (head == 0) wrapped = true;
    }
    bool truncated() const { return wrapped; }
};
// Butce: dpcs 200k x 26B ~5 MB, disks 100k x 40B ~4 MB, faults 200k x 28B ~6 MB
```

`wrapped == true` → `EtwCaptureResult::signalsTruncated = true` → rapor "sinyal kaydı kırpıldı, oranlar eksik olabilir" notu basar.

**Aynı zamanda `faultBuckets` hatasından kaçınır:** session bulgusundaki kodda `faultBuckets` hiçbir yerde boyutlandırılmıyordu, `if (b < size())` boş vektörde daima false → tüm hard fault'lar sessizce düşerdi. Kova toplaması **çevrimdışı** yapılır.

### P3.6 — Çevrimdışı birleştirme (`etw_signal_merge.cpp`)

```cpp
// Pencere: [t1 - lead, t2]. t1/t2 takilmayi ureten iki ardisik Present.
// LEAD PAYI SART: takilmadan ONCE baslayan disk okumasi / fault firtinasi
// pencere icinde tamamlanir. [t1, t2] alirsan sucluyu kacirir ve %30 esigini
// asla gecemezsin.
constexpr double kLeadMs = 50.0;

// IRP bayraklari (ntddk.h). DIKKAT: IRP_INPUT_OPERATION ve
// IRP_SYNCHRONOUS_PAGING_IO AYNI biti (0x40) paylasir. Tek basina 0x40
// testi siradan okumalara da uyar -> tam olarak onlenmek istenen
// "arka planda indirme varken STORAGE teshisi" yanlis pozitifi.
constexpr uint32_t kIrpPagingIo            = 0x00000002;
constexpr uint32_t kIrpSynchronousPagingIo = 0x00000040;

bool isBlocking(const DiskRecord& r, uint32_t gamePid) {
    if ((r.irpFlags & kIrpPagingIo) && (r.irpFlags & kIrpSynchronousPagingIo))
        return true;                                   // senkron sayfalama
    if (gamePid != 0 && r.pid == gamePid && !r.isWrite)
        return true;                                   // oyunun KENDI okumasi
    return false;
}
```

Oyun PID'i **yakalama sonrası** seçiliyor (`etw_frame_source.cpp:342-350`; UI her zaman `targetPid = 0` geçiyor, `ss_ui.cpp:948-949`) → PID filtresi callback'te uygulanamaz, `IoRecord` başına PID saklanıp burada uygulanır.

```cpp
MergeReport mergeSignals(AnalysisResult& r, const EtwCaptureResult& cap,
                         SystemInfo& sys) {
    MergeReport rep;
    if (!cap.systemSignalsOn || !cap.clock.ok()) return rep;
    sys.etwSystemSignalsAvailable = true;

    const uint64_t f = cap.clock.qpcFrequency;
    const uint64_t lead = (uint64_t)(kLeadMs * f / 1000.0);
    const uint32_t gamePid = cap.info.processId;

    // frames indeksinden mutlak QPC'ye: framePresentQpc (P0.2)
    auto windowFor = [&](const StutterEvent& e, uint64_t& lo, uint64_t& hi) -> bool {
        // timestampUs -> mutlak QPC
        const uint64_t t2 = cap.clock.baseQpc + (e.timestampUs * f) / 1'000'000ull;
        const uint64_t dur = (uint64_t)(e.frameTimeMs * f / 1000.0);
        hi = t2;
        lo = (t2 > dur + lead) ? t2 - dur - lead : 0;
        return true;
    };

    for (auto& e : r.events) {
        uint64_t lo, hi; windowFor(e, lo, hi);
        e.signals.systemSignalsMeasured = true;

        double worstDisk = 0.0;
        for (const auto& d : cap.disks) {
            if (d.endQpc < lo || d.startQpc > hi) continue;   // kesismiyor
            if (!isBlocking(d, gamePid)) continue;
            worstDisk = std::max(worstDisk,
                                 (double)(d.endQpc - d.startQpc) * 1000.0 / (double)f);
        }
        e.signals.diskWaitMs = worstDisk;

        uint32_t n = 0;
        for (const auto& hf : cap.faults)
            if (hf.endQpc >= lo && hf.startQpc <= hi) ++n;
        e.signals.hardFaults = n;

        if (worstDisk > 0.0) ++rep.eventsWithDisk;
        if (n > 0)           ++rep.eventsWithFault;
    }
    // DPC: FAZ P4, kontrol grubu ile (§6.3)
    return rep;
}
```

> Hard fault **süresi** `diskWaitMs`'e **beslenmez** — iki farklı kuralın (7 ve 8) kanıtını karıştırmak, hangi hipotezin ateşlediğini belirsizleştirir.

---

## 6. FAZ P4 — DPC (en son, en riskli)

### P6.1 — Bayrak ve payload

```cpp
p->EnableFlags |= EVENT_TRACE_FLAG_DPC;    // 0x00000020

static const GUID kPerfInfoGuid =
    {0xce1dbfb4,0x137e,0x4da6,{0x87,0xb0,0x3f,0x59,0xaa,0x10,0x2c,0xbc}};

// 66 = ThreadDPC (MOF adi; PerfView "ThreadedDPC" der), 68 = DPC, 69 = TimerDPC.
// UCUNU DE al: yalnizca 68 dinlersen uzun timer DPC'lerini kacirirsin.
enum : UCHAR { kOpThreadDpc = 66, kOpDpc = 68, kOpTimerDpc = 69 };

#pragma pack(push, 1)
struct DpcPayload64 { int64_t InitialTime; uint64_t Routine; };   // 16 bayt
#pragma pack(pop)
```

```cpp
void handleDpc(CaptureState* st, const EVENT_RECORD* rec, uint64_t ts) {
    const UCHAR op = rec->EventHeader.EventDescriptor.Opcode;
    if (op != kOpDpc && op != kOpTimerDpc && op != kOpThreadDpc) return;
    if (rec->UserDataLength < sizeof(DpcPayload64)) return;
    const auto* d = static_cast<const DpcPayload64*>(rec->UserData);

    const uint64_t start = (uint64_t)d->InitialTime;
    ++st->dpcSeen;
    if (ts <= start) { ++st->dpcBadTiming; return; }   // RAW_TIMESTAMP eksik sinyali

    const uint64_t ticks = ts - start;
    if (ticks < st->dpcKeepTicks) return;              // GIRIS ESIGI: 0.5 ms
    // Saglama: 100 ms'yi asan DPC saat yapilandirmasi bozuk demektir.
    if (ticks > st->qpcFrequency / 10) { ++st->dpcBadTiming; return; }

    st->dpcs.pushDropOldest({start, ts, d->Routine,
                             rec->BufferContext.ProcessorIndex});
}
```

> **MSDN yanıltıcı:** *"These events are logged when a DPC is entered"* — olay DPC **bitince** yazılır, `InitialTime` giriş anıdır. PerfView ve xperf bu varsayımla süre hesaplar.

### P6.2 — Adres → `.sys` adı

**İki yol da aynı `SeDebugPrivilege` kapısına tabidir** (`EnumDeviceDrivers` zaten `NtQuerySystemInformation(SystemModuleInformation)` sarmalayıcısıdır). "NtQSI muaf" iddiası **çürütüldü**. Sürüm gerekçesi de yanlıştı (Win11 24H2'ye özgü değil).

`NtQSI`'nin **gerçek ve tek** avantajı: `ImageSize` + `FullPathName` tek çağrıda → **kesin aralık**. `EnumDeviceDrivers` boyut vermez; "bir sonraki base'e kadar" varsayımı modüller arası boşlukları yanlış sürücüye yazar ve **kullanıcıya yanlış `.sys` adı söyletir — bu ürünün 1 numaralı riski.**

```cpp
typedef struct _RTL_PROCESS_MODULE_INFORMATION {
    HANDLE Section; PVOID MappedBase; PVOID ImageBase;
    ULONG  ImageSize, Flags;
    USHORT LoadOrderIndex, InitOrderIndex, LoadCount, OffsetToFileName;
    UCHAR  FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION;
constexpr ULONG kSystemModuleInformation = 11;

bool enableDebugPrivilege() {           // DONUS DEGERI KULLANILACAK
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid) != 0;
    if (ok) {
        SetLastError(ERROR_SUCCESS);
        AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        ok = (GetLastError() == ERROR_SUCCESS);   // ERROR_NOT_ALL_ASSIGNED -> false
    }
    CloseHandle(tok);
    return ok;
}
```

**Kritik:** yönetici olmak yetmez; yükseltilmiş token'da `SeDebugPrivilege` **DISABLED** gelir, `AdjustTokenPrivileges` ile açılması şarttır. Dönüş `false` ise **`dpcDriver` hiç doldurulmaz** — sessiz yanlış-ad üretmektense boş bırak (tasarım kuralı 3).

Ek doğrulama: tablo kurulduktan sonra `base != 0` olan modül sayısı **> 50** değilse tabloyu at.

```cpp
// KESIN aralik eslemesi. "En yakin alt" TAHMINI YAPMA.
std::string driverForAddress(const std::vector<DriverRange>& tbl, uint64_t addr) {
    auto it = std::upper_bound(tbl.begin(), tbl.end(), addr,
        [](uint64_t a, const DriverRange& r) { return a < r.base; });
    if (it == tbl.begin()) return {};
    --it;
    return (addr >= it->base && addr < it->end) ? it->name : std::string{};
}
```

Tablo **yakalama başında VE sonunda** alınır ve birleştirilir (yakalama ortasında yüklenen sürücüler için).

**Bağlama gerekmez** — `ntdll` `GetProcAddress` ile geç bağlanır; `psapi` yolu seçilmediği için `CMakeLists.txt:50`'ye ek kütüphane eklenmez.

### P6.3 — Kontrol grubu (ZORUNLU)

`core.cpp:352-372` KURAL 1 yalnızca "en çok eşleşen `.sys` ≥ %40" bakıyor, **kontrol grubu yok**. 50–500 ms'lik bir pencereyle kesişen >1 ms'lik bir DPC neredeyse her makinede bulunur (`nvlddmkm`, `ndis`, `storport`, `ACPI`). Sadece kesişim testi `frac`'i ~1.0'a çıkarır ve DRIVER_DPC her kullanıcıda %80'e oturur.

```cpp
// etw_signal_merge.cpp — dpcDriver YALNIZCA bu testten gecerse doldurulur.
void attachDpc(AnalysisResult& r, const EtwCaptureResult& cap,
               const std::vector<DriverRange>& drv, MergeReport& rep) {
    const uint64_t f = cap.clock.qpcFrequency;
    const uint64_t lead = (uint64_t)(kLeadMs * f / 1000.0);

    // 1) Takilma pencerelerinde surucu basina isabet
    std::map<std::string, size_t> hitStutter;
    std::vector<std::pair<uint64_t,uint64_t>> stutterWins;
    for (auto& e : r.events) { uint64_t lo,hi; windowFor(e,lo,hi);
                               stutterWins.push_back({lo,hi}); }

    // 2) KONTROL: ayni SAYIDA ve ayni ORTALAMA GENISLIKTE, takilma OLMAYAN
    //    kare siniralarina yerlestirilmis sahte pencereler.
    auto shamWins = buildShamWindows(r.frames, cap.framePresentQpc,
                                     stutterWins, /*seed*/ 1337);
    std::map<std::string, size_t> hitControl;

    auto scan = [&](const std::vector<std::pair<uint64_t,uint64_t>>& wins,
                    std::map<std::string,size_t>& out) {
        for (const auto& w : wins) {
            const DpcRecord* best = nullptr;
            for (const auto& d : cap.dpcs) {
                if (d.endQpc < w.first || d.startQpc > w.second) continue;
                if (!best || (d.endQpc - d.startQpc) > (best->endQpc - best->startQpc))
                    best = &d;
            }
            if (!best) continue;
            auto nm = driverForAddress(drv, best->routine);
            if (!nm.empty()) ++out[nm];
        }
    };
    scan(stutterWins, hitStutter);
    scan(shamWins,    hitControl);

    // 3) Hukum: taban orana gore ANLAMLI yukselme yoksa dpcDriver BOS kalir.
    for (size_t i = 0; i < r.events.size(); ++i) {
        uint64_t lo, hi; windowFor(r.events[i], lo, hi);
        const DpcRecord* best = longestIn(cap.dpcs, lo, hi);
        if (!best) continue;
        r.events[i].signals.dpcMaxMs =
            (double)(best->endQpc - best->startQpc) * 1000.0 / (double)f;  // OLCUM

        const std::string nm = driverForAddress(drv, best->routine);
        if (nm.empty()) continue;
        const double sRate = (double)hitStutter[nm] / std::max<size_t>(1, stutterWins.size());
        const double cRate = (double)hitControl[nm] / std::max<size_t>(1, shamWins.size());
        // Baslangic kriteri (kalibre edilecek, §9.6):
        if (sRate >= 0.40 && sRate >= 2.0 * cRate && cRate < 0.50) {
            r.events[i].signals.dpcDriver = nm;   // KURAL 1 ancak simdi ateslenir
            rep.dpcSuspect = nm; rep.dpcControlRate = cRate;
        }
    }
}
```

**Bu tasarım `core.cpp`'de sıfır değişiklik gerektirir:** KURAL 1 hem `dpcMaxMs > 1.0` hem `!dpcDriver.empty()` istiyor (satır 355); kontrol testi `dpcDriver`'ı boş bırakarak kapıyı kapatıyor.

### P6.4 — DPC eklendikten sonra zorunlu ölçüm

DPC oturumdaki **en yüksek hacimli** kaynaktır ve Present ile **aynı** callback'e akar. Her koşuda:
- `EventsLost`, `RealTimeBuffersLost`, `LogBuffersLost` **hepsi 0 mı?**
- DPC'siz koşuya göre Present olay sayısı düştü mü?

Düştüyse: önce tamponu büyüt, hâlâ düşüyorsa **DPC'yi B planı ikinci oturumuna taşı** (§1 K1).

---

## 7. `core.cpp` kural değişiklikleri

### 7.1 — Kör oturumda sahte yüksek güven (gerçek hata)

`diagnose()` `UNKNOWN_OTHER` payını **yalnızca** en üst hipotez %80'i aştığında üretiyor (satır 727-745). Sistem oturumu açılamazsa ve iki kural örn. 55/25 ile ateşlerse `residual = 0` olur, `UNKNOWN_OTHER` hiç görünmez ve güven satır 751'e göre **95'e kadar** çıkabilir — tam da olçülmemiş bir sistemde yapay yüksek güven.

```cpp
// core.cpp:727 civari, residual hesabinin YERINE
int residual = 0;
if (!d.ranked.empty() && d.ranked.front().percent > kMaxSingleCausePercent) {
    residual = d.ranked.front().percent - kMaxSingleCausePercent;
    d.ranked.front().percent = kMaxSingleCausePercent;
}

// YENI: cekirdek sinyalleri hic olculemediyse TABAN bir bilinmezlik payi.
// 0 ile "olctum, yoktu" ayni sey degildir.
const int kBlindResidual = 20;
if (!sys.etwSystemSignalsAvailable && !d.ranked.empty()) {
    const int extra = std::max(0, kBlindResidual - residual);
    if (extra > 0) {
        // Farki en ustten al, alt siralari bozma.
        const int take = std::min(extra, d.ranked.front().percent - 5);
        if (take > 0) { d.ranked.front().percent -= take; residual += take; }
    }
}

if (residual > 0) {
    Hypothesis u;
    u.cause   = Cause::UNKNOWN_OTHER;
    u.percent = residual;
    u.label   = causeLabel(Cause::UNKNOWN_OTHER);
    u.action  = causeAction(Cause::UNKNOWN_OTHER);
    u.evidence = sys.etwSystemSignalsAvailable
        ? "tek bir kural atesledi; olculemeyen alan icin ayrilan pay"
        : "bu oturumda DPC, disk ve sayfalama sinyalleri OLCULEMEDI "
          "(sistem izleme oturumu acilamadi)";
    d.ranked.push_back(u);
    std::sort(d.ranked.begin(), d.ranked.end(),
              [](const Hypothesis& a, const Hypothesis& b){ return a.percent > b.percent; });
}
```

> `core.cpp:264-266` ve `:737-738`'deki *"bu oturumda DPC, VRAM ve disk sinyalleri olculmedi"* sabit metinleri, ETW ölçmeye başladığında **olgusal olarak yanlış** hale gelir → yukarıdaki koşullu metinle değiştirilir.
> `test_core.cpp:613/636/644` UNKNOWN_OTHER davranışına assert ediyor — bu değişiklikte 67 vaka testi yeniden koşturulmalı.

### 7.2 — Yeni KURAL 18: oturum seviyesi bellek baskısı (Katman A)

```cpp
// core.cpp — KURAL 17'nin ardina
// ---------------------------------------------------------------------
// KURAL 18 — Bellek baskisi (oturum seviyesi, 1 Hz ornekleme)
// TEK BASINA "commit > fiziksel RAM" ATESLEMEZ. Bu, saglikli makinelerde
// normaldir (gelistirme makinesinde %105,7 iken 8,5 GB bostu). Zorunlu
// birlesim: commit tepesi RAM'i asmis VE fiziksel bellek gercekten daralmis.
// ---------------------------------------------------------------------
if (sys.physicalTotalBytes > 0 && sys.commitPeakBytes > 0) {
    const double commitRatio = (double)sys.commitPeakBytes / (double)sys.physicalTotalBytes;
    const bool availTight = sys.physicalAvailMinBytes > 0 &&
        (double)sys.physicalAvailMinBytes < 0.10 * (double)sys.physicalTotalBytes;
    const bool faultsHot  = sys.gameHardFaultPeakPerSec >= 500.0;   // §9.6: kalibre

    if (commitRatio > 1.0 && (availTight || faultsHot)) {
        int w = 45;
        std::string why = "islenen bellek talebi (" + gb(sys.commitPeakBytes) +
                          " GB) takili RAM'i (" + gb(sys.physicalTotalBytes) + " GB) asti";
        if (availTight) { w += 15; why += "; bos fiziksel bellek %10'un altina indi"; }
        if (faultsHot)  { w += 15; why += "; oyun sureci saniyede " +
                          std::to_string((int)sys.gameHardFaultPeakPerSec) +
                          " hard fault uretti"; }
        acc.add(Cause::PAGEFILE_RAM, w, why);
    }
    // Ayri hukum: sayfa dosyasi kapaliysa sayfalama GORUNMEZ, RAM tukendiginde
    // oyun cokerek biter.
    if (sys.pagefileDisabled && commitRatio > 0.90) {
        acc.add(Cause::PAGEFILE_RAM, 30,
                "sayfa dosyasi kapali ve bellek talebi RAM'e cok yakin — "
                "RAM tukendiginde oyun takilmaz, coker");
    }
}
```

### 7.3 — KURAL 8 destekleyicileri

```cpp
// core.cpp:499-508 KURAL 8'in YERINE
{
    double f = fractionOf(events, [](const StutterEvent& e) {
        return e.signals.systemSignalsMeasured && e.signals.diskWaitMs > 100.0;
    });
    if (f >= 0.30) {
        int w = (int)(f * 70);
        std::string why = "takilmalarin " + pct(f) + "'inde disk beklemesi 100 ms ustu";
        // Destekleyiciler YALNIZCA zaten atesleyen hipotezi guclendirir.
        if (sys.gameDriveRotational == SystemInfo::Tri::Yes)
            { w += 15; why += "; oyun sabit diskte (HDD)"; }
        if (sys.gameDriveFreeRatio >= 0.0 && sys.gameDriveFreeRatio < 0.10)
            { w += 10; why += "; diskte %10'dan az bos alan"; }
        if (sys.gameDriveReliabilityDegraded)
            { w += 20; why += "; surucu guvenilirlik uyarisi veriyor"; }
        if (sys.gameDriveSpareBelowThreshold)
            { w += 20; why += "; yedek blok esigin altinda"; }
        acc.add(Cause::STORAGE, w, why);
    }
}
// NOT: gameDriveWearPercentUsed BILINCLI OLARAK YOK. Asinma kanit degildir.
```

KURAL 7 (`core.cpp:489-496`) aynı şekilde `systemSignalsMeasured` kapısını alır.

---

## 8. Doğrulama sırası (atlamayın, sırayla)

| # | Adım | Beklenen | Başarısızsa |
|---|---|---|---|
| **0** | Uygulama **yönetici olarak** mı koşuyor? | `isElevated() == true` | P3/P4 hiç denenmez, P1/P2 çalışır |
| **1** | P0.3 sonrası: RAW_TIMESTAMP açık, Present hâlâ akıyor mu ve **kare SÜRELERİ** hâlâ doğru mu? | Boş masaüstü 10 sn → ~13.700 olay, medyan kare süresi ≈ 16.7 ms / 6.9 ms | Frekans kaynağını kontrol et (`PerfFreq` 0 mı?) |
| **2** | P0.4 sonrası: çevrimdışı delta ile eski delta aynı kareleri mi veriyor? | Aynı `frameCount`, aynı medyan | Sıralama/taban hatası |
| **3** | P3: `SYSTEM_LOGGER_MODE` + `DISK_IO` açık. **Olay sayacı > 0 mı?** | `diskEventsSeen > 0` | `EnableFlags` sessizce yok sayıldı → `SYSTEM_LOGGER_MODE` eksik |
| **4** | `EventsLost` + `RealTimeBuffersLost` + `LogBuffersLost` **hepsi 0** mı? | 0 | Tamponu büyüt; hâlâ değilse raporu "güvenilmez" işaretle |
| **5** | P4: `dpcSeen > 0` ve `dpcBadTiming == 0` mı? | 0 | `dpcBadTiming > 0` → RAW_TIMESTAMP eksik |
| **6** | `buildDriverTable().size() > 50` ve `enableDebugPrivilege() == true` mi? | Evet | `dpcDriver`'ı **hiç doldurma** |
| **7** | Kontrol grubu: boş masaüstünde 3 dakika DPC yakala → `dpcControlRate` ne? | Yüksek çıkmalı; suçlu üretilmemeli | Eşikleri §9.6'ya göre kalibre et |
| **8** | `ctest` — 67 vaka testi hâlâ geçiyor mu? | Geçiyor | `core.h`/`core.cpp` değişiklikleri regresyon üretti |
| **9** | DPC açık/kapalı Present olay sayısı karşılaştırması | Fark < %1 | DPC'yi B planı ikinci oturumuna taşı |

---

## 9. DOĞRULANMASI GEREKEN

Bu maddeler **hiçbir bulguda ampirik olarak test edilmedi**. Kod yazılmadan önce doğrulanmalı; varsayımla ilerlenmemeli.

**9.1 — Tek oturumda sistem + manifest sağlayıcı karışımı.** `EVENT_TRACE_SYSTEM_LOGGER_MODE` + `EnableFlags` + aynı handle'a `EnableTraceEx2(DxgKrnl)` kombinasyonunun bu makinede çalıştığı **test edilmedi**. Kanıt: PerfView `TraceEventSession.cs` (satır ~714) ve `EVENT_TRACE_PROPERTIES` dokümanının `EnableFlags`'i sistem logger'ları için tanımlaması. **İlk iş bu.** Tutmazsa maliyet düşük (§1 K1 B planı).

**9.2 — `Wnode.Guid` çelişkisi.** MS dokümanı *"You must assign a new GUID to this member"* diyor; PerfView tamponu sıfırlayıp hiç dokunmuyor ve çalışıyor. Plan doküman-uyumlu yolu (kendi GUID'imiz) seçti; sıfır bırakmanın da tolere edildiği **doğrulanmadı**.

**9.3 — Gerçek zamanlı oturumda `LogfileHeader.PerfFreq`'in dolduğu.** Dokümanda garanti edilmiyor. P0.3'teki fallback bu belirsizliği kapatıyor ama fallback'in hangi sıklıkta devreye girdiği ölçülmeli (logla).

**9.4 — `PageFault_HardFault.InitialTime`'ın saat alanı.** MOF'ta tipi `object` + `Extension("WmiTime")`; PerfView bunu 100 ns tabanında okuyor. RAW_TIMESTAMP açıkken `TimeStamp` ham QPC olduğu için **iki farklı saat alanı olabilir**. İlk koşuda `(TimeStamp - InitialTime)` farkının makul aralıkta (0–2000 ms) olduğunu doğrula; saçmalıyorsa **süreyi kullanma, sadece sayıyı kullan** (KURAL 7 zaten sadece sayıyı istiyor).

**9.5 — Windows bellek sıkıştırma ve hard fault sayacı.** Bir bulgu "Memory Compression süreci 76.289 hard fault gösteriyor, ham sayıya güvenme" derken; bir çürütme "sıkıştırma deposundan geri dönüş SOFT fault'tur" diyor. **Çelişkili.** Sıkıştırılmış-depo fault'unun `HardFaultCount`'a ve ETW `PageFault_HardFault` olayına girip girmediği ölçülmeli (Memory Compression sürecinin sayacını izole et).

**9.6 — Tüm eşikler kalibre edilmemiş.** Aşağıdakiler **tahmin**dir, gerçek veriyle değiştirilmeli:
- Giriş eşikleri: DPC 0.5 ms, disk 10 ms — güvenli (kural eşikleri 1.0 ms / 100 ms) ama hacim etkisi ölçülmedi.
- KURAL 7 `hardFaults > 100` **tek pencerede**. Bir bulgu hard fault hacmini "normalde onlarca" diye tahmin ediyor → eşik ulaşılamaz olabilir. 50 ms lead payı burada kozmetik değil, eşiğin ulaşılabilir olmasının tek sebebi.
- KURAL 8 `diskWaitMs > 100 ms`: sağlıklı NVMe'de neredeyse hiç görülmez, HDD'de normal çalışmada bile görülür. **Disk tipine göre ayrı eşik (SSD ~50 ms / HDD ~150 ms) düşünülmeli — ama önce veri topla.**
- KURAL 18 `gameHardFaultPeakPerSec >= 500` — tamamen tahmin.
- Kontrol grubu kriteri (`sRate >= 0.40 && sRate >= 2×cRate && cRate < 0.50`) — istatistiksel temeli yok, ampirik.
- Lead payı 50 ms.

**9.7 — Vaka 2 (laptop güç limiti) hiçbir bulguda kapsanmıyor.** GPU/CPU %50'yi geçmiyor tablosu için ne bir API ne bir sinyal önerildi. `telemetry.cpp` NVML throttle bayrakları (`powerCapThrottle`, `powerBrake`) kısmen dokunuyor ama AMD tarafı yok (ADLX entegrasyonu bekliyor, `telemetry.h:17`) ve laptop güç profili/PL1-PL2 okuması hiç yok. **Ayrı bir araştırma kalemi.**

**9.8 — VRAM / paylaşılan GPU belleği.** Vaka 1'in tam çözümü "RTX 5070 12 GB taşıp paylaşılan GPU belleğine döküldü mü?" sorusunu gerektiriyor, ama **hiçbir bulguda doğrulanmış bir VRAM API'si yok**. Bir çürütme `IDXGIAdapter3::QueryVideoMemoryInfo` / `D3DKMTQueryStatistics`'i eksiklik olarak işaret etti ama test etmedi. `SignalSnapshot::vramOverBudget` alanı var, dolduran yok. **Ayrı kalem.**

**9.9 — `SMART_RCV_DRIVE_DATA_EX`.** `CTL_CODE(IOCTL_DISK_BASE, 0x0023, METHOD_BUFFERED, FILE_ANY_ACCESS)`, `NTDDI_WIN10_CO` kapılı. `FILE_ANY_ACCESS` olduğu için `dwDesiredAccess = 0` handle ile SATA SMART'ı yönetici olmadan okuyabilir. **Denenmedi.** Tutarsa "yükseltilmemiş modda SATA aşınması okunamaz" kısıtı tamamen kalkar. Win10'da yoktur.

**9.10 — Image rundown zamanlaması.** Gerçek zamanlı oturumda kernel image rundown'unun `DCStart` (başta) mı `DCEnd` (sonda) mı geldiği doğrulanmadı. Plan `NtQSI` snapshot yolunu birincil tuttuğu için bu bilgi kritik değil; `IMAGE_LOAD` yoluna geçilirse gerekir. (P0.5'teki teardown düzeltmesi `DCEnd` senaryosunu zaten zararsız hale getiriyor.)

**9.11 — ISR payload'unda `Vector` alanının tipi.** Bir bulgu `UCHAR @17`, bir çürütme `UINT16 @17-18` diyor. **Çelişkili.** ISR v0.2'de açılmadığı için etkisiz; ISR açılırsa çözülmeli.

**9.12 — `nvme.h` bayt 6.** Windows `nvme.h` bunu `Reserved0[26] @6..31` sayıyor; güncel NVMe spec'inde bayt 6 = *Endurance Group Critical Warning Summary*. Bu alan kullanılacaksa fark bilinmeli.

**9.13 — Anti-cheat görünürlüğü.** İmzasız bir exe'nin (GitHub Releases) `SeDebugPrivilege` açıp kernel modül listesi çekmesi ve sistem ETW oturumu başlatması, EDR/anti-cheat sezgiselleri için klasik bir imzadır. Tasarım kuralı 2'nin **lafzını** ihlal etmez (oyun sürecine dokunulmuyor) ama ruhunu zorlar. **Ölçülmedi.** Sistem oturumunu kullanıcının kapatabileceği bir seçenek yap.

---

## 10. Plana ALINMAYAN iddialar

### 10.1 — Çürütüldüğü için elenenler

| İddia | Neden elendi |
|---|---|
| `ProcessTrace` 64 handle alır, oturumlar arası kronolojik teslim eder | **Yanlış.** 64 = yalnızca dosya oturumları. Birden fazla realtime handle → `ERROR_INVALID_PARAMETER` |
| "Çekirdek olayları DxgKrnl oturumuna eklenemez, iki oturum ZORUNLU" | NT Kernel Logger'a özgü kısıt; Win8+ için eskimiş |
| System Provider'lar "Win8+" / "Win11 22H2+" | **İkisi de yanlış**: Windows 10 SDK build **20348+**. Win10 19045'te yok |
| Callback'te `ProviderId == SystemInterruptProviderGuid` karşılaştırması | Olaylar bu GUID'lerle etiketlenmez → hiçbir şey yakalamaz |
| `FileObject` → `pagefile.sys` vs `.pak` ayrımı | FileIo rundown **oturum sonunda** gelir; pagefile boot'ta açılır → eşleme canlıda hiç gelmez. **Ayrıca mantık ters**: Windows önce temiz dosya-destekli sayfaları atar, oyunun kendi dosyalarına düşen hard fault RAM baskısının en tipik belirtisidir |
| `notResident = PagefileUsage - WorkingSetSize` | Birim uyumsuz (özel vs özel+paylaşımlı), alttan taşar → `WorkingSetPrivateSize` |
| `PercentageUsed` → STORAGE hipotez yüzdesi | Garanti odometresi, gecikme metriği değil; §4.3 |
| `NtQuerySystemInformation` `SeDebugPrivilege`'ten muaf | `EnumDeviceDrivers` zaten onun sarmalayıcısı; aynı kapı |
| `ramTight` dışlayıcı kapı (`lowMemoryFlag \|\| avail < %5`) | Pratikte hiç açılmaz → `hardFaults` daima 0. Destekleyici olarak kullanılır, kapı olarak değil |
| `StorageDeviceHealthProperty`, `STORAGE_DEVICE_HEALTH_DESCRIPTOR`, `RETURN_SMART_ATTRIBUTE_VALUES` | **Var olmayan isimler**, derlenmez |
| `MediaErrors` @176 | @160. @176 `ErrorInfoLogEntryCount` |
| Tek `& 0x40` ile senkron-sayfalama testi | `IRP_INPUT_OPERATION` aynı biti paylaşır → sıradan okumalara uyar. `0x02 & 0x40` birlikte |
| `\Memory\Pages/sec`, `dwMemoryLoad`, `GetProcessMemoryInfo().PageFaultCount`, PDH `Avg. Disk sec/Read` (takılma anı için) | Hepsi ayrıştırıcı değil; §3, §4 |
| `IOCTL_STORAGE_PREDICT_FAILURE` "StorNVMe uygulamıyor" | n=1 aşırı genelleme. SATA'da denenebilir, başarısızlık normal karşılanır |

### 10.2 — Kapsam dışı bırakılanlar (çürütülmedi, ertelendi)

- **ISR (`EVENT_TRACE_FLAG_INTERRUPT`)** — `SignalSnapshot`'ta alan yok, hacmi katlıyor.
- **`CSWITCH`** — hiçbir sinyal gerektirmiyor.
- **`FILE_IO` / `DISK_FILE_IO`** — hacmi Present'ten büyük.
- **`DISK_IO_INIT`** — `HighResResponseTime` zaten init→complete.
- **`IMAGE_LOAD` sürücü tablosu** — `NtQSI` snapshot daha ucuz; Plan B olarak dursun (§9.10).
- **Win7 desteği** — `IssuingThreadId` `TypeGroup1`'de yok. **Açıkça desteklenmiyor denmeli**, `DISK_IO_INIT` + Irp korelasyonu yazılmamalı.
- **x86 hedefi** — payload yerleşimleri farklı; x86'da sinyal kaydı atlanır.

### 10.3 — Değerlendirmelerin kendi hataları (depoda doğrulandı)

Çürütmeler de hatasız değil; aşağıdakiler **kodda kontrol edildi** ve plana yanlış girmemesi için not düşülüyor:

1. **"`sstelem::Sampler` hiçbir yerde örneklenmiyor, `ss_telem` `ss_ui`'ye linklenmiyor"** → **YANLIŞ.** `CMakeLists.txt:93` `ss_telem ss_probe` bağlıyor, `ss_ui.cpp:1180` `new sstelem::Sampler()`, `ss_ui.cpp:987-990` `medianDiskActivePct`/`p95DiskLatencyMs` dolduruyor. Sorun yalnızca **`ss_cli`** için gerçek (`CMakeLists.txt:71-76`) → P0.7'de düzeltiliyor.
2. **Çağrı noktası satır numaraları** — `analyzeSource` gerçekte `ss_ui.cpp:1003` ve `ss_cli.cpp:457/469`'da (492/457 değil). `kDurations` `ss_ui.cpp:92`'de (74 değil).
3. **"`EVENT_TRACE_USE_PAGED_MEMORY` temizlendiğinden emin ol"** → bu bayrak bugün hiç set edilmiyor (`etw_frame_source.cpp:105`). Sadece yorum olarak korunuyor.

---

## 11. Vaka kapsama matrisi

| Forum vakası | Çözen faz | Sinyal | Kapsama |
|---|---|---|---|
| **1** — RTX 5070 + 16 GB, 17 GB commit, CPU %99 GPU %70 | **P1** (+P3 doğrular) | `commitPeak > physicalTotal` **VE** `physAvailMin < %10` / hard fault oranı → KURAL 18; ayrıca KURAL 0'daki `gpuStarved` dalı (`core.cpp:332-345`) zaten bu vaka için yazılmış | **Tam** (VRAM taşması hariç, §9.8) |
| **2** — Laptop, GPU/CPU %50'yi geçmiyor → güç limiti | **HİÇBİRİ** | — | **Kapsanmıyor**, §9.7 |
| **3** — i9-13900K + 4080, masaüstünde de mikro takılma | **P4** | `dpcMaxMs` + kontrol grubunu geçmiş `dpcDriver` → KURAL 1. Ayrıca `desktopStutterObserved` (`ss_ui.cpp:1000`) zaten set ediliyor | **Tam** (kontrol grubu kalibrasyonuna bağlı) |
| **4** — RX6600, SSD sağlık %40, mikro takılma | **P2 + P3** | `diskWaitMs` (nedensel kanıt) + `IncursSeekPenalty` + boş alan → KURAL 8; **ters teşhis** "%40 sebep değil" | **Tam** |
| **5** — RX5700XT, sağ tık 3-4 sn, SSD %56 | **P2 + P3** (+P1) | `diskWaitMs` sistem geneli + `hardFaults` → KURAL 7/8; ters teşhis | **Tam**; sağ-tık gecikmesi oyun dışı olduğu için `desktopStutterObserved` de destekler |

---

## 12. Uygulama sırası — tek bakışta

```
P0  boru hatti                        [2-3 gun]  ── atlanmaz
 ├─ P0.1  core.h: systemSignalsMeasured + SystemInfo alanlari
 ├─ P0.2  etw_frame_source.h: EtwClock, *Record, framePresentQpc, 3 kayip sayaci
 ├─ P0.3  RAW_TIMESTAMP + PerfFreq/QPF fallback + kendi oturum GUID'i
 ├─ P0.4  Present delta'yi cevrimdisi hesapla (saglayici-bagimsiz taban)
 ├─ P0.5  ControlTrace(QUERY->STOP) durdurucu iplikten; CloseTrace en sona
 ├─ P0.6  mergeSignals() + rediagnose(); ss_cli:457, ss_ui:1003 guncelle
 └─ P0.7  CMake: ss_cli'ye ss_telem+ss_probe, ss_probe'a psapi
        └── DOGRULAMA 1, 2

P1  bellek Katman A                   [1 gun]    ── yonetici GEREKMEZ
 ├─ system_probe: readMemorySnapshot / readProcessMemory (NtQSI=5)
 ├─ telemetry Sample'a 4 alan, 1 Hz
 └─ core.cpp KURAL 18
        └── vaka 1

P2  depolama envanteri + ters teshis  [1-1.5 gun] ── yonetici GEREKMEZ
 ├─ system_probe: readDriveHealthForPath (IOCTL_STORAGE_QUERY_PROPERTY=50, nvme.h)
 ├─ Tri rotational + freeRatio + criticalWarning/spare/mediaErrors delta
 ├─ core.h Diagnosis::notes + ters teshis blogu
 └─ core.cpp KURAL 8 destekleyicileri
        └── vaka 4, 5

P3  ETW sistem logger                 [3-4 gun]  ── yonetici + 1 slot
 ├─ SYSTEM_LOGGER_MODE + DISK_IO|MEMORY_HARD_FAULTS|THREAD
 ├─ callback dispatch (GUID once), DropOldestRing
 └─ mergeSignals: isBlocking() + lead payi
        └── DOGRULAMA 0, 3, 4  →  vaka 4, 5 nedensel; vaka 1 dogrulanir

P4  DPC                               [3-4 gun]  ── en riskli, en son
 ├─ EVENT_TRACE_FLAG_DPC, opcode 66/68/69
 ├─ enableDebugPrivilege + NtQSI(11) surucu tablosu (bas + son)
 └─ KONTROL GRUBU (sham pencereler) — gecemezse dpcDriver BOS
        └── DOGRULAMA 5, 6, 7, 9  →  vaka 3

her fazdan sonra: ctest (67 vaka) + DOGRULAMA 8
```