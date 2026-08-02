# ============================================================================
#  Syspect - surum paketleme
#  ----------------------------------------------------------------------
#  NEDEN BETIK: imzasiz bir binary dagitiyoruz. Guvenin tek dayanagi, her
#  surumde AYNI adimlarin AYNI sirayla islemesi. Ozet degeri, VirusTotal
#  taramasi ve surum notu elle yapilirsa er ya da gec biri unutulur; "bu
#  sefer ozet yok" demek, hic olmamasindan kotudur.
#
#  Surum numarasi version.h'den okunur, burada ikinci bir yerde tutulmaz.
#
#  DIKKAT - bu dosya SAF ASCII olmali. PowerShell 5.1 .ps1 dosyalarini
#  varsayilan olarak ANSI okuyor; uzun tire gibi bir karakter ayristirmayi
#  bozuyor ve hata mesaji sebebi hic gostermiyor. ss_cli.cpp ayni kurali
#  izliyor.
#
#  Surum notu metni bu dosyada DEGIL: tools\release-notes.template.md (UTF-8)
#  ve degisiklik listesi CHANGELOG.md. Ikisi de asagida, 5. adimda okunuyor.
#
#  Kullanim:
#      tools\release.ps1                     paketle (not TASLAK isaretlenir)
#      tools\release.ps1 -VirusTotal <url>   tarama linkiyle, yayima hazir not
#      tools\release.ps1 -Tag                paketle + git etiketi olustur
#      tools\release.ps1 -SkipBuild          derlemeyi atla (hizli deneme)
#
#  Etiket BILEREK varsayilan degil: paket dogrulanmadan etiket atilmamali.
# ============================================================================
param(
    [switch]$Tag,
    [switch]$SkipBuild,
    [string]$VirusTotal
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# CMake yolu. Gelistirme makinesinde PATH'te DEGIL, Visual Studio'nun icinde;
# CI kosucusunda ise PATH'te var ve VS yolu YOK. Once PATH'e bakip yoksa
# gelistirme makinesinin yoluna dusmek ikisini de calistirir.
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
if (-not (Test-Path $cmake)) { throw "cmake bulunamadi: $cmake" }

# ---- 1) Surum numarasini TEK kaynaktan oku --------------------------------
$verLine = Select-String -Path "version.h" -Pattern '#define SS_VERSION_STRING\s+"([^"]+)"'
if (-not $verLine) { throw "version.h icinde SS_VERSION_STRING bulunamadi." }
$version = $verLine.Matches[0].Groups[1].Value
$tagName = "v$version"
Write-Host "Surum: $version" -ForegroundColor Cyan

# ---- 2) Derle ve TEST ET ---------------------------------------------------
#  Testler gecmeden paket uretilmez. Bu bir kolaylik degil, kilit: vaka
#  kulliyatinin kirildigi bir surum, yanlis teshis dagitmak demektir.
if (-not $SkipBuild) {
    # CI'da build/ klasoru bos gelir; yoksa yapilandir.
    if (-not (Test-Path "build\CMakeCache.txt")) {
        Write-Host "Yapilandiriliyor..." -ForegroundColor Cyan
        & $cmake -B build -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) { throw "CMake yapilandirmasi basarisiz." }
    }

    Write-Host "Derleniyor..." -ForegroundColor Cyan
    & $cmake --build build --config Release | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Derleme basarisiz." }

    Write-Host "Testler calisiyor..." -ForegroundColor Cyan
    & $cmake --build build --config Release --target RUN_TESTS | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "TESTLER GECMEDI, paket uretilmedi." }
}

# ---- 3) Paketi kur ---------------------------------------------------------
$out = Join-Path $root "dist"
$stage = Join-Path $out "syspect-$version"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

$files = @(
    "build\Release\ss_ui.exe",
    "build\Release\ss_cli.exe",
    "LICENSES.txt"
)
foreach ($f in $files) {
    if (-not (Test-Path $f)) { throw "Eksik dosya: $f" }
    Copy-Item $f $stage
}

# Dil klasoru bos da olsa paketle gitsin ki kullanici nereye koyacagini
# bilsin. Icine kisa bir yonerge birakiliyor.
$langDir = Join-Path $stage "lang"
New-Item -ItemType Directory -Force $langDir | Out-Null

