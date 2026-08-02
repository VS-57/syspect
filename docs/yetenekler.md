# Syspect — Neye Bakıyor, Hangi Sorunları Buluyor

> Bu belge uygulamanın bugün gerçekten yapabildiklerini anlatır. Vaat değil,
> envanterdir. Bulamadıkları da aynı belgede, aynı açıklıkla yazılıdır.

---

## Ne işe yarar

Oyun oynarken takılıyorsanız, donuyorsanız veya mavi ekran alıyorsanız Syspect
sisteminizi bir süre izler ve **sebebin ne olduğunu söyler.**

Grafik gösterip yorumu size bırakmaz. Her sonuç üç parçadır:

> **Sebep** · **%kaç ihtimalle** · **ne yapmanız gerektiği (tek cümle)**

Emin değilse hüküm vermez, "bilmiyorum" der. Yanlış teşhis, çalışan bir
parçayı söktürdüğü ya da gereksiz parça aldırdığı için hiç teşhis vermemekten
daha kötüdür.

---

## Nasıl kullanılır

1. Uygulamayı açın, **Ölçüm** sayfasında süreyi seçin ve başlatın
2. Oyununuzu oynayın — takılmanın olduğu andan geçin
3. Ölçüm bitince **Sonuçlar** sayfasında sıralı liste sizi bekliyor

Ölçüm sırasında oyununuza **dokunulmaz**. Oyunun içine hiçbir şey enjekte
edilmez, hiçbir dosyası okunmaz. Sadece Windows'un kendi yayınladığı ölçüm
bilgileri dinlenir — bu yüzden anti-cheat sistemleriyle hiçbir sorun çıkmaz.

Hiçbir ayarınız değiştirilmez. Ne yapmanız gerektiği söylenir, sizin yerinize
yapılmaz.

En az **30 saniyelik** bir kayıt gerekir. Daha kısa kayıtlarda sayılar
gösterilir ama hüküm verilmez.

---

## Neye bakıyor

### Oyunun kendisi
Her karenin ne kadar sürdüğü. Takılmaların ne zaman, ne sıklıkta ve ne
şiddette olduğu. Üç tür ayırt edilir:

- **Mikro takılma** — kısa, tekrar eden pürüz
- **Tökezleme** — yarım saniyeye kadar tek sıçrama
- **Donma** — yarım saniyeden uzun, sistem sonra kendine geliyor

FPS'iniz de ölçülür: ortalama, medyan ve "en kötü %1". Bu üçünün birbirinden
farkı tek başına bilgi taşır.

### Ekran kartı
Sıcaklık, kullanım oranı, frekans, çektiği güç, VRAM kullanımı. Ayrıca kartın
**kendi bildirdiği** kısıtlamalar: ısındığı için mi yavaşlıyor, güç limitine mi
takıldı, yoksa güç kaynağından fren sinyali mi geliyor.

Kartın güç limiti ve Resizable BAR durumu da okunur.

> Şu an yalnızca **NVIDIA** kartlarda çalışır. AMD kartta bu alanlar boş kalır
> ve "okunamadı" yazar — uydurma değer üretilmez.

### İşlemci
Kullanım oranı, gerçek çalışma performansı (temel frekansın altında mı üstünde
mi), çekirdeklerin park edip etmediği.

### Bellek
Takılı modüllerin kapasitesi, tipi, üreticisi, hangi slotta olduğu, desteklediği
hız ve **gerçekten çalıştığı hız.** EXPO/XMP profilinin açık olup olmadığı
buradan çıkarılır.

Ölçüm boyunca sistemin ne kadar bellek istediği de izlenir — RAM'inizden
fazlasını istediği an yakalanır.

### Disk
İki ayrı soru sorulur.

**Ne yapıyor:** ne kadar meşgul olduğu, işlemlerin ne kadar sürdüğü, saniyede ne
kadar veri aktardığı.

**Ne olduğu:** her diskin modeli, bağlantı tipi (NVMe / SATA), **dönen disk mi
katı hal mi**, kapasitesi, hangi bölümlerin o diskte olduğu, Windows'un hangi
diskte kurulu olduğu ve her bölümde ne kadar boş alan kaldığı.

