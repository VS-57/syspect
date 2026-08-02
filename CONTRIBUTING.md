# Katkı

Önce en önemli şey:

> **Bu proje şu anda kod katkısı KABUL ETMİYOR.**
> Hata bildirimi, öneri ve gerçek vaka paylaşımı ise çok değerli.

---

## Neden kod katkısı kapalı

Syspect'in telif hakkı tek elde. Bu, aynı kodun ileride farklı bir lisansla
(örneğin kapalı bir ticari sürüm olarak) yeniden yayınlanabilmesini mümkün
kılıyor — AGPL başkalarını bağlar, telif sahibini bağlamaz.

Ama bu hak **bütün telif tek elde kaldığı sürece** geçerli. Telif devri
imzalanmamış tek bir katkı kabul edilirse o satırların lisansını değiştirmek
için katkıcının izni gerekir. Katkıcı bulunamazsa, fikrini değiştirirse ya da
hayatta olmazsa yeniden lisanslama **imkânsız** hale gelir.

Bu geriye dönük düzeltilemeyecek bir karardır. Bu yüzden düzgün bir telif
devri (CLA) süreci kurulana kadar pull request birleştirilmiyor.

Kapalı olması "katkı istemiyoruz" demek değil — **hazırlıksız kabul etmek
istemiyoruz** demek.

---

## Bunun yerine neye ihtiyaç var

### Gerçek vakalar

Bu projenin omurgası `test_core.cpp` içindeki vaka külliyatı ve o külliyat
gerçek forum şikâyetlerinden türetildi. Motorun kalitesi doğrudan gerçek vaka
sayısına bağlı.

Bir issue açın ve şunları ekleyin:

- Ne yaşadığınız (takılma, donma, mavi ekran)
- Neyi zaten denediğiniz ve elediğiniz
- Syspect'in ne dediği
- **`.syscap` kaydı** — Sonuçlar sayfası → *Kaydı Dışa Aktar*

Kayıt düz metindir; Not Defteri'nde açıp ne paylaştığınızı görebilirsiniz.
İçinde ölçüm ve ölçümün alındığı programın adı dışında kişisel veri yoktur.

### Yanlış teşhis

En değerli geri bildirim bu. Motor size yanlış bir sebep söylediyse ve gerçek
sebebi biliyorsanız, bu bir kural hatasıdır ve düzeltilmesi gerekir.

Yanlış teşhis bu projenin bir numaralı riski: kullanıcı çalışan bir sürücüyü
kaldırabilir ya da gereksiz donanım alabilir.

### Çeviri

Kod değişikliği gerektirmez, bu yüzden telif sorunu da doğurmaz.

Ayarlar → *Çeviri şablonu* → *Şablonu Kaydet*, çevirin, issue'ya ekleyin.
Boş bırakılan satırlar çevrilmemiş sayılır ve Türkçe görünür; dosyayı parça
parça doldurabilirsiniz.

---

## Kendiniz derlemek

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

AGPL size kodu değiştirme ve kendi sürümünüzü dağıtma hakkı **veriyor** —
kaynağını aynı lisansla açtığınız sürece. Fork yapmak serbest.

---

## Ticari lisans

AGPL koşulları size uymuyorsa (kapalı bir üründe kullanmak istiyorsanız)
ticari lisans için iletişime geçin. Ayrıntı: [NOTICE.md](./NOTICE.md)