# Depodaki hazir ceviri dosyalari pakete girer.
Get-ChildItem (Join-Path $root "lang") -Filter *.lang -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item $_.FullName $langDir }
$readme = @"
Bu klasore .lang uzantili ceviri dosyalari konur.

Sablon uretmek icin: Syspect'i acin, butun sayfalarda bir kez gezin,
Ayarlar > Ceviri sablonu > Sablonu Kaydet.

Cevirdikten sonra dosyayi buraya koyun; program acilista bulur.
Bos birakilan satirlar cevrilmemis sayilir ve Turkce gorunur, yani
dosyayi parca parca doldurabilirsiniz.
"@
$readme | Out-File -FilePath (Join-Path $langDir "BENIOKU.txt") -Encoding utf8

$zip = Join-Path $out "syspect-$version.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "Paket: $zip" -ForegroundColor Green

# ---- 4) SHA-256 ------------------------------------------------------------
#  Hem zip hem tek tek exe'ler: kullanici zip'i degil dogrudan exe'yi
#  dogrulamak isteyebilir.
$hashes = [ordered]@{}
$hashes["syspect-$version.zip"] = (Get-FileHash $zip -Algorithm SHA256).Hash
foreach ($n in @("ss_ui.exe", "ss_cli.exe")) {
    $hashes[$n] = (Get-FileHash (Join-Path $stage $n) -Algorithm SHA256).Hash
}

# ---- 5) Surum notu ---------------------------------------------------------
#  Metin BU DOSYADA DEGIL, tools/release-notes.template.md icinde. Sebebi
#  dosyanin basindaki ASCII kurali: notu burada kursaydik not da ASCII
#  kalirdi ("Dosya ozetleri", "Dogrulamak icin") ve kullanicinin gordugu ilk
#  sayfa bozuk Turkce olurdu. Sablon ayri bir UTF-8 dosyada; betik yalnizca
#  yer tutuculari degistiriyor.
#
#  Degisiklik listesi CHANGELOG.md'den geliyor. Elle yazilan bir liste er ya
#  da gec unutulur; surumun ne getirdigini soylemeyen bir not, kullanicinin
#  neyi indirdigini bilmeden guncellemesi demek.
$changelogPath = Join-Path $root "CHANGELOG.md"
if (-not (Test-Path $changelogPath)) { throw "CHANGELOG.md bulunamadi." }
$utf8 = New-Object System.Text.UTF8Encoding $false
$clog = [System.IO.File]::ReadAllText($changelogPath, $utf8)

# "## [0.2.1]" basligindan bir sonraki "## [" basligina (ya da dosya sonuna)
# kadar olan blok.
$secPattern = '(?ms)^##\s*\[' + [regex]::Escape($version) + '\][^\r\n]*\r?\n(.*?)(?=^##\s*\[|\z)'
$secMatch = [regex]::Match($clog, $secPattern)
if (-not $secMatch.Success) {
    throw "CHANGELOG.md icinde [$version] bolumu yok. Once degisiklikleri yazin; paket uretilmedi."
}
$changes = $secMatch.Groups[1].Value.Trim()
if (-not $changes) {
    throw "CHANGELOG.md icindeki [$version] bolumu bos. Paket uretilmedi."
}

$tplPath = Join-Path $PSScriptRoot "release-notes.template.md"
if (-not (Test-Path $tplPath)) { throw "Sablon bulunamadi: $tplPath" }
$tpl = [System.IO.File]::ReadAllText($tplPath, $utf8)
# Sablonun basindaki aciklama bloguna cikti icinde yer yok: gercek not, SATIR
# BASINDA "## " ile baslayan ilk satir. Duz IndexOf("## ") yetmiyor -- aciklama
# blogunun icinde de "## " gecebiliyor ve kesme oraya dusuyor.
$tplStart = [regex]::Match($tpl, '(?m)^##\s')
if (-not $tplStart.Success) { throw "Sablonda satir basinda '## ' yok." }
$tpl = $tpl.Substring($tplStart.Index)

