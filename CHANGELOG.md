# Değişiklik günlüğü

Biçim [Keep a Changelog](https://keepachangelog.com/tr/1.1.0/), sürüm
numaraları [Semantic Versioning](https://semver.org/lang/tr/) izler.

**Bu dosya sürüm notunun kaynağıdır.** `tools/release.ps1` yayımlanan sürümün
bölümünü buradan alıp GitHub sürüm notuna gömer; bölüm yoksa paket üretmez.
Testlerdeki kuralın aynısı: "bu sefer değişiklik listesi yok" demek, hiç
olmamasından kötüdür — kullanıcı neyi indirdiğini bilmeden güncelliyor.

Kullanıcının gördüğü değişiklikler yazılır. İç düzenlemeler (yeniden
adlandırma, test ekleme, derleme ayarı) buraya girmez; onlar commit
geçmişinde durur.

## [Yayımlanmamış]

## [0.2.1] — 2026-08-02

### Kaldırıldı

- **Ayarlar → "Çeviri şablonu" düğmesi.** Ürettiği şablon kullanılabilir
  değildi: toplayıcı ekrana çıkan her metni kaydediyor, oysa metinlerin çoğu
  çalışma anında kuruluyor — işlemci adı, ölçüm sonuçları, saat, sayaçlar.
  Örnek bir kayıtta 328 satırın 141'i düğmenin kendi sayacıydı ("12 metin",
  "13 metin", …): sayacı ekrana yazmak sayacı artırıyor, her çizim yeni bir
  satır doğuruyordu.

  Dil eklemek hâlâ mümkün ve kod değişikliği gerektirmiyor: `lang/` klasörüne
  bir `.lang` dosyası bırakmak yeterli, program açılışta buluyor.
  `lang/en.lang` büyük ölçüde tam bir örnek.

### Değişti

- Arayüz metinleri cümle başında büyük harfle başlıyor: "bilinmiyor" →
  "Bilinmiyor", "ölçülemiyor" → "Ölçülemiyor", "belirlenemedi" →
  "Belirlenemedi", "yakında" → "Yakında", "tekerlekle kaydırın" → "Tekerlekle
  kaydırın". Aynı sütunda bazı satırların büyük bazılarının küçük başlaması
  tabloyu dağınık gösteriyordu.
- Ayarlar sayfası, çeviri kartı kalkınca kısaldı; sürüm ve güncelleme bölümü
  yukarı taşındı.

## [0.2.0] — 2026-08-01

İlk genel sürüm.

### Eklendi

- **Bellek kararsızlığı teşhisi** — EXPO/XMP profili, modül eşleşmesi ve
  bellek baskısı ölçümü.
- **Depolama envanteri** — bağlı diskler, arayüz hızı ve SMART durumu.
- **DPC suçlusu tespiti** — çekirdek ETW oturumu ve modül tablosuyla gecikmeye
  sebep olan sürücünün adı.
- **Dil katmanı** — `lang/` klasörüne konan `.lang` dosyalarıyla arayüz
  çevirisi; çevrilmemiş satırlar Türkçe görünür, dosya parça parça
  doldurulabilir.
- **Sürüm denetimi** — açılışta tek bir istek; kapatılabilir, kimlik
  taşımıyor.
- **Açık tema** ve tema tercihinin hatırlanması.
- **Komut satırı** (`ss_cli`) — arayüzsüz yakalama, çözümleme, olay günlüğü,
  depolama ve DPC dökümü.

### Notlar

- Program imzasız dağıtılıyor; Windows SmartScreen uyarısı çıkar.
- Paket GitHub Actions tarafından etiketli commit'ten derleniyor; derleme
  günlüğü herkese açık.

[Yayımlanmamış]: https://github.com/VS-57/syspect/compare/v0.2.1...HEAD
[0.2.1]: https://github.com/VS-57/syspect/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/VS-57/syspect/releases/tag/v0.2.0
