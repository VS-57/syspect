# ============================================================================
#  Syspect - public depoya surum yayinlama
# ----------------------------------------------------------------------------
#  NEDEN IKI DEPO:
#  Gelistirme birden fazla makinede yurutuluyor ve push senkron mekanizmasi
#  olarak kullaniliyor: yarim kalmis WIP commit'leri surekli itiliyor.
#  Yerelde toparlayip tek temiz commit atmak bu calisma bicimiyle mumkun degil.
#
#  Cozum: ozel depo gunluk calisma, public depo yalnizca SURUM anlik
#  goruntuleri.
#
#  PROVENANCE KORUNUYOR - kritik nokta:
#  Release public depoda uretiliyor. Yani "bu binary, GitHub tarafindan public
#  kaynaktan derlendi" iddiasi gecerli kaliyor. Release'i ozel depoda uretip
#  dosyayi public'e kopyalasaydik o zincir kirilirdi ve imzasiz dagitimda
#  elimizdeki tek guven argumani gitmis olurdu.
#
#  GECMIS: public depo dogrusal bir surum zinciri tutar (v0.1.0 -> v0.2.0).
#  Her surum tek commit; ara WIP gorunmez ama surum gecmisi kaybolmaz.
#
#  Kullanim:
#      tools\publish.ps1 v0.2.0
#      tools\publish.ps1 v0.2.0 -DryRun     (ne yapacagini goster, yapma)
#
#  DIKKAT - saf ASCII. PowerShell 5.1 .ps1 dosyalarini ANSI okuyor.
# ============================================================================
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($Tag -notmatch '^v\d+\.\d+\.\d+$') {
    throw "Etiket bicimi vX.Y.Z olmali (ornek: v0.2.0). Verilen: $Tag"
}

# ---- 1) Etiket ile version.h AYNI OLMALI ----------------------------------
#  Kontrol edilmezse v0.3.0 etiketiyle 0.2.0 binary'si yayinlanir ve surum
#  denetimi kullaniciya sonsuza dek "guncelleme var" der.
$m = Select-String -Path "version.h" -Pattern '#define SS_VERSION_STRING\s+"([^"]+)"'
$ver = $m.Matches[0].Groups[1].Value
if ($Tag -ne "v$ver") {
    throw "Etiket ($Tag) version.h ile ($ver) uyusmuyor. version.h guncellenmemis olabilir."
}

# ---- 2) Calisma agaci temiz olmali ----------------------------------------
#  Kirli agactan yayinlamak, public depoya commit EDILMEMIS bir seyi
#  gondermek demek. O dosya ozel depoda hic bulunmaz ve nereden geldigi
#  anlasilmaz.
$dirty = git status --porcelain
if ($dirty) {
    Write-Host $dirty
    throw "Calisma agaci kirli. Once commit edin."
}

# ---- 3) Testler ------------------------------------------------------------
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
Write-Host "Testler calisiyor..." -ForegroundColor Cyan
& $cmake --build build --config Release | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Derleme basarisiz." }
& $cmake --build build --config Release --target RUN_TESTS | Out-Null
if ($LASTEXITCODE -ne 0) { throw "TESTLER GECMEDI, yayinlanmadi." }

# ---- 4) Public depo klonu --------------------------------------------------
#  .publish/ .gitignore'da; ozel depoya girmez.
$pubUrl  = "https://github.com/VS-57/syspect.git"
$pubDir  = Join-Path $root ".publish"

if (-not (Test-Path (Join-Path $pubDir ".git"))) {
    Write-Host "Public depo klonlaniyor..." -ForegroundColor Cyan
    if (Test-Path $pubDir) { Remove-Item -Recurse -Force $pubDir }
    git clone --quiet $pubUrl $pubDir
} else {
    Push-Location $pubDir
    git fetch --quiet origin
    git reset --quiet --hard origin/HEAD 2>$null
    Pop-Location
}

# ---- 5) Takip edilen dosyalari kopyala ------------------------------------
#  `git ls-files` .gitignore'a uyar; build ciktisi, dist/ ve .publish/
#  kendiliginden disarida kalir.
Write-Host "Dosyalar aktariliyor..." -ForegroundColor Cyan

# Public taraftaki eski dosyalar temizlenir; yoksa silinen bir dosya orada
# sonsuza dek yasar ve iki depo sessizce ayrisir.
Get-ChildItem $pubDir -Force |
    Where-Object { $_.Name -ne ".git" } |
    ForEach-Object { Remove-Item -Recurse -Force $_.FullName }

$files = git ls-files
foreach ($f in $files) {
    $dest = Join-Path $pubDir $f
    $destDir = Split-Path -Parent $dest
    if ($destDir -and -not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Force $destDir | Out-Null
    }
    Copy-Item $f $dest -Force
}
Write-Host "  $($files.Count) dosya" -ForegroundColor Green

# ---- 6) Commit + etiket + push --------------------------------------------
Push-Location $pubDir
try {
    git add -A
    $changed = git status --porcelain
    if (-not $changed) {
        Write-Host "Public depo zaten guncel; yeni commit yok." -ForegroundColor Yellow
    }

    $srcSha = (git -C $root rev-parse --short HEAD)
    $msg = "release: Syspect $ver`n`nOzel depodaki $srcSha commit'inin anlik goruntusu.`nAra gelistirme gecmisi bu depoda tutulmuyor; her commit bir surumdur."

    if ($DryRun) {
        Write-Host "`n--- DRY RUN ---" -ForegroundColor Yellow
        Write-Host "commit: $msg"
        Write-Host "etiket: $Tag"
        Write-Host "hedef : $pubUrl"
        git status --short
        return
    }

    if ($changed) { git commit --quiet -m $msg }
    git tag -a $Tag -m "Syspect $ver"
    git push --quiet origin HEAD
    git push --quiet origin $Tag
    Write-Host "`nYayinlandi: $Tag" -ForegroundColor Green
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "SIRADAKI ADIMLAR:" -ForegroundColor Cyan
Write-Host "  1. Public depoda Release is akisi calisiyor (TASLAK olusturur)"
Write-Host "  2. Uretilen zip'i virustotal.com'a yukleyin"
Write-Host "  3. Tarama linkini surum notuna ekleyin"
Write-Host "  4. lib/syspect-release.ts icindeki SHA-256'lari guncelleyin"
Write-Host "  5. Taslagi yayinlayin"
