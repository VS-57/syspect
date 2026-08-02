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
#  Kullanim:
#      tools\release.ps1              paketle
#      tools\release.ps1 -Tag         paketle + git etiketi olustur
#      tools\release.ps1 -SkipBuild   derlemeyi atla (hizli deneme)
#
#  Etiket BILEREK varsayilan degil: paket dogrulanmadan etiket atilmamali.
# ============================================================================
param(
    [switch]$Tag,
    [switch]$SkipBuild
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
# Iki ad altinda yaziliyor: "0.2.0" (insan icin) ve "v0.2.0" (release is
# akisi ref_name ile ariyor). Tek ada baglamak, ikisinden birinin sessizce
# bulunamamasi demekti.
$notes  = Join-Path $out "release-notes-$version.md"
$notesV = Join-Path $out "release-notes-$tagName.md"
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("## Syspect $version")
$lines.Add("")
$lines.Add("### Dosya ozetleri (SHA-256)")
$lines.Add("")
$lines.Add('```')
foreach ($k in $hashes.Keys) { $lines.Add("$($hashes[$k])  $k") }
$lines.Add('```')
$lines.Add("")
$lines.Add("Dogrulamak icin PowerShell'de:")
$lines.Add("")
$lines.Add('```powershell')
$lines.Add("Get-FileHash syspect-$version.zip -Algorithm SHA256")
$lines.Add('```')
$lines.Add("")
$lines.Add("### Windows uyarisi")
$lines.Add("")
$lines.Add("Program imzasiz dagitiliyor; Windows mavi bir uyari gosterecek.")
$lines.Add("**Daha fazla bilgi** > **Yine de calistir** ile gecebilirsiniz.")
$lines.Add("Kaynak kod acik; dilerseniz kendiniz derleyin.")
$lines.Add("")
$lines.Add("### VirusTotal")
$lines.Add("")
$lines.Add("<!-- Paketi virustotal.com'a yukleyip tarama linkini BURAYA yapistirin. -->")
$lines.Add("<!-- Bu adim atlanirsa surum YAYINLANMAMALI. -->")
$body = $lines -join "`r`n"
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
Write-Host "  2. Tarama linkini surum notuna yapistirin"
Write-Host "  3. GitHub Releases'e $tagName olarak yukleyin"
Write-Host "  4. EnUcuzSistem sayfasindaki ozetleri guncelleyin"