Sağlık tarafında: diskin kendi arıza tahmini (üretici "bu disk arızalanmak
üzere" diyor mu) ve NVMe disklerde ömrün ne kadarının kullanıldığı, kalan yedek
blok oranı, sıcaklık, toplam yazılan veri, kaç saat çalıştığı.

> **Aşınma yüzdesi sebep olarak sayılmaz.** "SSD sağlığı %56, takılmanın sebebi
> bu" çıkarımı yanlıştır — aşınma bir ömür sayacıdır, hız göstergesi değil.
> %56 aşınmış bir disk ilk günkü hızında çalışıyor olabilir. Bu sayı size bilgi
> olarak verilir, teşhise girmez.
>
> Doğrudan bulgu sayılan üç şey vardır: **dönen disk üzerinde Windows**,
> **neredeyse dolu bölüm**, **üreticinin arıza uyarısı**.

Sağlık verisi için programın yönetici olarak çalışması gerekir. Çalışmıyorsa
disk modelleri yine okunur, sağlık satırı "okunamadı" der — "sorunsuz" demez.

### Sistemin geçmişi
Son 30 günde Windows'un kaydettiği şikâyetler: donanım hataları, kontrolsüz
kapanmalar, mavi ekranlar, ekran sürücüsünün kaç kez çöküp toparlandığı, diskin
kaç kez sıfırlandığı, dosya sistemi bozulmaları.

Her biri üç ayrı pencerede sayılır: **son 24 saat / 7 gün / 30 gün.** "30 günde
400 tane" ile "son 24 saatte 400 tane" tamamen farklı iki durumdur.

### Windows ayarları
Güç planı, pil durumu, sürücüsü eksik veya sorunlu aygıtlar, Secure Boot,
**Bellek Bütünlüğü (izole çekirdek)**, donanım hızlandırmalı GPU zamanlama,
Oyun Modu.

### Mavi ekran kayıtları
Bilgisayarınızda duran mavi ekran kayıtları otomatik bulunur ve okunur: hangi
hata kodu, hangi sürücüler o anda bellekteydi. Mavi ekran kaydının kapalı olup
olmadığı da kontrol edilir — çoğu makinede kapalıdır ve ilk mavi ekran kaçar.

---

## Hangi sorunları buluyor

| Şikâyetiniz | Bakılan ayırt edici | Verilen hüküm |
|---|---|---|
| Oyunda ani takılmalar, ses de cızırdıyor | Tek bir sürücü takılmaların büyük kısmıyla çakışıyor mu | Sürücü sorunu |
| Sistem donuyor, mavi ekran vermeden kapanıyor | Donanım hatası kayıtları + bellek yapılandırması | Bellek / EXPO-XMP kararsızlığı |
| Farklı marka veya hızda RAM taktım | Modüller birbirinin aynısı mı | **Karışık bellek takımı** |
| Oyun dışında da takılıyor (masaüstü, tarayıcı) | Sorun oyunda mı sistemde mi | Undervolt / sistem geneli kararsızlık |
| Ekran bir an donup kendine geliyor | Ekran sürücüsü ölçüm sırasında sıfırlandı mı | Ekran kartı sürücüsü |
| Oyunun ilk 10 dakikası takılıyor, sonra düzeliyor | Takılmalar zamanla kayboluyor mu | **Normal** — gölgelendirici derlemesi |
| Monitör değiştirdim, o zamandan beri pürüzlü | Mikro takılmalar düzenli aralıklı mı | Ekran senkronizasyonu / kare zamanlaması |
| Oyun sırasında bilgisayar aniden kapanıyor | Ekran kartına fren sinyali geldi mi | Güç kaynağı yetersizliği |
| FPS düşük ama takılma yok | Ekran kartı gerçekten dolu mu | Donanım yetersiz **ya da** başka bir tavan |
| Ekran kartım %70'te kalıyor, işlemci %99 | İkisinin oranı | **Darboğaz işlemcide** |
| Ne kartım ne işlemcim doluyor, FPS düşük | İkisi de boştaysa | Bir yerde tavan var (FPS sınırı, dikey eşitleme, güç limiti) |
| Oyun takılıyor, RAM'im 16 GB | Sistem RAM'den fazlasını istedi mi | Bellek yetersiz, diske sayfalama |
| Yükleme ekranlarında donma, dosya açılışları yavaş | Disk sürekli dolu mu, gecikme yüksek mi | Depolama |
| Windows disk hatası yazıyor | Diskin kendi bildirdiği sıfırlama/hata kayıtları | Depolama arızası |
| Yeni bilgisayar ama her şey yavaş | Windows dönen diskte mi | **HDD üzerinde Windows** — bu sistemdeki en büyük tek iyileştirme |
| Diskim doldu, sorun olur mu | Bölüm doluluk oranı | Boş alan %5'in altındaysa evet |
| SSD'm ölüyor mu | Üreticinin kendi arıza tahmini | SMART eşiği aşıldıysa: yedek alın |
| Ekran kartım kaç derece | Ölçüm boyunca en yüksek sıcaklık | 85 °C üstü uyarı, altı bilgi |
| Oyunda kasma, arka planda bir şey çalışıyor | Hangi program takılmalarla çakışıyor | Arka plan programı |
| Yeni sürücü kurdum, o zamandan beri sorun var | Değişiklik notu | Yakın zamandaki değişiklik |
| Bilgisayar ısınıyor sanıyorum | Ekran kartı ısındığını **kendi mi** bildirdi | Termal kısıtlama |

### Darboğaz var mı yok mu — evet, söylüyor

Bu ayrı bir soru olarak ele alınır. Ekran kartınızın ve işlemcinizin ölçüm
boyunca ne kadar dolduğuna bakılır ve dört ayrı sonuçtan biri verilir:

- **Ekran kartı dolu, FPS düşük, takılma yok** → sistem dengeli, ayarlar ağır
- **Ekran kartı boşta, işlemci dolu** → darboğaz işlemcide
- **İkisi de boşta** → bir yerde tavan var; sınırlayıcı ne olduğu ayrıca aranır
- **İkisi de boşta ama bellek dolu** → asıl darboğaz RAM'de

Üçüncü ve dördüncü şık önemli: "ekran kartım %70'te kalıyor, demek ki kart
yetersiz" en sık yapılan yanlış okumadır. Kart %70'teyse **bekliyordur** —
yetersiz değil, beslenemiyordur.

### Özellikle iyi olduğu iki konu

**Bellek kararsızlığı.** İncelenen gerçek şikâyetlerin yarısında ana şüpheli
buydu. Modüllerin hızı, sayısı, birbirine uygunluğu, EXPO/XMP durumu ve
sistemin donanım hatası geçmişi birlikte değerlendirilir.

Karışık modül takımı ayrıca ele alınır çünkü kullanıcıların **elediğini sandığı**
en yaygın şeydir: modülleri tek tek test edersiniz, ikisi de geçer — ama sorun
modüllerde değil, birlikte çalışmalarındadır.

**"Oyun dışında da oluyor mu?"** Bu tek soru, sorunun oyunda mı yoksa sistemde
mi olduğunu tek başına söyler. Uygulama bunu size sormaz; ölçümün hangi
programda alındığından kendisi çıkarır.

---

## Ne zaman susuyor

Yanlış teşhis bu uygulamanın en büyük riski. Dört ayrı fren var:

**Kayıt kısaysa hüküm yok.** 30 saniyenin altındaki kayıtta sayılar gösterilir
ama sebep sıralaması yapılmaz. Kısa kayıtta liste üretip altına "ama güvenim
düşük" yazmak dürüst değildir — herkes listeyi okur, uyarıyı okumaz.

**Hiçbir sebep %80'i geçemez.** Kalan pay açıkça "ölçemediğimiz alan" olarak
listelenir. "Kesin sebep bu" cümlesi hiçbir koşulda kurulmaz.

**Geçmişteki olay ile şu anki olay ayrılır.** İki ay önce bir kez ekran
sürücüsü sıfırlanmış sağlıklı bir makine, bugünkü takılması için suçlanmaz.
Ölçüm sırasında olan şey **sebeptir**; sadece geçmişte duran şey **bağlamdır.**

**Gürültü sinyal sayılmaz.** Bazı hata kayıtları sağlıklı makinelerde de
binlerce kez üretilir. Bu kayıtlar tek başlarına hiçbir zaman hüküm doğurmaz;
yanlarında başka bir kanıt olması ya da sayının normalin çok üstüne çıkmış
olması gerekir.

Sıcaklık konusundaki kural şu: **ölçebildiğimiz sıcaklık teşhise girer,
ölçemediğimiz girmez.** Ekran kartının sıcaklığını okuyabiliyoruz — 85 °C
üstünde uyarı verilir, kart kısıtlamaya girdiğini bildirdiyse doğrudan bulgu
olur. İşlemcinin çekirdek sıcaklığını okuyamıyoruz, o yüzden ona dayalı bir
hüküm de kurulmaz. Uydurma değer üretmektense "okunamadı" demek doğrudur.

---

## Bulamadıkları

Bunlar eksik listesi değil, yanlış beklenti kurmamak için yazıldı.

### Ölçülemiyor

| Ne | Neden |
|---|---|
| **İşlemci çekirdek sıcaklığı** | Windows bu değeri hiçbir arayüzle yayınlamıyor; sistem sürücüsü gerekiyor. Anakartın ACPI sensörü varsa o gösterilir ama çekirdek sıcaklığı değildir ve bu ayrım yazılır |
| **Neden kısıtlandığı** (ısı mı, güç mü, akım mı) | Bu karar donanımın içinde veriliyor ve Windows'a hiç bildirilmiyor |
| **EXPO/XMP profilinin kendisi** | Doğrudan okunamıyor; çalışan hız ile desteklenen hızın farkından çıkarılıyor |
| **Güç kaynağının anlık düşüşleri** | Milisaniyenin binde biri mertebesinde; hiçbir yazılım bunu yakalayamaz. Ekran kartının fren sinyali elde edilebilecek en yakın kanıt |

### Henüz eklenmedi

- **Takılma anında hangi sürücünün işlemciyi kilitlediği.** Şu an "bir sürücü
  sorunu var" denebiliyor ama **hangi sürücü** olduğu söylenemiyor. Sıradaki en
  öncelikli iş bu
- **AMD ekran kartı telemetrisi.** AMD kartta sıcaklık, kullanım, güç ve
  kısıtlama bilgileri okunamıyor
- **SATA SSD'lerin ayrıntılı sağlık verisi.** Arıza tahmini her diskte okunuyor
  ama aşınma yüzdesi şu an yalnızca NVMe disklerde
- **Dizüstü bilgisayarların güç limiti** — "140 W'lık kart neden 40 W çekiyor"
  sorusu henüz cevaplanamıyor
- Arka planda sürekli çalışıp siz oynarken kendi kendine kayıt tutan bir servis

### Bilerek yapılmıyor

- Oyununuza hiçbir şekilde müdahale edilmez — anti-cheat sınırı hiç zorlanmaz
- Hiçbir ayarınız değiştirilmez
- Sıcaklık hiçbir teşhisin dayanağı değildir

---

## Sonucu paylaşmak

Sonuçlar üç biçimde dışarı alınabilir:

- **Rapor (.md)** — foruma veya bir yapay zekâya doğrudan yapıştırılacak metin
- **Görsel rapor (HTML)** — tarayıcıda açılan, kendi kendine yeten tek dosya
- **Ham kayıt (.syscap)** — ölçümün kendisi. Foruma bırakırsınız, karşı taraf
  aynı uygulamada açıp kendi gözüyle bakar. Ekran görüntüsü tartışması biter

Size gönderilen bir kaydı açmak için Ölçüm sayfasındaki **Kayıt Aç** düğmesini
kullanın; dosyayı pencereye sürükleyip bırakmak da olur. Başkasının kaydını
açtığınızda kendi makinenizin bilgileri (disk, bellek, olay geçmişi) listeden
çıkarılır — o satırlar kaydın sahibine aitmiş gibi okunmasın diye.

Ham kayıt düz metindir; Not Defteri'nde açıp ne paylaştığınızı görebilirsiniz.
İçinde ölçüm bilgisi ve ölçümün alındığı programın adı dışında kişisel veri
yoktur.
