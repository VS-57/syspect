# Oyun Kararlılık Tanı Aracı — Teknik Tasarım Dokümanı

**Kod adı:** StutterScope (geçici)
**Hedef platform:** Windows 10 (1809+) ve Windows 11, x64
**Sürüm:** Tasarım v1.1 — 26 Temmuz 2026
**Değişiklik:** v1.0'daki 10 teknik hata düzeltildi (Bölüm 5, 7, 8). Gerçek vaka külliyatı eklendi (Bölüm 6) ve ürün odağı bu külliyata göre yeniden hizalandı.

---

## 1. Ürünün tek cümlelik tanımı

Oyuncunun *"bilgisayarım donuyor / mavi ekran veriyor"* şikayetini, kullanıcıdan hiçbir teknik bilgi istemeden **sebebe indirgeyip somut tek bir aksiyon öneren** tanı aracı.

**Bu bir izleme aracı değil, teşhis aracıdır.** Piyasadaki farkımız bu. PresentMon, CapFrameX, LatencyMon, HWiNFO ve MSI Afterburner zaten mükemmel veri topluyor — ama hepsi kullanıcıya grafik verip yorumu ona bırakıyor. Hedef kitlemiz o grafiği okuyamayan kişi. Ürünün değeri veri toplamada değil, **hüküm vermede**.

Başarı kriteri şu ekranı üretebilmek:

> **Son 7 günde 34 takılma, 1 mavi ekran tespit edildi.**
> Ana şüpheli: **Bellek/EXPO kararsızlığı**
> Son 30 günde 412 adet düzeltilmiş donanım hatası (WHEA) kaydedildi ve takılmaların %71'i bunlarla çakışıyor.
> **Yapılacak:** BIOS'ta EXPO profilini kapatıp bir gün deneyin. Sorun geçerse bellek hızını 6000 MT/s'e düşürüp EXPO'yu tekrar açın. → [Nasıl yapılır]

---

## 2. Verilen kararlar ve gerekçeleri

| Konu | Karar | Gerekçe |
|---|---|---|
| Dil / stack | **C++20, MSVC, Win32** | .NET bağımlılığı yok, statik CRT ile VC++ redist yok. ETW/WMI/DXGI/NVML'e doğrudan erişim. Kernel sürücü zaten C/C++ olmak zorunda — tek toolchain. |
| CRT | `/MT` statik link | Kullanıcıya "şunu da yükle" dedirtmez |
| Arayüz | WebView2 + yerel HTML | Win11'de yerleşik, Win10'da çoğu makinede zaten var. UI sadece pencere açıkken RAM tüketir. |
| Veritabanı | SQLite amalgamation (tek `.c`) | Statik link, bağımlılık yok, ~700 KB |
| Kurulum | Inno Setup, tek `.exe`, ~10–15 MB | Kurulum kabul edildi, ön koşul yok |
| CPU sıcaklığı | **Kendi KMDF sürücümüz** — WinRing0 **değil** | Bkz. Bölüm 3 |
| Sürücü ne zaman yüklenir | Kurulumda **değil**, kullanıcı açıkça isteyince | Anti-cheat ve Defender riski varsayılan kullanıcıya bulaşmasın |
| Oyun içi overlay | **Hook/injection yok**, sadece katmanlı ayrı pencere | Injection = ban sebebi |

### Neden C++, Rust değil?

İstenen üç şey — en az kaynak tüketimi, tam ve geleceğe dönük Windows API erişimi, .NET bağımsızlığı — üçü de C++ ve Rust'ta karşılanıyor. Ayrım noktası: **bu projede kernel-mode sürücü var** ve sürücü WDK ile C/C++ yazılmak zorunda. Rust seçilirse usermode Rust + kernelmode C++ olur; iki toolchain, iki FFI katmanı, ortak veri yapılarının iki kez tanımlanması. C++ ile tek Visual Studio çözümü. Ayrıca ETW consumer API'si, DXGI, WMI, PDH ve vendor SDK'ları (NVML, ADLX, IGCL) hepsi birinci sınıf C++ başlıklarıyla geliyor.

---

## 3. En kritik karar: CPU sıcaklığı vs. anti-cheat

### Sorun

CPU çekirdek sıcaklığı, işlemcinin MSR yazmaçlarını okumayı gerektirir — Intel'de `IA32_THERM_STATUS`, AMD'de SMN üzerinden `THM_TCON_CUR_TMP`. Bu yalnızca ring 0'da yapılabilir.

