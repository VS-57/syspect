// ============================================================================
//  StutterScope — cokme dumplarini otomatik bulma
//  ----------------------------------------------------------------------
//  dumpreader.cpp'nin ayristiricisini kullanarak makinedeki mavi ekran
//  kayitlarini tarar. Ayri bir baslik olmasinin sebebi, ayristirici mantigin
//  tek bir yerde (dumpreader.cpp) kalmasi; burasi yalnizca "hangi dosyalar"
//  sorusuna cevap verir ve sonucu arayuzun kullanabilecegi sade bir yapiya
//  cevirir.
//
//  Taranan yerler:
//    %SystemRoot%\Minidump\*.dmp     — her mavi ekranda bir dosya
//    %SystemRoot%\MEMORY.DMP         — tam/cekirdek dump (tek dosya, ustune yazilir)
//
//  NOT: Bu dosyalar kullanici-mod MDMP DEGILDIR; cekirdek dump formatidir
//  (imza PAGEDU64). Ayrinti icin dumpreader.cpp basligina bakin.
// ============================================================================
#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <vector>

namespace ssdump {

struct Cause {
    int         percent = 0;
    std::string label;
    std::string action;
    std::string evidence;
};

struct DumpFinding {
    std::string path;
    std::string fileName;
    uint64_t    fileTimeUtc = 0;   // FILETIME, siralama icin

    bool        parsed = false;
    std::string error;

    uint32_t    bugcheckCode = 0;
    std::string bugcheckName;      // "IRQL_NOT_LESS_OR_EQUAL" gibi
    std::string bugcheckMeaning;   // kullaniciya donuk aciklama
    std::string suspectDriver;     // en olasi suclu .sys (varsa)

    std::vector<Cause> ranked;
    int         confidence = 0;
};

struct DumpScan {
    std::vector<DumpFinding> findings;   // en yeniden en eskiye
    bool        dumpsEnabled = false;    // CrashControl acik mi
    std::string note;                    // kullaniciya gosterilecek ozet

    // ------------------------------------------------------------------
    //  KRITIK AYRIM: "kayit yok" ile "okuyamadim" ayni sey DEGILDIR.
    // ------------------------------------------------------------------
    //  %SystemRoot%\Minidump klasoru yonetici olmayan sureclere KAPALIDIR
    //  (ERROR_ACCESS_DENIED). Ilk surumde bu durum sessizce bos sonuc
    //  uretiyordu ve arayuz "Mavi ekran kaydi bulunamadi. Bu iyi haber."
    //  diyordu — makinede mavi ekranlar dururken. Yanlis negatif, ve bu
    //  projenin kacinmasi gereken hata tipinin ta kendisi.
    //
    //  accessDenied true iken findings BOS OLABILIR ama bu bir olcum
    //  DEGILDIR; hukum verilemez.
    bool accessDenied  = false;   // klasor var, okuma izni yok
    bool folderMissing = false;   // klasor hic yok

    // Guvenilir bir cevap verebildik mi?
    bool conclusive() const { return !accessDenied; }
};

// Makinedeki dumplari tarar ve ayristirir. En fazla `limit` dosya okur
// (en yeniler oncelikli) — 200 dump biriken makinelerde arayuzu kilitlememek
// icin sinir var.
DumpScan scanSystemDumps(size_t limit = 20);

} // namespace ssdump

#endif // _WIN32
