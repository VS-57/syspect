# Syspect — Proje Bağlamı

> **Ad:** Proje "StutterScope" olarak başladı, **Syspect** adını aldı
> (sys + suspect/inspect). Kullanıcıya görünen metinler Syspect diyor;
> depo adı, hedef adları (`ss_ui`, `ss_cli`, `ss_core`…) ve dosya adları
> hâlâ eski önekle — bilinçli, tek seferde yeniden adlandırma ayrı iş.


Windows oyun kararlılık tanı aracı. Takılmaların ve mavi ekranların sebebini
bulup kullanıcıya **olasılık oranlarıyla** sunar.

---

## Ürünün tanımı

Bu bir izleme aracı **değil**, teşhis aracıdır. PresentMon, CapFrameX,
LatencyMon ve HWiNFO zaten mükemmel veri topluyor — hepsi kullanıcıya grafik
verip yorumu ona bırakıyor. Hedef kitle o grafiği okuyamayan kişi.

Ürünün değeri veri toplamada değil, **hüküm vermede**.

Her çıktı üç parçadan oluşur: **sebep + güven yüzdesi + tek cümlelik aksiyon.**

---

## Bozulmaması gereken kurallar

Bunlar tartışmaya kapalı tasarım kararlarıdır. Değiştirmeden önce sor.

1. **Hiçbir teşhis kuralı ÖLÇÜLEMEYEN sıcaklığa dayanamaz.** Ölçülen sıcaklık
   ve donanımın kendi bildirdiği kısıtlama bayrağı hem bulgu olarak gösterilir
   hem teşhise girebilir.

   *Revize edildi (2026-07-31).* Eski hali "hiçbir kural CPU sıcaklığına
   bağımlı olamaz" idi ve gerekçesi vaka külliyatıydı: 8 şikayetin hiçbirinde
   sebep sıcaklık değildi. Ama o külliyatta **seçim yanlılığı** var — donanım
   forumuna yazacak kadar meraklı insanlar sıcaklığı zaten ölçüp elemişti.
   Hedef kitle o kontrolü hiç yapmamış kişi, dolayısıyla veri kuralın
   dayandığı iddiayı kanıtlamıyor.

   Kuralın koruduğu asıl şey sıcaklığın önemsizliği değil, **ölçemediğimiz
   veriye kural bağlamamak.** CPU die sıcaklığı (Tctl/Tdie) ring 0 gerektirir
   ve okunamıyor; ona kural bağlamak uydurma hüküm üretir. GPU sıcaklığı NVML
   ile ölçülüyor ve kısıtlama bayrağı zaten teşhise 45 puan katıyor — kural
   fiilen GPU için çoktan bükülmüştü, yeni hali bunu dürüstçe yazıyor.

2. **Anti-cheat'e dokunma.** Hook yok, DLL injection yok, oyun process'ine
   okuma yok. Sadece pasif ETW dinleme. Bu sınırı aşmak kullanıcıların
   hesabını kaybettirir.

3. **Motor emin olmadığında susmalı.** `Diagnosis::inconclusive` gerçek bir
   çıktıdır. Yanlış teşhis bu projenin bir numaralı riskidir — kullanıcı
   çalışan bir sürücüyü kaldırabilir ya da gereksiz donanım alabilir.

4. **Asla "kesin sebep bu" deme.** Her zaman güven skoru ve alternatifler.

5. **`core.h` Windows API'sine bağımlı olmamalı.** Taşınabilir kalması,
   sentetik verilerle test edilebilmesini sağlıyor. Windows'a özgü her şey
   ayrı katmanda.

6. **Bilinmeyen veri için sentinel kullan, 0 kullanma.** Bu kural gerçek bir
   hatadan doğdu: `minutesSinceGameStart = 0` varsayılanı "shader derlemesi"
   kuralını her senaryoda tetikliyordu. Bkz. `SignalSnapshot::kUnknownMinute`.

7. **Vaka külliyatı testleri kırılmamalı.** `test_core.cpp` içindeki
   senaryolar gerçek forum şikayetlerinden türetildi. Yeni kural eklerken
   hepsi geçmeye devam etmeli.

---

## Mimari