> **Nüans (v1.0'da yanlış yazılmıştı):** "Kullanıcı modundan hiçbir yolu yok" ifadesi abartılıydı. `ROOT\WMI` altındaki `MSAcpi_ThermalZoneTemperature` sınıfı sürücüsüz okunabilir. Ancak bu genellikle **anakart termal bölgesidir, CPU die değil**; birçok masaüstü anakartında hiç implemente edilmemiştir ve güncelleme sıklığı çok düşüktür. Doğru ifade: *çekirdek başına hassas DTS/Tctl sıcaklığı* ring 0 gerektirir.

### WinRing0 neden kullanılamaz

WinRing0 **CVE-2020-14979** ile kayıtlı bir açık taşıyor: düşük yetkili bir process, sürücü üzerinden rastgele fiziksel bellek okuyup yazabiliyor ve SYSTEM yetkisine yükselebiliyor.

- Microsoft Defender bunu `VulnerableDriver:WinNT/Winring0` olarak **karantinaya alıyor**.
- Microsoft'un vulnerable driver blocklist'i **Windows 11 2022 Update'ten beri varsayılan açık**.
- HVCI / Bellek Bütünlüğü açık sistemlerde yüklenmiyor.

İnce nokta: WinRing0 bir dönem anti-cheat'ler tarafından *beyaz listeye alınmıştı*. Yani sorun tarihsel olarak anti-cheat değil, **Microsoft'un kendisi** oldu. Bugün WinRing0 kullanmak, kullanıcının Defender'ının uygulamanı virüs sanması demek.

### Çözüm: kendi minimal sürücümüz

WinRing0 kara listede çünkü **rastgele** fiziksel bellek erişimi veriyor — bu bir exploit primitifidir. Bizimki değil:

- **Sadece okuma.** Hiçbir MSR yazma yolu yok.
- **Beyaz listeli register.** Derleme zamanında sabitlenmiş sensör listesi. IOCTL'e adres geçirilemez, yalnızca sensör kimliği.
- **Çağıran doğrulaması.** Yalnızca imzalı servisimiz konuşabilir.
- **Minimum yüzey.** Tek IOCTL, sabit boyut, `METHOD_BUFFERED`.

**Bedeli:** EV kod imzalama sertifikası (~300–600 USD/yıl) + Microsoft Partner Center attestation signing. Süreç 2–4 hafta — **projenin ilk gününde başlatılmalı.**

### İki modlu çalışma

Sürücünün varlığı hiçbir zaman şart olamaz.

**Tanı Modu** — Sürücü aktif, tam sensör seti.
**Gölge Mod** — Korumalı oyun süreci görülünce servis sürücüyü SCM üzerinden durdurur, yalnızca ETW/WMI/PDH/vendor API ile devam eder.

Tespit: `vgk.sys` (Vanguard), `EasyAntiCheat.sys`, `BEDaisy.sys`, `mhyprot`. Liste uzaktan güncellenebilir JSON.

> **Düzeltme:** v1.0'da "`SERVICE_CONTROL_STOP` + `NtUnloadDriver`" yazıyordu. `NtUnloadDriver` belgelenmemiş bir ntdll native API'sidir, `SeLoadDriverPrivilege` ister ve `NtLoadDriver` ile yüklenenler içindir. Sürücü servis olarak kayıtlıysa **SCM'nin `SERVICE_CONTROL_STOP`'u zaten imajı boşaltır.** İkincisi gereksiz, hata döndürür ve belgelenmemiş native çağrılar AV/anti-cheat sezgisel tetikleyicisidir — kendi "Defender yanlış pozitifi" riskimizle çelişir. Sürücüde `EvtDriverUnload` rutini bulunmalıdır.

### Kaybın gerçek boyutu

> **Önemli düzeltme.** v1.0'da şu iddia vardı: *"`Microsoft-Windows-Kernel-Processor-Power` throttle sebebini doğrudan söyler."* **Bu yanlıştır.** Sağlayıcı adı doğru ama böyle bir dört kategorili çıktı yok. Gerçekte olanlar `PPM_ETW_THERMAL_CAP_CHANGE` (48), `PPM_ETW_BIOS_CAP_CHANGE` (47), `PPM_ETW_PCC_CAP_CHANGE` (46) — tek alanları `Cap`, yani OS'in gördüğü **performans tavanı** değişimi; gerçek zamanlı throttle olayı değil. `PPM_ETW_PERF_CONSTRAINT_CHANGE` (61) `LimitReasons` taşır ama PPM/PEP sürücüleri tarafından yazılır ve PEP'i olmayan klasik x86 masaüstlerinde hiç üretilmeyebilir. `PPM_ETW_THROTTLE_STATES_ERROR` ise ACPI T-state firmware **hatası** bildirimidir, ısınma değil.
>
> Asıl sebep: Intel PL1/PL2, EDP/akım limiti, PROCHOT ve AMD PPT/TDC/EDC kısıtları **donanım tarafından özerk** uygulanır ve Windows güç yöneticisine görünmez. HWiNFO tam bu yüzden ring 0'da MSR `0x690` (`MSR_CORE_PERF_LIMIT_REASONS`) okur.

Bu düzeltme, Gölge Mod'da termal teşhisi zayıflatır. **Ancak Bölüm 6'daki gerçek vaka analizi gösteriyor ki bu neredeyse hiç önemli değil:** vakaların hiçbirinde sebep sıcaklık değil ve kullanıcılar sıcaklığı zaten kendileri elemiş durumda. Sıcaklık modülü bir *konfor özelliğidir*, teşhis motorunun omurgası değildir. Kural tekrarlanır: **hiçbir teşhis kuralı CPU sıcaklığına bağımlı olamaz.**

Gölge Mod'da termal için kullanılabilecek dolaylı sinyal: PDH sayacı `Processor Information\% Processor Performance` düşüşü + GPU tarafında NVML/ADLX'in verdiği (sürücüsüz erişilebilen) throttle bayrakları.

---

## 4. Bileşen mimarisi

```
┌─────────────────────────────────────────────────────────┐
│  ss_ui.exe          Sadece pencere açıkken çalışır       │
│  WebView2 + yerel HTML/CSS/JS                            │
└───────────────────────┬─────────────────────────────────┘
                        │  \\.\pipe\stutterscope
┌───────────────────────┴─────────────────────────────────┐
│  ss_svc.exe         Windows servisi, LocalSystem         │
│  ┌──────────┬──────────┬──────────┬──────────┬────────┐ │
│  │ ETW      │ Sensör   │ Çökme    │ Sistem   │ Teşhis │ │
│  │ toplayıcı│ toplayıcı│ izleyici │ envanteri│ motoru │ │
│  └──────────┴──────────┴──────────┴──────────┴────────┘ │
│  Halka arabellek (son 60 sn) → SQLite (olaylar)          │
│  Kara kutu: 2 sn'de bir diske flush (donma hayatta kalır)│
│  Hedef: < 30 MB RAM, < %0.5 CPU                          │
└───────────────────────┬─────────────────────────────────┘
                        │  tek IOCTL, opsiyonel
┌───────────────────────┴─────────────────────────────────┐
│  ss_drv.sys         KMDF, salt okunur, talep üzerine     │
└─────────────────────────────────────────────────────────┘
```

**Neden servis + ayrı UI?** Kullanıcı donmanın ne zaman olacağını bilmiyor. Servis 7/24 düşük maliyetle dinler. Bu, "çok basit kullanım" hedefinin temeli: **kullanıcının bir şeyi başlatması gerekmez.**

**Kara kutu (yeni, v1.1):** Vaka 5 ve 6 gösteriyor ki en can sıkıcı arızalar **minidump üretmeyen sert donmalar**. Sistem kilitlenirse bellekteki halka arabellek kaybolur. Bu yüzden son 60 saniyenin sıkıştırılmış özeti 2 saniyede bir küçük bir dosyaya yazılır. Sistem sert donup elle resetlendiğinde, açılışta bu dosya bulunur ve **donma anındaki son 60 saniye yeniden inşa edilir.** Piyasada bunu yapan araç yok.

---

## 5. Veri kaynakları

Ring 0 gerektiren tek satır CPU çekirdek sıcaklığıdır.

| Sinyal | Kaynak | Ring 0? | AC riski |
|---|---|:---:|:---:|
| Frame time / present | ETW `Microsoft-Windows-DxgKrnl` | Hayır | Yok |
| Present modu, VSync, tearing | ETW `Microsoft-Windows-DxgKrnl` (`VSyncDPC`, `PresentHistory`) | Hayır | Yok |
| DPC / ISR gecikmesi ve suçlu sürücü | ETW Kernel Logger — `PERF_DPC`, `PERF_INTERRUPT` | Hayır | Yok |
| CPU gerçek performans oranı | **PDH: `Processor Information\% Processor Performance`** | Hayır | Yok |
| Park edilmiş çekirdek | **PDH: `Processor Information\Parking Status`** veya ETW `PPM_ETW_PARK_CORE`/`UNPARK_CORE` | Hayır | Yok |
| Hard page fault | ETW Kernel — hard fault olayları | Hayır | Yok |
| Disk I/O gecikmesi | ETW Kernel — `PERF_DISK_IO` | Hayır | Yok |
| Context switch / ready gecikmesi | ETW Kernel — `PERF_CONTEXT_SWITCH` | Hayır | Yok |
| GPU sıcaklık, saat, fan, throttle | NVIDIA `nvml.dll` · AMD ADLX · Intel IGCL | Hayır | Yok |
| **VRAM kullanımı (sistem geneli)** | **ETW DxgKrnl VidMm olayları veya PDH `GPU Process Memory`** | Hayır | Yok |
| **CPU çekirdek sıcaklığı** | **MSR — kendi sürücümüz** | **Evet** | **Var** |
| Mavi ekran | `%SystemRoot%\Minidump\*.dmp` + Olay Günlüğü 1001 | Hayır | Yok |
| Donanımsal hata (WHEA) | Olay Günlüğü — bkz. 8.2 | Hayır | Yok |
| GPU çökmesi (mavi ekransız) | `%SystemRoot%\LiveKernelReports\` | Hayır | Yok |
| Ekran bağlantısı kopması | ETW DxgKrnl `HotPlug`/`TargetMode` + Olay Günlüğü `Display` | Hayır | Yok |
| RAM hızı (JEDEC/yapılandırılmış) | SMBIOS Type 17 — `GetSystemFirmwareTable('RSMB')` | Hayır | Yok |
| SSD sağlığı (NVMe) | `IOCTL_STORAGE_QUERY_PROPERTY` + `ProtocolTypeNvme` | Hayır | Yok |
| SSD/HDD sağlığı (SATA) | `SMART_RCV_DRIVE_DATA` / `IOCTL_ATA_PASS_THROUGH` | Hayır | Yok |
| Sürücü sürümleri ve tarihleri | SetupAPI / `EnumDeviceDrivers` | Hayır | Yok |

### v1.0'a göre düzeltilen satırlar

**`CallNtPowerInformation(ProcessorInformation)` kaldırıldı.** İki sebeple: (a) `PROCESSOR_POWER_INFORMATION` yapısında park bilgisi **yoktur** (alanlar: `Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState`); (b) Windows 10/11'de **`CurrentMhz` artık `MaxMhz` ile aynı değeri döndürüyor** — gerçek anlık frekans bu yolla ölçülemez. Yerine PDH sayaçları kondu.

**`IDXGIAdapter3::QueryVideoMemoryInfo` kaldırıldı.** Bu metot **çağıran process'in** bütçesini döndürür, sistem genelini değil. `ss_svc.exe` bunu çağırırsa kendi sıfıra yakın kullanımını okur, oyunun VRAM'ini göremez. Yerine DxgKrnl VidMm ETW olayları ve `GPU Process Memory` sayaç seti kondu.

**XMP/EXPO durumu satırı kaldırıldı.** SMBIOS Type 17 yalnızca **yapılandırılmış hızı** verir. XMP/EXPO profil verisi modülün SPD EEPROM'undadır, SMBus üzerinden okunur, bu da ring 0 gerektirir. Yapılabilecek tek şey **çıkarım**: yapılandırılmış hız > JEDEC taban hızı ⇒ muhtemelen XMP/EXPO açık. Bu çıkarım Bölüm 7'deki bellek kuralı için yeterlidir ve sürücüsüz çalışır.

**SATA SMART ayrı satıra çıkarıldı.** `IOCTL_STORAGE_QUERY_PROPERTY` NVMe içindir; SATA farklı IOCTL ister. Birçok USB köprüsü ve RAID denetleyicisi her ikisini de bloke eder — zarif bozulma gerekir.

Not: `Microsoft-Windows-DxgKrnl` üzerinden frame time toplama, Intel'in **PresentMon** (MIT lisanslı) projesinin yöntemidir. `PresentData` katmanı doğrudan kullanılabilir; bu, projenin en zor parçasını haftalardan günlere indirir. PresentMon 2.5.0 ile ETW oturumu başlatma ve sağlayıcı etkinleştirme ayrıldı — bazı katı anti-cheat'li oyunlarda ETW toplamanın engellenmesini azaltıyor; bu davranışı taklit etmeliyiz.

---

## 6. Vaka külliyatı — ürünün gerçek test kümesi

Aşağıdaki 8 vaka, Türk donanım forumlarından gerçek kullanıcı şikayetleridir. Kural motoru bunlara karşı geliştirilmeli ve **regresyon testi olarak** kullanılmalıdır: her yeni kural bu 8 vakada doğru hükmü vermelidir.

### 6.1 Vakalar

| # | Sistem | Şikayet | Gerçek şüpheli |
|---|---|---|---|
| 1 | 9950X3D · 5080 · B850 · 48GB 6400 CL32 | Opera'da donma, Valorant'ta 400–750 FPS dalgalanma, program açarken ve açılıştan sonra fare takılması. **Curve Optimizer −25.** | **Negatif CO kararsızlığı.** Oyun-dışı (Opera, fare) semptomlar sistem geneli demek. |
| 2 | X870-Plus Wi-Fi | Ekran gidiyor, "DisplayPort no signal", monitör kapanıyor, **PC çalışmaya devam ediyor** | Ekran bağlantısı/GPU link kaybı. Mavi ekran yok ⇒ minidump yok. |
| 3 | X870-Plus Wi-Fi | Sadece arka **USB4 Type-C** portunda Code 43 / Device Descriptor Failed. Mavi USB 3.2 portları sorunsuz. | USB4/Thunderbolt firmware. Temiz kurulum, BIOS, sürücü denenmiş. |
| 4 | 7800X3D · 5070 · B850M-K · 2×16 CL30 | CPU-yoğun anlarda takılma (Valorant çatışma), AAA'da nadir. **Sıcaklık ve frekans normal, SSD %100, DDU, PBO off, C-state denemeleri fayda vermedi.** | EXPO/IMC kararsızlığı veya DPC. Kullanıcı tüm bariz sebepleri elemiş. |
| 5 | 7500F · B850 Tomahawk · XPG 2×16 6000 CL30 | 2 yıldır sorun. RAM hiç 4800 üstü çalışmadı. RAM, SSD, anakart değişti. Şimdi **crash vermeden ekran donuyor** | IMC / bellek kararsızlığı. **Dump üretmiyor** — kara kutu şart. |
| 6 | 7800X3D · 5060 Ti · X670-P · **1×32GB 6400 CL40** · **Rampage 750W Bronze** | Basit oyunlarda (Minecraft, Roblox) siyah ekran → masaüstüne dönüş. DDU sonrası 4 ay sorunsuz, sonra tekrarladı. | İki şüpheli: AM5'te 6400 MT/s güvenli sınırın üstü **ve** düşük kaliteli PSU. Siyah ekran→masaüstü = TDR. |
| 7 | 9800X3D · 5070 Ti · B650 Tomahawk | 1080p'de sorun yoktu. **240 Hz OLED 1440p monitöre geçince** PUBG'de ufak takılmalar başladı. Her şey güncel. | VRR/frame pacing veya artık GPU-bound olma. Değişken tek: monitör. |
| 8 | 5600 · RX 9060 XT · A520M Pro · **16GB** · **550W** | LoL/CS sorunsuz, Forza/Witcher'da FPS düşüşü, Windows'ta anlık takılmalar | Dengesiz sistem: CPU darboğazı + muhtemel tek kanal RAM + zayıf PSU. |

### 6.2 Külliyattan çıkan ürün kararları

**a) Sıcaklık teşhis için neredeyse değersiz.** 8 vakanın hiçbirinde sebep sıcaklık değil. Vaka 4 ve 5'te kullanıcı sıcaklığı **zaten kendisi ölçüp elemiş**. Bu, Bölüm 3'teki kernel sürücü riskini stratejik olarak önemsizleştirir: **sürücü v1.0'a rahatlıkla ertelenebilir, hatta hiç yapılmayabilir.** Kullanıcılar sıcaklığı görmeyi *ister* ama sorunları sıcaklık *değildir*.

**b) AM5 bellek/IMC kararsızlığı bir numaralı temadır.** 8 vakanın 4'ü (1, 4, 5, 6) buraya işaret ediyor. Ürün bu tek sebepte mükemmel olursa, hedef kitlenin yarısını çözer. Bu, v0.1'in odağı olmalıdır.

**c) "Oyun-dışı semptom" en güçlü ayırt edicidir.** Vaka 1'de Opera donması ve fare takılması var — bu, sorunun oyunda değil sistemde olduğunu tek başına söyler. Kural motoruna eklenmesi gereken en değerli sinyal: **takılmalar sadece oyunda mı, yoksa masaüstünde de mi oluyor?**

**d) Undervolt / Curve Optimizer sorgulanmalı.** Vaka 1 doğrudan soruyor. CO negatif değerleri AM5'te yaygın kararsızlık sebebi ve semptomu tam olarak "rastgele donma, açılışta takılma". Uygulama CO/PBO/EXPO durumunu tespit edip kullanıcıya sormalı.

**e) Kapsam sadece "stutter" değil.** Vaka 2 (DisplayPort sinyal kaybı) ve Vaka 3 (USB4 Code 43) takılma değil ama aynı kullanıcı aynı araçtan cevap bekliyor. Ürün kendini *"oyun takılma dedektörü"* değil, **"PC kararlılık doktoru"** olarak konumlandırmalı.

**f) Donanım dengesi kontrolü şart.** Vaka 6 ve 8'de PSU kalitesi ve RAM yapılandırması sorunun kaynağı. Bunun için sürücü değil, **veri tabanı** gerekir: PSU model→kalite katmanı listesi, platform→güvenli bellek hızı tablosu (AM5 için 2 çubuk 6000, 4 çubuk 5600, tek çubuk >6000 riskli), CPU-GPU denge matrisi.

**g) "Dump üretmeyen donma" birinci sınıf senaryodur.** Vaka 2, 5 ve 6'da mavi ekran yok. Minidump modülü tek başına yetmez; kara kutu mekanizması (Bölüm 4) bu vakaların **tek** veri kaynağıdır.

**h) Değişiklik günlüğü tutulmalı.** Vaka 7'nin cevabı "monitörü değiştirdim"de gizli. Uygulama donanım ve sürücü değişikliklerini tarihli olarak kaydederse, "sorun 12 Mart'ta başladı, 11 Mart'ta NVIDIA sürücüsü güncellenmişti" gibi hükümler kurabilir. **Bu, tek başına bir özellik olarak bile satılabilir.**

---

## 7. Takılma tespiti ve teşhis motoru

### 7.1 Olay tespiti

Sabit eşik işe yaramaz — 60 FPS'te normal olan 144 Hz'de felakettir.

```
pencere = son 120 frame
taban   = pencerenin medyanı
eşik    = max(2.0 × taban, taban + 8 ms)
olay    = frametime > eşik
```

Medyan kullanılır; ortalama takılmaların kendisi tarafından kirletilir.

### 7.2 Sınıflandırma

| Sınıf | İmza | Tipik sebep ailesi |
|---|---|---|
| **Mikro-takılma** | 1–2 frame, düzenli tekrar | Frame pacing, VRR/V-Sync çakışması *(Vaka 7)* |
| **Hitch** | 50–500 ms tek sıçrama | Shader derleme, varlık yükleme, hard fault, DPC |
| **Donma** | > 500 ms, sistem geri geliyor | Sürücü TDR, disk, VRAM *(Vaka 6)* |
| **Sert kilitlenme** | Sistem geri gelmiyor, dump yok | IMC/bellek kararsızlığı *(Vaka 5)* |
| **Sinyal kaybı** | Ekran gidiyor, PC çalışıyor | GPU link/TDR *(Vaka 2)* |
| **Düşük FPS** | Takılma yok, düz ve düşük | Yetersiz donanım *(Vaka 8)* — **takılma değildir** |

### 7.3 Kural seti

Kurallar tek sebep seçmez, her sebebe **güven skoru** verir. Tek takılma bir şey kanıtlamaz; 34 takılmanın 27'sinin aynı sinyalle çakışması kanıtlar.

| Kural | Tetikleyici | Kullanıcıya |
|---|---|---|
| **Bellek/EXPO kararsızlığı** ⭐ | WHEA corrected sayısı yüksek **veya** yapılandırılmış hız > platform güvenli sınırı **ve** sert donmalar dump üretmiyor | "Bellek ayarları kararsız. EXPO'yu kapatıp deneyin." |
| **Undervolt / CO** ⭐ | Negatif CO tespit edildi **ve** oyun-dışı takılma var **ve** WHEA corrected mevcut | "Undervolt kararsızlık yapıyor. CO'yu sıfırlayıp test edin." |
| **Sürücü DPC** | Tek bir `.sys`'in DPC süresi > 1 ms **ve** olayların > %40'ı | "X sürücüsü sistemi kilitliyor. Güncelleyin." |
| **VRAM taşması** | VidMm sayfalama trafiği + GPU bellek sayacı tavanda | "Doku kalitesini bir kademe düşürün." |
| **Shader derleme** | İlk 10 dk yoğun, sonra azalan + tek çekirdek sıçraması | "Normal, birkaç dakikada geçecek." |
| **VRR / frame pacing** | Mikro-takılma düzenli + refresh oranı ile faz ilişkisi | "G-Sync/FreeSync ve V-Sync ayarını kontrol edin." |
| **Hard fault / RAM yetersiz** | Hard fault sıçraması + pagefile I/O | "RAM yetersiz kalıyor." |
| **Termal** | GPU throttle bayrağı veya `% Processor Performance` düşüşü | "Isı sebebiyle yavaşlıyor. Soğutma/toz." |
| **Güç planı / park** | `Parking Status` yüksek + throttle yok | "Güç planı performansı kısıtlıyor." |
| **Arka plan süreç** | Bir process'in sıçraması olayların > %30'u ile çakışıyor | "Y programı çakışıyor." |
| **Depolama** | Disk bekleme > 100 ms + SMART uyarısı | "Diskiniz yavaşlıyor." |
| **PSU şüphesi** ⭐ | Ani kapanma/TDR + PSU veritabanında düşük katman + yüksek çekişli GPU | "Güç kaynağı yetersiz olabilir." |
| **Dengesiz sistem** | CPU/GPU sınıf farkı + tek kanal RAM tespiti | "Takılma yok — bu ayarlar için donanım dengesiz." |
| **Değişiklikle korelasyon** ⭐ | Sorunların başlangıç tarihi bir sürücü/donanım değişikliğinden ≤ 3 gün sonra | "Sorun 11 Mart'taki NVIDIA güncellemesiyle başlamış." |

⭐ = vaka külliyatından doğan, rakiplerde bulunmayan kurallar.

Her sonuç üç parça üretir: **sebep + güven yüzdesi + tek cümlelik somut aksiyon.** Güven skoru mutlaka gösterilir; "kesin sebep bu" demek, yanlış olduğunda kullanıcıyı gereksiz donanım almaya iter.

---

## 8. Çökme ve donanım hatası modülü

Bu modül **WinDbg / Debugging Tools for Windows kurulumu gerektirmez.**

### 8.1 Mavi ekran — minidump ayrıştırma

> **Kritik düzeltme.** v1.0'da bu akış `dbghelp.dll` + `MiniDumpReadDumpStream` + `SystemInfoStream`/`ExceptionStream`/`ModuleListStream` üzerine kuruluydu. **Bu çalışmaz.** O API ve stream'ler **kullanıcı-mod** MDMP formatına aittir (imza `MDMP`). `%SystemRoot%\Minidump\` içindeki BSOD dosyaları **çekirdek** dump'ıdır: imza `PAGEDU64` (x64), yapı `_DMP_HEADER64`. dbghelp bu dosyaları açamaz.

Doğru akış:

1. `ReadDirectoryChangesW` ile `%SystemRoot%\Minidump\` izlenir.
2. Yeni `.dmp` görülünce başlık okunur; imza `PAGEDU64` doğrulanır.
3. **Bugcheck kodu `DUMP_HEADER64` içinde sabit ofset `0x38`'dedir**, 4 parametre hemen ardından gelir. Stream araması gerekmez.
4. Yüklü modül listesi `KdDebuggerDataBlock` → `PsLoadedModuleList` üzerinden çözülür.
5. Bugcheck parametrelerindeki hata adresi modül aralıklarıyla eşleştirilerek **suçlu sürücü** bulunur.
6. Kod, insan diline çeviren sözlükten geçirilir.

Kendi `DUMP_HEADER64` ayrıştırıcımızı yazıyoruz. Açık kaynaklı referanslar mevcut (`kdmp-parser`, Volatility'nin crash address space belgesi). **`dbgeng.dll` kullanılmayacak** — Windows'ta yerleşik değildir, Debugging Tools ile gelir ve bu bölümün "kurulum gerektirmez" vaadini bozar.

### 8.2 WHEA — düzeltilmiş sınıflandırma

> **Kritik düzeltme.** v1.0'da *"WHEA olayları (ID 17/18/19/47) donanım hatasıdır, genellikle overclock/XMP anlamına gelir"* yazıyordu. **Bu sınıflandırma yanlış ve tehlikelidir.**

| ID | Sınıf | Anlam |
|---|---|---|
| **18** | **Fatal** | "A fatal hardware error has occurred" — **ciddi olan tek ID** |
| 17, 19, 46, 47 | **Corrected** | Düzeltilmiş hata, Uyarı seviyesi, **çoğu zaman zararsız** |
| 1, 20 | Diğer | v1.0 listesinde atlanmıştı |

Özellikle **ID 17 (PCIe AER corrected)** birçok AMD çipsetli sistemde hiçbir semptom olmadan binlerce kez üretilir. Dördünü birden "donanım arızası" olarak sunmak, dokümanın kendi 1 numaralı riski olan **yanlış teşhisi doğrudan üretir.**

Kural motoru şunu yapmalıdır:

- **Fatal (18)** → yüksek öncelik, doğrudan bildir.
- **Corrected** → tek başına asla bildirme. Yalnızca **(a)** frekansı taban çizgisinin çok üstündeyse **ve (b)** takılma/donma olaylarıyla zamansal olarak çakışıyorsa sinyal say. Bu, Vaka 1, 4 ve 5'teki bellek/CO kararsızlığının parmak izidir.

### 8.3 Diğer kaynaklar

- **Olay Günlüğü ID 1001** (`BugCheck`) — minidump silinmişse bile kodu verir.
- **`%SystemRoot%\LiveKernelReports\`** — mavi ekran vermeden gerçekleşen GPU sürücü çökmeleri. *Vaka 2 ve 6'nın tek kanıtı burada.* **Rakiplerin neredeyse hiçbiri buraya bakmıyor.**
- **Display olayları** — ETW DxgKrnl hotplug/target-mode olayları ve `Display` kanalı: Vaka 2'deki "DisplayPort no signal" için.

### 8.4 Bugcheck sözlüğü

| Kod | Resmi ad | Kullanıcıya |
|---|---|---|
| `0x133` | DPC_WATCHDOG_VIOLATION | Bir sürücü işlemciyi çok uzun bloke etti |
| `0x1A` | MEMORY_MANAGEMENT | Bellek sorunu — RAM veya EXPO/XMP |
| `0x116` | **VIDEO_TDR_FAILURE** | Ekran kartı sürücüsü yanıt vermedi |
| `0x117` | **VIDEO_TDR_TIMEOUT_DETECTED** | Ekran kartı zaman aşımına uğradı |
| `0x50` | PAGE_FAULT_IN_NONPAGED_AREA | Geçersiz bellek erişimi — sürücü veya RAM |
| `0x124` | WHEA_UNCORRECTABLE_ERROR | Donanım hatası — genellikle overclock |
| `0x9F` | DRIVER_POWER_STATE_FAILURE | Sürücü uyku/uyanma sırasında takıldı |

> Düzeltme: v1.0'da `0x116` için `VIDEO_TDR_ERROR` yazılmıştı; güncel resmi ad `VIDEO_TDR_FAILURE`'dır.

---

## 9. Arayüz tasarımı

"Çok basit kullanım": **tek ekran, tek ana mesaj, sıfır zorunlu kullanıcı eylemi.**

**Ana ekran** — üç öğe:

1. Büyük durum kartı: son 7 günün özeti ve ana şüpheli.
2. Tek eylem butonu: **"Şimdi Test Et"** (yoğun izleme oturumu).
3. Küçük bağlantı: "Gelişmiş görünüm".

**Gelişmiş sekmesi** — frame time grafiği, DPC tablosu, sensör geçmişi, değişiklik günlüğü. Varsayılan gizli.

**Rapor çıktısı** — tek tıkla panoya kopyalanabilir özet. Kullanıcı forumda yardım isterken yapıştırır. **Organik büyümenin motoru budur** — vaka külliyatındaki her gönderi bir dağıtım fırsatıdır. Seri numarası, kullanıcı adı ve makine adı **maskelenir**.

**Gizlilik** — tüm veri yerelde. Telemetri opt-in, kurulumda önceden işaretli değil.

---

## 10. Dağıtım ve boyut bütçesi

| Bileşen | Boyut |
|---|---|
| `ss_svc.exe` (statik CRT + SQLite) | ~3 MB |
| `ss_ui.exe` + gömülü HTML/CSS/JS | ~2 MB |
| `ss_drv.sys` | ~100 KB |
| PresentMon `PresentData` katmanı | ~1 MB |
| Donanım veri tabanları (PSU, bellek, denge) | ~1 MB |
| **Toplam** | **~10–15 MB** |

.NET yok, VC++ redist yok. WebView2: Win11'de yerleşik; Win10'da yoksa 1.5 MB sessiz bootstrapper. Sürücü kurulumda yer almaz.

---

## 11. Yol haritası

Vaka külliyatı ışığında **yeniden önceliklendirildi.** Sürücü sona atıldı, bellek/kararlılık teşhisi öne alındı.

| Sürüm | Kapsam | Süre |
|---|---|---|
| **v0.1** | ETW frame time, takılma tespiti, minidump (`PAGEDU64`) okuma, WHEA doğru sınıflandırma, **bellek/EXPO kuralı**, oyun-dışı semptom ayrımı | 4–6 hafta |
| **v0.2** | DPC suçlu tespiti, GPU vendor API, **kara kutu**, LiveKernelReports, WebView2 arayüzü, rapor çıktısı | +4 hafta |
| **v0.3** | **Değişiklik günlüğü**, donanım denge veri tabanı (PSU/bellek), CO/PBO tespiti, VRR kuralı, Gölge Mod | +3 hafta |
| **v1.0** | Kernel sürücü *(opsiyonel — sertifika hazırsa)*, cilalama | +4 hafta |

> **Değişen karar:** Sertifika süreci artık kritik yol üzerinde değil. Bölüm 6.2(a) gereği sürücü, ürünün değerinin küçük bir kısmını taşıyor. Yine de başvuru erken yapılmalı, ama v1.0 sertifikayı beklemek zorunda değil.

---

## 12. Riskler

| Risk | Etki | Azaltma |
|---|---|---|
| **Yanlış teşhis** | **Yüksek** | Güven skoru; asla "kesin" deme; corrected/fatal WHEA ayrımı; alternatif sebepleri listele |
| Bazı oyunlar ETW toplamayı engelliyor | Orta | PresentMon 2.5+ yöntemi: oturum başlatma ile sağlayıcı etkinleştirmeyi ayır |
| Kernel ETW oturumu çakışması | Orta | Xperf, HWiNFO, GPU-Z ile eşzamanlı çalışma testleri; zarif bozulma |
| Anti-cheat sürücüyü engeller | Düşük | Gölge Mod; sürücü artık kritik değil |
| HVCI sürücüyü reddeder | Düşük | Attestation signing; reddedilirse sessizce Gölge Mod |
| Defender yanlış pozitifi | Orta | EV sertifika; belgelenmemiş native API kullanma; SmartScreen itibarı zamanla |
| Donanım veri tabanı bakımı | Orta | Uzaktan güncellenen JSON; topluluk katkısı |
| PresentMon lisansı | Düşük | MIT — ticari kullanıma uygun |

En büyük risk teknik değil: **yanlış teşhis.** Vaka 6'daki kullanıcı çocuğu için sistem toplamış ve zaten iki kez parça değiştirmiş. Yanlış bir hüküm ona üçüncü bir gereksiz masraf yaptırır. Kural motoru muhafazakâr ayarlanmalı ve emin olmadığında **"belirgin bir sebep bulunamadı, şu üç şeyi sırayla deneyin"** demeyi bilmelidir.

---

## Kaynaklar

- [PresentMon — GameTechDev / GitHub](https://github.com/GameTechDev/PresentMon) · [Lisans (MIT)](https://github.com/GameTechDev/PresentMon/blob/main/LICENSE.txt) · [v2.5.0 notları](https://newreleases.io/project/github/GameTechDev/PresentMon/release/v2.5.0)
- [Kernel-Processor-Power PPM olay listesi — Geoff Chappell](https://www.geoffchappell.com/studies/windows/km/ntoskrnl/events/microsoft-windows-kernel-processor-power.htm)
- [Throttling detection needed in Windows — microsoft/Windows-Dev-Performance #101](https://github.com/microsoft/Windows-Dev-Performance/issues/101)
- [CurrentMhz artık çalışmıyor — Windows-Dev-Performance #100](https://github.com/microsoft/Windows-Dev-Performance/issues/100)
- [PROCESSOR_POWER_INFORMATION — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/power/processor-power-information-str)
- [IDXGIAdapter3::QueryVideoMemoryInfo — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo)
- [Crash Address Space (PAGEDU64 / _DMP_HEADER64) — Volatility](https://github.com/volatilityfoundation/volatility/wiki/Crash-Address-Space) · [kdmp-parser](https://pypi.org/project/kdmp-parser)
- [Bug Check 0x116 VIDEO_TDR_FAILURE — Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x116---video-tdr-failure)
- [WHEA-Logger olay ID'leri — TheWindowsClub](https://www.thewindowsclub.com/fix-whea-logger-fatal-hardware-and-event-id-errors)
- [CVE-2020-14979 — NVD](https://nvd.nist.gov/vuln/detail/cve-2020-14979)
- [Microsoft Defender — VulnerableDriver:WinNT/Winring0](https://support.microsoft.com/en-us/windows/security/threat-malware-protection/microsoft-defender-antivirus-alert-vulnerabledriver-winnt-winring0)
- [Riot Vanguard WinRing0 engeli — Open Hardware Monitor #1268](https://github.com/openhardwaremonitor/openhardwaremonitor/issues/1268)
- [STORAGE_PROTOCOL_NVME_DATA_TYPE — Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddstor/ne-ntddstor-_storage_protocol_nvme_data_type)
- [Attestation Sign Windows Drivers — Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-attestation)
- [MSAcpi_ThermalZoneTemperature — WMI referansı](https://wutils.com/wmi/root/wmi/msacpi_thermalzonetemperature/)
