cmake_minimum_required(VERSION 3.16)
project(StutterScope LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------------------
#  Statik CRT: kullaniciya "VC++ Redistributable yukle" dedirtmemek icin.
#  Bu, .NET bagimsizligi hedefinin de bir parcasidir.
# ----------------------------------------------------------------------------
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # /utf-8 SART: report_html.cpp icindeki Turkce metinler UTF-8. Bu bayrak
    # olmadan MSVC kaynagi sistem ANSI kod sayfasi sanip metni bozar.
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ----------------------------------------------------------------------------
#  ss_core — tasinabilir analiz motoru
#  Windows API'sine bagimli DEGILDIR; her platformda derlenir ve test edilir.
# ----------------------------------------------------------------------------
add_library(ss_core STATIC
    core.cpp          core.h
    frame_source.cpp  frame_source.h
    report_html.cpp   report_html.h
)
target_include_directories(ss_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------------------
#  ss_dumpreader — cekirdek dump okuyucu (tek dosya, bagimsiz calisir)
# ----------------------------------------------------------------------------
add_executable(ss_dumpreader dumpreader.cpp)
# CrashControl registry anahtarini okumak icin (RegOpenKeyEx / RegQueryValueEx)
if (WIN32)
    target_link_libraries(ss_dumpreader PRIVATE advapi32)
endif()

# ----------------------------------------------------------------------------
#  ss_cli — kullanicinin calistirdigi program
#  PresentMon CSV'sini okur, teshis raporu basar. Windows API'sine bagimsiz.
# ----------------------------------------------------------------------------
#  ss_etw — Windows'a ozgu ETW toplayicisi
#  ss_core'dan AYRI tutulur: tasinabilir beyin Windows API'sine bulasmamali
#  (bkz. CLAUDE.md, bozulmamasi gereken kural 5).
# ----------------------------------------------------------------------------
if (WIN32)
    add_library(ss_etw STATIC etw_frame_source.cpp etw_frame_source.h)
    target_link_libraries(ss_etw PUBLIC ss_core PRIVATE tdh advapi32)

    # ss_telem — sicaklik/kullanim/frekans ornekleyici (NVML + PDH)
    # nvml.dll BAGLANMAZ, calisma aninda dinamik yuklenir: NVIDIA surucusu
    # olmayan makinede program yine de acilir, GPU alanlari bos kalir.
    add_library(ss_telem STATIC telemetry.cpp telemetry.h
                                telemetry_signals.cpp telemetry_signals.h)
    target_link_libraries(ss_telem PUBLIC ss_core)
    # wbemuuid: ACPI termal bolgesini WMI'dan okumak icin (CLSID_WbemLocator)
    target_link_libraries(ss_telem PRIVATE pdh wbemuuid ole32 oleaut32)

    # ss_probe — guc plani + sorunlu aygit taramasi
    add_library(ss_probe STATIC system_probe.cpp system_probe.h)
    target_link_libraries(ss_probe PRIVATE powrprof setupapi cfgmgr32)

    # ss_update — surum denetimi. Kendi sunucumuz yok; GitHub Releases API'si.
    # Dosya INDIRMEZ, kendini guncellemez — bkz. update_check.h
    add_library(ss_update STATIC update_check.cpp update_check.h version.h)
    target_include_directories(ss_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ss_update PRIVATE winhttp advapi32)

    # ss_dpc — DPC suclusu tespiti. Cekirdek ETW oturumu + modul tablosu.
    # psapi: EnumDeviceDrivers / GetDeviceDriverBaseNameW
    add_library(ss_dpc STATIC dpc_source.cpp dpc_source.h)
    target_include_directories(ss_dpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ss_dpc PRIVATE psapi advapi32)

    # ss_i18n — dil katmani. Ceviri dosyalari lang/ klasorunden okunur;
    # yeni dil eklemek yeniden derleme GEREKTIRMEZ.
    add_library(ss_i18n STATIC i18n.cpp i18n.h)
    target_include_directories(ss_i18n PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ss_i18n PRIVATE advapi32)

    # ss_store — depolama envanteri ve SMART. Ek kutuphane gerektirmez;
    # IOCTL'ler kernel32 uzerinden gider.
    add_library(ss_store STATIC storage_probe.cpp storage_probe.h)
    target_include_directories(ss_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    # ss_evtlog — Windows olay gunlugu okuyucu (Event Viewer'in verisi).
    # wevtapi her Windows'ta vardir, dinamik yuklemeye gerek yok.
    # Yonetici hakki GEREKMEZ: System ve Application kanallari herkese acik.
    add_library(ss_evtlog STATIC event_log.cpp event_log.h)
    target_include_directories(ss_evtlog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ss_evtlog PUBLIC ss_core PRIVATE wevtapi)

    # ss_capio — .syscap kayit dosyasi. Ayri kutuphane oldu ki hem arayuz
    # hem de test hedefi kullanabilsin; dosya bicimi testsiz kalmamali.
    add_library(ss_capio STATIC capture_io.cpp capture_io.h)
    target_link_libraries(ss_capio PUBLIC ss_core ss_evtlog ss_telem)

    # ss_dumplib — dumpreader.cpp'nin main'siz derlemesi.
    # Ayni kaynak iki hedefte: bagimsiz exe (ss_dumpreader) ve arayuzun
    # kullandigi kutuphane. SS_DUMP_LIB main'i disarida birakir.
    add_library(ss_dumplib STATIC dumpreader.cpp dump_scan.h)
    target_compile_definitions(ss_dumplib PRIVATE SS_DUMP_LIB)
    target_include_directories(ss_dumplib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ss_dumplib PRIVATE advapi32)
endif()

add_executable(ss_cli ss_cli.cpp)
target_link_libraries(ss_cli PRIVATE ss_core)
# ShellExecuteA — raporu varsayilan tarayicida acmak icin
if (WIN32)
    # ss_probe: capture yolunda SMBIOS bellek yoklamasi icin
    target_link_libraries(ss_cli PRIVATE shell32 ss_etw ss_evtlog ss_probe
                                         ss_store ss_dpc ss_i18n)
endif()

# ----------------------------------------------------------------------------
#  test_core — vaka kulliyati regresyon paketi
# ----------------------------------------------------------------------------
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE ss_core)

# ----------------------------------------------------------------------------
#  ss_ui — masaustu uygulamasi (klasik Win32)
#  Harici bagimlilik YOK. Gorsel stiller icin comctl32 v6 manifesti sart:
#  bu satir olmadan sekmeler ve dugmeler Windows 95 gorunumunde cizilir.
# ----------------------------------------------------------------------------
if (WIN32)
    # ss_makeicon — logo.cpp'den .ico uretir (gelistirme araci)
    add_executable(ss_makeicon make_icon.cpp logo.cpp logo.h)
    target_link_libraries(ss_makeicon PRIVATE gdiplus ole32)

    add_executable(ss_ui WIN32 ss_ui.cpp
                   report_share.cpp report_share.h
                   findings.cpp     findings.h
                   logo.cpp         logo.h
                   ss_ui.rc)
    target_compile_definitions(ss_ui PRIVATE UNICODE _UNICODE)
    target_link_libraries(ss_ui PRIVATE
        ss_core ss_etw ss_telem ss_probe ss_evtlog ss_capio ss_dumplib ss_store
        ss_i18n ss_update ss_dpc gdiplus shell32 comdlg32 psapi)

    # Windows katmani testleri: olay gunlugu eslemesi + kayit dosyasi bicimi
    add_executable(test_winlayer test_winlayer.cpp)
    target_link_libraries(test_winlayer PRIVATE ss_evtlog ss_capio ss_telem
                                                ss_i18n ss_dpc psapi)
    # Dil testi Turkce kaynak metin literalleri iceriyor: UTF-8 kaynak +
    # dogru genis karakter uretimi icin /utf-8 sart.
    target_compile_options(test_winlayer PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endif()

enable_testing()
add_test(NAME core_scenarios COMMAND test_core)
add_test(NAME dump_selftest  COMMAND ss_dumpreader --selftest)
if (WIN32)
    add_test(NAME winlayer COMMAND test_winlayer)
endif()

# CLI'nin en azindan ayaga kalktigini ve kullanim metnini bastigini dogrular.
add_test(NAME cli_help COMMAND ss_cli --help)