```
ss_cli.exe       Komut satırı: CSV/canlı -> teşhis raporu      [BİTTİ]
ss_ui.exe        Klasik Win32 masaüstü uygulaması (3 sekme)    [BİTTİ]
     │ (şimdilik doğrudan çağrı — servis gelince named pipe)
ss_svc.exe       Windows servisi, LocalSystem, 7/24 dinler     [YAPILMADI]
     ├── ETW toplayıcı        (DxgKrnl Present)                [BİTTİ]
     ├── Olay günlüğü okuyucu (event_log.cpp)                  [BİTTİ]
     ├── Çökme izleyici       (dumpreader.cpp)                 [BİTTİ]
     └── Teşhis motoru        (core.cpp)                       [BİTTİ]
ss_drv.sys       KMDF, salt okunur, opsiyonel                  [v1.0'A ERTELENDİ]
```

**Taşınabilir beyin, Windows kabuk.** Analiz mantığı saf C++; veri toplama
katmanı `SignalSnapshot` ve `SystemInfo` yapılarını doldurup verir.

---

## Dosyalar

| Dosya | İçerik | Durum |
|---|---|---|
| `core.h` / `core.cpp` | Takılma dedektörü + teşhis kural motoru | ✅ 92 test |
| `event_log.h/.cpp` | Olay günlüğü okuyucu (WHEA, KP41, TDR, depolama) | ✅ |
| `frame_source.h/.cpp` | Kare kaynağı arayüzü + PresentMon CSV okuyucu | ✅ |
| `etw_frame_source.h/.cpp` | Kendi ETW oturumumuz (DxgKrnl Present) | ✅ |
| `telemetry.h/.cpp` | GPU/CPU/disk örnekleyici (NVML + PDH + WMI) | ✅ |
| `system_probe.h/.cpp` | Güç planı, sorunlu sürücü, SMBIOS bellek/EXPO | ✅ |
| `dump_scan.h` + `dumpreader.cpp` | Mavi ekran kayıtlarını otomatik tarar | ✅ |
| `report_html.h/.cpp` | Görsel HTML rapor (taşınabilir) | ✅ |
| `report_share.h/.cpp` | Foruma/yapay zekâya yapıştırılacak markdown rapor | ✅ |
| `storage_probe.h/.cpp` | Disk envanteri, dönen disk tespiti, SMART / NVMe | ✅ |
| `i18n.h/.cpp` | Dil katmanı — `lang/*.lang` dosyaları, şablon üretimi | ✅ |
| `ss_ui.cpp` | Masaüstü uygulaması — Win32 + GDI+, tamamen özel çizim | ✅ |
| `ss_cli.cpp` | Komut satırı — `capture` (canlı ETW) ve `analyze` (CSV) | ✅ |
| `samples/demo-144hz.csv` | Sentetik PresentMon izi (4320 kare, seed 1337) | ✅ |
| `test_core.cpp` | Vaka külliyatı regresyon paketi | ✅ |
| `teknik-tasarim.md` | Tam teknik tasarım dokümanı | ✅ |
| `LICENSES.txt` | Üçüncü parti lisanslar + sorumluluk reddi | ✅ |

---

## Derleme ve test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

**ss_ui.exe harici bağımlılık kullanmaz.** Klasik Win32 + comctl32. Bir dönem
WebView2 ile yazıldı ve geri alındı — kullanıcı "app değil bu, pencere içine
konmuş web sayfası" dedi ve haklıydı. Native kontroller gerçek uygulama
hissini veriyor. WebView2 SDK'sı artık gerekmiyor.

