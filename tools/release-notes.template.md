<!--
  Sürüm notu şablonu — tools/release.ps1 doldurur.

  NEDEN AYRI DOSYA: release.ps1 saf ASCII olmak zorunda (PowerShell 5.1 .ps1
  dosyalarını ANSI okuyor, tek bir Türkçe karakter ayrıştırmayı bozuyor ve
  hata sebebi göstermiyor). Metin betiğin içinde durduğu sürece sürüm notu da
  ASCII kalıyordu: "Dosya ozetleri", "Dogrulamak icin". Kullanıcının gördüğü
  ilk sayfa buydu.

  Şablon ayrı bir UTF-8 dosyada durunca betik ASCII kalıyor, metin düzgün
  Türkçe oluyor. Betik yalnızca yer tutucuları değiştiriyor:

    {{VERSION}}   0.2.1
    {{CHANGES}}   CHANGELOG.md içindeki o sürümün bölümü
    {{HASHES}}    "<sha256>  <dosya>" satırları
    {{VT_URL}}    VirusTotal tarama adresi

  Koşullu bölümler `<!--AD-->` … `<!--/AD-->` arasında. Betik birini tutup
  diğerini siliyor — böylece Türkçe metnin TAMAMI bu dosyada kalıyor,
  betikte tek bir Türkçe karakter bile olmuyor.

  Bu yorum bloğu çıktıya GİRMEZ; betik ilk `## ` satırından itibaren alır.
-->
## Syspect {{VERSION}}

<!--TASLAK-->
> **TASLAK — yayımlanmaya hazır değil.** VirusTotal taraması eklenmeden bu
> sürüm yayımlanmamalı.

<!--/TASLAK-->
{{CHANGES}}

### Kurulum

Zip'i açın ve `ss_ui.exe` dosyasını çalıştırın. Kurulum yapmaz; kayıt
defterine dil ve tema tercihiniz dışında bir şey yazmaz. Windows 10/11.

### Windows uyarısı

Program imzasız dağıtılıyor, Windows mavi bir uyarı gösterecek:
**Daha fazla bilgi** → **Yine de çalıştır**.

Kod imzalama sertifikası ücretli; bunu saklamak yerine söylemeyi tercih
ediyoruz. Güvenmiyorsanız aşağıdaki özetleri karşılaştırın, taramaya bakın ya
da kaynaktan kendiniz derleyin.

### Doğrulama

Paket bu depodaki etiketli commit'ten GitHub Actions tarafından derlendi;
derleme günlüğü herkese açık. Aşağıdaki özetler o derlemenin çıktısına aittir.

```
{{HASHES}}
```

```powershell
Get-FileHash syspect-{{VERSION}}.zip -Algorithm SHA256
```

### VirusTotal

<!--VT_VAR-->
Paketin taraması: {{VT_URL}}
<!--/VT_VAR-->
<!--VT_YOK-->
Tarama linki henüz eklenmedi.
<!--/VT_YOK-->