# Kosullu bolumler: <!--AD--> ... <!--/AD-->. Tutulacaksa isaretciler silinip
# icerik birakilir, tutulmayacaksa blogun tamami gider. Metin sablonda kaldigi
# icin bu dosya ASCII kalabiliyor.
function Select-NoteBlock {
    param([string]$Text, [string]$Name, [bool]$Keep)
    $pattern = '(?s)[ \t]*<!--' + [regex]::Escape($Name) + '-->\r?\n(.*?)<!--/' +
               [regex]::Escape($Name) + '-->[ \t]*\r?\n'
    if ($Keep) {
        return [regex]::Replace($Text, $pattern, { param($m) $m.Groups[1].Value })
    }
    return [regex]::Replace($Text, $pattern, "")
}

# VirusTotal linki verilmediyse not TASLAK sayilir. Eski surum bunu HTML
# yorumuyla isaretliyordu; yorum yayimlanan notta GORUNMUYOR, yani unutuldugu
# an kimse fark etmiyordu -- v0.2.0 tam olarak boyle cikti. Artik uyari
# okunabilir bir alinti satiri.
$hasVt = [bool]$VirusTotal
if ($hasVt -and $VirusTotal -notmatch '^https://(www\.)?virustotal\.com/') {
    throw "VirusTotal linki virustotal.com adresinde olmali: $VirusTotal"
}
if (-not $hasVt) {
    Write-Host "UYARI: VirusTotal linki verilmedi, not TASLAK olarak isaretlendi." -ForegroundColor Yellow
}

$tpl = Select-NoteBlock -Text $tpl -Name "TASLAK" -Keep (-not $hasVt)
$tpl = Select-NoteBlock -Text $tpl -Name "VT_VAR" -Keep $hasVt
$tpl = Select-NoteBlock -Text $tpl -Name "VT_YOK" -Keep (-not $hasVt)

$hashBlock = (($hashes.Keys | ForEach-Object { "$($hashes[$_])  $_" }) -join "`r`n")

$body = $tpl.Replace("{{VERSION}}", $version).
             Replace("{{CHANGES}}", $changes).
             Replace("{{HASHES}}",  $hashBlock).
             Replace("{{VT_URL}}",  "<$VirusTotal>")
$body = $body -replace "`r?`n", "`r`n"

# Iki ad altinda yaziliyor: "0.2.0" (insan icin) ve "v0.2.0" (release is
# akisi ref_name ile ariyor). Tek ada baglamak, ikisinden birinin sessizce
# bulunamamasi demekti.
$notes  = Join-Path $out "release-notes-$version.md"
$notesV = Join-Path $out "release-notes-$tagName.md"
# BOM'suz UTF-8: Out-File -Encoding utf8 PowerShell 5.1'de BOM yaziyor ve
# GitHub surum notunun ilk satirinda gorunmez bir karakter birakiyor.
$noBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($notes,  $body, $noBom)
[System.IO.File]::WriteAllText($notesV, $body, $noBom)

Write-Host ""
Write-Host "SHA-256:" -ForegroundColor Cyan
foreach ($k in $hashes.Keys) { Write-Host ("  {0}  {1}" -f $hashes[$k], $k) }
Write-Host ""
Write-Host "Surum notu: $notes" -ForegroundColor Green

# ---- 6) Etiket -------------------------------------------------------------
if ($Tag) {
    $existing = git tag --list $tagName
    if ($existing) {
        Write-Host "Etiket zaten var: $tagName" -ForegroundColor Yellow
    } else {
        git tag -a $tagName -m "Syspect $version"
        Write-Host "Etiket olusturuldu: $tagName (henuz push edilmedi)" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "SIRADAKI ADIMLAR:" -ForegroundColor Cyan
Write-Host "  1. $zip dosyasini virustotal.com'a yukleyin"
Write-Host "  2. Notu tarama linkiyle yeniden uretin:"
Write-Host "       tools\release.ps1 -SkipBuild -VirusTotal <link>"
Write-Host "     (ya da taslaktaki notu GitHub'da duzenleyip TASLAK satirini silin)"
Write-Host "  3. GitHub Releases'e $tagName olarak yukleyin"
Write-Host "  4. EnUcuzSistem sayfasindaki ozetleri guncelleyin"