İki derleme tuzağı (ikisi de CMakeLists'te çözüldü):
- **comctl32 v6 manifesti şart.** `/MANIFESTDEPENDENCY` satırı olmadan
  sekmeler ve düğmeler Windows 95 görünümünde çizilir.
- **`UNICODE` tanımlı olmalı.** Yoksa `ListView_*` makroları ANSI mesajlara
  (`LVM_INSERTITEMA`) bağlanır ve `LVITEMW` ile tip uyuşmazlığı verir.

Değişiklik yaptıktan sonra **her zaman** `ctest` çalıştır. Testler hızlı
(< 0.1 sn), mazeret yok.

MSVC'de statik CRT (`/MT`) zorunlu — kullanıcıdan VC++ Redistributable
istenmeyecek.

**Geliştirme makinesi (2026-07-26 itibarıyla hazır).** VS Enterprise 2026'ya
NativeDesktop workload'u eklendi. `cmake` PATH'te DEĞİL, tam yol gerekiyor:

```
C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

MSVC 14.50.35717 (cl 19.50), Windows SDK 10.0.26100, CMake 4.1.1.
Generator: `-G "Visual Studio 18 2026" -A x64`.

Windows'ta ilk derlemede çıkan iki tuzak (ikisi de düzeltildi):
- **`NOMINMAX` şart.** `windows.h` min/max makroları `std::max(...)` çağrılarını
  bozuyor → `error C2589: illegal token on right side of '::'`. dumpreader.cpp
  daha önce yalnızca Linux'ta derlendiği için bu hata görünmemişti.
- **`advapi32` linkle.** `RegOpenKeyEx`/`RegQueryValueEx` (CrashControl kontrolü)
  bu kütüphanede; yoksa `LNK2019 unresolved external`.

---

## Kritik teknik notlar

Bunlar acı çekilerek öğrenildi, tekrar keşfetme:

**Minidump formatı.** `%SystemRoot%\Minidump\*.dmp` dosyaları **kullanıcı-mod
MDMP değildir.** Çekirdek dump formatıdır: imza `PAGEDU64`, yapı
`_DMP_HEADER64`. `dbghelp.dll`'in `MiniDumpReadDumpStream` API'si bunları
**açamaz**. Bugcheck kodu sabit ofset `0x38`'dedir, parametreler `0x40`'tan
başlar (0x3C'de hizalama dolgusu var).

**Throttle sebebi ETW'de yok.** `Microsoft-Windows-Kernel-Processor-Power`
sağlayıcısında "termal/güç/akım" diye dört kategorili bir throttle çıktısı
**yoktur**. Intel PL1/PL2, PROCHOT ve AMD PPT/TDC/EDC kısıtları donanım
tarafından özerk uygulanır ve Windows'a görünmez. HWiNFO bu yüzden ring 0'da
MSR `0x690` okur.

**`CurrentMhz` ölü API.** Windows 10/11'de `PROCESSOR_POWER_INFORMATION.CurrentMhz`
artık `MaxMhz` ile aynı değeri döndürüyor. Gerçek frekans için PDH sayacı
`Processor Information\% Processor Performance` kullan. Aynı yapıda park
bilgisi de **yok** — `Parking Status` sayacına bak.

**`QueryVideoMemoryInfo` süreç-başına.** `IDXGIAdapter3::QueryVideoMemoryInfo`
çağıran process'in bütçesini döndürür, sistem genelini değil. Servis bunu
çağırırsa kendi sıfır kullanımını okur. VidMm ETW olaylarını veya
`GPU Process Memory` sayacını kullan.

**WHEA olay ID'leri.** Sadece **18** ölümcül. 17/19/46/47 "düzeltilmiş"
hatalardır ve özellikle ID 17 birçok AMD sisteminde semptomsuz binlerce kez
üretilir. Tek başına asla bildirme — yalnızca frekansı taban çizgisinin çok
üstündeyse **ve** takılmalarla zamansal çakışıyorsa sinyal say.

**Dump kaydı çoğu makinede kapalı.** `HKLM\SYSTEM\CurrentControlSet\Control\
CrashControl` → `CrashDumpEnabled`. İlk çalıştırmada kontrol edilip 7
(Automatic) yapılmalı, yoksa ilk mavi ekran kaçar.

**XMP/EXPO durumu SMBIOS'ta yok.** SMBIOS Type 17 yalnızca yapılandırılmış
hızı verir. Profil verisi SPD EEPROM'unda, SMBus üzerinden okunur (ring 0).
Çıkarım yap: yapılandırılmış hız > JEDEC taban ⇒ muhtemelen EXPO açık.

**PSU transient'leri ölçülemez.** Mikrosaniye mertebesinde; NVML'in örnekleme
hızı çok yavaş. Ama `nvmlClocksThrottleReasonHwPowerBrakeSlowdown` bayrağı
PSU'nun GPU'ya gönderdiği frenleme sinyalidir — elde edilebilecek en yakın
doğrudan kanıt.

---

## Vaka külliyatı

`test_core.cpp` içindeki senaryolar gerçek Türk donanım forumu şikayetlerinden
türetildi. Ürün stratejisi bunlardan çıktı:

| Senaryo | Beklenen hüküm | Ayırt edici sinyal |
|---|---|---|
| CPU-yoğun anlarda takılma | Sürücü DPC | tek `.sys` olayların %40+'ında |
| Monitör değişiminden sonra | VRR / kare zamanlama | mikro-takılmalar **düzenli** aralıklı |
| Dump üretmeyen sert donma | Bellek / EXPO | WHEA + Kernel-Power 41, bugcheck yok |
| Opera'da da takılıyor | Undervolt / CO | takılma **oyun dışında** da var |
| Düşük FPS, düzenli | Donanım yetersiz | takılma **yok**, dağılım dar |
| Power Brake + ani kapanma | Güç kaynağı | PSU'nun GPU'ya frenleme sinyali |
| İlk 10 dakikada yoğun | Shader derlemesi | sonra kayboluyor — **normal** |
| Az veri | "Bilmiyorum" | motor hüküm vermeyi reddediyor |

**En önemli bulgu:** AM5 bellek/EXPO kararsızlığı 8 vakanın 4'ünde ana şüpheli.
Ürün bu tek sebepte mükemmel olursa hedef kitlenin yarısını çözer.

**İkinci en önemli:** "oyun dışında da takılıyor mu?" sorusu en güçlü ayırt
edicidir. Sorunun oyunda değil sistemde olduğunu tek başına söyler.

---

## Ölçülebilenler ve ölçülemeyenler (2026-07-26)

Ürünün en büyük riski yanlış teşhis. Bu yüzden neyin **ölçüldüğü**, neyin
**çıkarım** olduğu ve neyin **hiç okunamadığı** ayrı tutulur.

| Sinyal | Kaynak | Durum |
|---|---|---|
| Kare süreleri | ETW · DxgKrnl Present | Ölçülüyor |
| GPU sıcaklık / kullanım / frekans / güç / VRAM | NVML | Ölçülüyor (NVIDIA) |
| GPU throttle bayrakları (ısınma / güç limiti / power brake) | NVML | Ölçülüyor |
| GPU güç limiti, Resizable BAR | NVML (BAR1 ↔ VRAM) | Ölçülüyor |
| CPU kullanım / performans / çekirdek park | PDH | Ölçülüyor |
| Disk aktiflik / gecikme / aktarım | PDH PhysicalDisk | Ölçülüyor |
| Bellek hızı, EXPO/XMP durumu | SMBIOS Type 17 | Ölçülüyor (iki alanın farkı) |
| Güç planı, pil durumu | powrprof | Ölçülüyor |
| Sorunlu / sürücüsüz aygıt | SetupAPI + CM_PROB_* | Ölçülüyor |
| Mavi ekran kayıtları | `%SystemRoot%\Minidump` | Ölçülüyor |
| WHEA (17/18/19/46/47), Kernel-Power 41, olay 6008 | Olay günlüğü · System | Ölçülüyor |
| Mavi ekran kaydı + bugcheck kodu (dump kapalıyken) | Olay günlüğü · WER 1001 | Ölçülüyor |
| **TDR** — ekran sürücüsü sıfırlanması | Olay günlüğü · Display 4101 | Ölçülüyor |
| Depolama sıfırlama / yeniden deneme / hatalı blok | Olay günlüğü · 129/153/7/11/51 | Ölçülüyor |
| Dosya sistemi bozulması | Olay günlüğü · Ntfs 55 | Ölçülüyor |
| LiveKernelReports (mavi ekransız sürücü çökmesi) | Dosya sistemi taraması | Ölçülüyor |
| Sistem sıcaklığı (ACPI) | WMI `MSAcpi_ThermalZoneTemperature` | Çoğu masaüstünde YOK |
| **CPU die (Tctl/Tdie) sıcaklığı** | MSR — ring 0 | **Okunamıyor**, uydurulmuyor |
| Disk modeli / veri yolu / dönen disk mi | IOCTL_STORAGE_QUERY_PROPERTY | Ölçülüyor |
| Bölüm doluluk oranı, Windows hangi diskte | GetDiskFreeSpaceEx + disk extents | Ölçülüyor |
| SMART arıza tahmini | IOCTL_STORAGE_PREDICT_FAILURE | Ölçülüyor (yönetici) |
| NVMe aşınma / yedek blok / yazılan veri | NVMe log sayfası 0x02 | Ölçülüyor (yönetici) |
| DPC süreleri + suçlu `.sys` | ETW çekirdek oturumu (`dpc_source.cpp`) | Toplanıyor — ölçüme henüz bağlanmadı |
| **Hard page fault** | ETW / `NtQuerySystemInformation` | **Yapılmadı** |
| **SATA SSD SMART ayrıntısı** | ATA passthrough | **Yapılmadı** — yalnızca arıza tahmini var |
| **AMD GPU telemetrisi** | ADLX | **Yapılmadı** |
| **Dizüstü güç limiti (TGP/PL1)** | üreticiye özel | **Yapılmadı** |

## Yol haritası

- **v0.1** — ETW frame time, takılma tespiti, minidump okuma, WHEA doğru
  sınıflandırma, bellek/EXPO kuralı, oyun-dışı semptom ayrımı
- **v0.2** — DPC suçlu tespiti, GPU vendor API, kara kutu, LiveKernelReports,
  WebView2 arayüzü, rapor çıktısı
- **v0.3** — Değişiklik günlüğü, donanım denge veri tabanı, CO/PBO tespiti,
  Gölge Mod
- **v1.0** — Kernel sürücü (opsiyonel), cilalama

---

## Dil katmanı (2026-07-31)

Anahtar icat edilmiyor — **kaynak metnin kendisi anahtar.** Sebep: Türkçe mod
tablo boşken bugünküyle bit bit aynı kalır, yani çeviri eksikliği Türkçe
arayüzde asla görünmez. Anahtar tabanlı bir sistemde eksik anahtar `btn.copy`
diye ekrana düşerdi.

Çeviri **iki noktada** yapılıyor, çağrı yerlerinde değil:
`ss_ui.cpp` içindeki `text()` ve `findings.cpp` içindeki `add()`. Ekrana yazmanın
başka yolu olmadığı için yeni metin eklerken sarmalamayı unutmak mümkün değil.

Şablon **çalışma anında toplanıyor**: `text()` ve `add()` gördükleri her metni
kaydediyor, Ayarlar sayfasındaki düğme bunu `.lang` dosyası olarak yazıyor.
Kaynak dosyaları ayrıştıran bir çıkarıcı yazılmadı — metinlerin bir kısmı
çalışma anında kuruluyor, ayrıştırma kırılgan olurdu.

**Motorun kanıt cümleleri de çevrilebilir.** `Hypothesis::evidenceParts`
şablon + değer taşıyor (`core.h`, `EvidencePart`); `evidence` alanı
birleştirilmiş Türkçe metin olarak duruyor ve komut satırı, testler, JSON onu
okumaya devam ediyor. `core.h` taşınabilir kaldı — orada çeviri yok, yalnızca
veri.

**Yer tutucu `{1}`, `%1` değil.** İlk deneme `%1` ile yapıldı ve bozuldu:
Türkçe yüzdeyi sayıdan önce yazıyor ("%40", "%5'i"), yani metinlerin içinde bol
bol `%` geçiyor ve "disk işlemlerinin %5'i" ifadesindeki `%5` yer tutucu
sanılıyordu. Kaçış kuralı eklemek yerine çakışmayan bir işaret seçildi —
çeviri yapan kişiye kural öğretmemek doğru. `EvidencePart::format` ve
`ss18::Tf` aynı biçimi kullanıyor.

**HTML rapor da çevriliyor — kuralı kırmadan.** `report_html.cpp` taşınabilir
kütüphanede duruyor ve dil katmanı Windows'a bağlı; ikisini doğrudan bağlamak
tasarım kuralı 5'i kırardı. Çözüm: rapor üreteci çeviriyi **kendi yapmıyor**,
kendisine verilen `ss::Translator` kancasını çağırıyor. Kanca verilmezse
metinler olduğu gibi kalıyor — yani taşınabilirlik bozulmuyor ve Türkçe çıktı
bit bit aynı. Kancayı `ss18::translator()` dolduruyor; arayüz ve komut satırı
aynı kancayı kullanıyor ki iki farklı davranış oluşmasın.

## Sıradaki iş — öncelik sırası

Beş paralel araştırma ajanının bulguları ve bunları çürütmeye çalışan
bağımsız değerlendirmeler sonucunda çıkan sıra (tam plan:
`docs/sinyal-plani.md`):

~~1. Sinyal kanalı bağlanmalı (P0).~~ **BİTTİ** — `telemetry_signals.cpp`
   `SignalSnapshot`'ı dolduruyor, `frame_source.cpp:219` kullanıyor.

~~2. Bellek baskısı (P1).~~ **BİTTİ** — `telemetry.cpp:481` `GetPerformanceInfo`
   ile commit baskısını okuyor.

~~3. Olay günlüğü okuyucu.~~ **BİTTİ** — `event_log.cpp`. WHEA, Kernel-Power 41,
   bugcheck 1001, TDR 4101, depolama 129/153/7/11/51, Ntfs 55, 6008,
   LiveKernelReports. Yönetici hakkı gerekmiyor. Doğrulama: `ss_cli eventlog`.

~~4. Depolama envanteri (P2).~~ **BİTTİ** — `storage_probe.cpp`. Model, veri
   yolu, dönen disk mi, bölüm doluluk oranı, SMART arıza tahmini, NVMe aşınma.
   Aşınma yüzdesi **bağlam** olarak sunuluyor, sebep olarak değil. SMART
   yönetici hakkı istiyor; yoksa alanlar "okunamadı" kalıyor, sıfır yazılmıyor.
   Doğrulama: `ss_cli storage`.

~~5. DPC toplayıcı.~~ **TOPLAYICI BİTTİ** — `dpc_source.cpp`. NT Kernel Logger,
   `EVENT_TRACE_FLAG_DPC`. Doğrulandı: 16,5 saniyede 18.666 olay, **0
   çözülemeyen adres**. Doğrulama: `ss_cli dpc` (yönetici).

   **Kritik not — `EnumDeviceDrivers` yönetici istiyor.** Windows 10 1607'den
   beri çekirdek adresleri yükseltilmemiş sürece verilmiyor (KASLR
   sertleştirmesi). Yani modül tablosu da yönetici gerektiriyor; testler bu
   durumda BAŞARISIZ değil ATLANMIŞ sayılıyor.

   **Kritik not — sınıflandırma sonradan yapılmalı.** İlk tasarımda olay
   geldiği anda "pencere içinde mi" diye işaretleniyordu; bu yanlış, çünkü
   takılma pencereleri ölçüm bittikten sonra belli oluyor. Ham kayıt tutulup
   `summarize()` ile sonradan sınıflandırılıyor.

~~6. DPC'yi ölçüme bağla.~~ **BİTTİ** — `ss_ui.cpp`, `onCaptureDone`. DPC oturumu
   kare yakalamasıyla eş zamanlı çalışıyor, sonra iki geçişli analiz.

   **İki geçiş şart.** Kontrol grubu takılma pencerelerini gerektiriyor,
   pencereler ise ancak dedektör çalıştıktan sonra belli oluyor: önce analiz,
   sonra suçlu tespiti, sonra analiz tekrar.

   **Hizalama şart.** İki ayrı ETW oturumunun sıfır noktası farklı ama saati
   aynı (QPC). `LongDpc::qpc` mutlak tutuluyor, `EtwCaptureResult::firstQpc`
   dışarı veriliyor. Bu adım atlanırsa sürücü **rastgele** suçlanır.

   **Suçlama eşiği** `dpc_source.h` içinde tek yerde: lift ≥ 3×, pencere içi
   ≥ 3 olay, pencere toplamı ≥ 50 ms. Eşiği geçen yoksa `dpcDriver` boş
   bırakılıyor ve KURAL 1 hiç ateşleyemiyor.

   **Aday seçimi LIFT'e göre, sayıya göre DEĞİL.** Bu bir testle yakalandı:
   gürültülü sürücü her yere düştüğü için pencerelerde de en yüksek sayıya
   sahip olur; sayıya göre seçmek tam da elemek istediğimiz sürücüyü öne
   çıkarıyordu. `test_winlayer.cpp` VAKA A ve E bunu koruyor.

**Kalanlar:**

1. **ETW sistem logger.** `DISK_IO` + `MEMORY_HARD_FAULTS` + `THREAD`.

**Olay günlüğü kurallarının tasarım sözleşmesi.** Sayım tek başına asla hüküm
değildir. Her kural iki ayrı yol izler:

| Durum | Anlam | Ağırlık |
|---|---|---|
| Olay ölçüm penceresiyle çakışıyor | sinyal — sebep olabilir | yüksek (65-70) |
| Olay yalnızca geçmişte | bağlam | düşük (20-45) |

Düzeltilmiş WHEA ayrıca **başka kanıt olmadan hiç sayılmaz**
(`core.cpp`, `otherMemoryEvidence`). Bu kilit `test_core.cpp` VAKA 16 ile
korunuyor — bozulursa motor sağlıklı her AM5 makinesine "belleğin kararsız" der.

**Not:** Dizüstü güç limiti vakası (GPU 140 W yerine 40 W) **hiçbir
araştırma başlığının kapsamında değil**; ayrı ele alınmalı.

## Eski not — kendi ETW toplayıcımız (tamamlandı)

**ETW frame time toplayıcı.** `Microsoft-Windows-DxgKrnl` sağlayıcısı,
GUID `{802EC45A-1E99-4B83-9920-87C98277BA9D}`.

Intel'in **PresentMon** projesi (MIT lisanslı) bu işi zaten yapıyor;
`PresentData` katmanı doğrudan kullanılabilir. Sıfırdan yazmak yerine onu
entegre et — projenin en zor parçasını haftalardan günlere indirir.

Ara adım olarak `CsvFrameSource` zaten çalışıyor: PresentMon.exe'nin CSV
çıktısı motora besleniyor. Yani **kendi ETW kodunu yazmadan çalışan bir ürün
çıkarılabilir.**

---

## Sürüm ve güncelleme (2026-07-31)

`version.h` **tek kaynak.** Daha önce sürüm `ss_cli.cpp` içinde iki yerde metin
olarak gömülüydü ("StutterScope v0.1") ve arayüzde hiç görünmüyordu. Şimdi
komut satırı başlığı, Ayarlar sayfası, `ss_ui.rc` içindeki VERSIONINFO ve
güncelleme karşılaştırması hep buradan besleniyor. GitHub etiketi `v0.2.0`
biçiminde olmalı.

**Otomatik güncelleyici YAZILMAYACAK.** Bu tartışmaya kapalı:
binary imzasız dağıtılıyor **ve** uygulama ölçüm için yönetici hakkıyla
çalışıyor. Yönetici süreçte imzasız bir exe indirip çalıştırmak ayrıcalık
yükseltme zafiyetidir ve zararlı yazılımların birebir aynı desenidir; antivirüs
de bunu işaretler. Program yalnızca **denetler**, indirmeyi kullanıcı yapar.

**Kendi sunucumuz yok.** `update_check.cpp` GitHub Releases API'sini kullanıyor
(`api.github.com/repos/VS-57/syspect/releases/latest`). Kimlik doğrulamasız
istek sınırı 60/saat/IP — açılışta bir kez denetleyen bir program için yeterli.

**Telemetri yok ve eklenmeyecek gibi düşünülmeli.** Bu uygulama kullanıcının
donanımını, olay günlüğünü ve çökme kayıtlarını okuyor, üstüne yönetici hakkı
istiyor. Hedef kitle makinesinde ne çalıştığından şüphelenen insanlar. Eve
telefon ederse güven kaybı ölçüm değerinden büyük olur — disk seri numarası bile
bu yüzden bilerek okunmuyor. İhtiyaç duyulan sayım zaten bedava: sürüm denetimi
isteklerini sunucu tarafında saymak kaç kurulum ve hangi sürümler sorusunu
kalıcı kimlik olmadan cevaplıyor.

## Dağıtım

İmzasız, GitHub Releases üzerinden. Kod imzalama sertifikası yalnızca kernel
sürücüsü için zorunlu; normal `.exe` imzasız çalışır, sadece SmartScreen
uyarısı gösterir. Sertifika (EV, ~500 USD/yıl + şahıs şirketi) ürün tutunca
alınacak, önce değil.

`LICENSES.txt` kuruluma dahil edilmeli — MIT telif bildirimini taşımak
zorunlu.
