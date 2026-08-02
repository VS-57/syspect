# ============================================================================
#  Syspect - ekran goruntusu yakalama (gelistirme araci)
#  ----------------------------------------------------------------------
#  YUKSELTILMIS calistirilmali. Iki sebep:
#    1) Uygulama yonetici degilse ustte "olcum alinamaz" uyari seridi cikiyor
#       ve tanitim goruntusunde o serit istenmiyor.
#    2) Windows UIPI, yukseltilmis bir pencereye yukseltilmemis surecten
#       sentetik tiklama gondermeyi ENGELLIYOR. Sekme degistiremezdik.
#
#  Kullanim (yukseltilmis PowerShell'de):
#      tools\capture-screenshots.ps1
#
#  DIKKAT - saf ASCII. PowerShell 5.1 .ps1 dosyalarini ANSI okuyor.
# ============================================================================
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$out = Join-Path $root "web\screenshots"
New-Item -ItemType Directory -Force $out | Out-Null

# Koyu tema. Tanitim goruntuleri koyu temada aliniyor.
New-Item -Path "HKCU:\Software\Syspect" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Syspect" -Name "DarkMode" -Value 1 -Type DWord

Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class ShotUtil {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int ht,bool p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }

  public static void Click(IntPtr h,int cx,int cy){
    RECT r; GetWindowRect(h,out r);
    SetCursorPos(r.L+cx, r.T+cy);
    System.Threading.Thread.Sleep(200);
    mouse_event(0x02,0,0,0,IntPtr.Zero);
    System.Threading.Thread.Sleep(90);
    mouse_event(0x04,0,0,0,IntPtr.Zero);
  }
  public static void Shot(IntPtr h,string path){
    RECT r; GetWindowRect(h,out r);
    var bmp = new Bitmap(r.R-r.L, r.B-r.T);
    using(var g = Graphics.FromImage(bmp)){
      IntPtr dc = g.GetHdc();
      PrintWindow(h, dc, 2);
      g.ReleaseHdc(dc);
    }
    bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    bmp.Dispose();
  }
}
"@ -ReferencedAssemblies System.Drawing

Get-Process ss_ui -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
Start-Sleep -Milliseconds 700

$p = Start-Process (Join-Path $root "build\Release\ss_ui.exe") -PassThru
Start-Sleep -Seconds 5
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq 0) { throw "Pencere bulunamadi" }

# Sabit boyut: goruntuler arasinda tutarli yerlesim.
[ShotUtil]::MoveWindow($h, 60, 60, 1240, 780, $true) | Out-Null
Start-Sleep -Milliseconds 1200
[ShotUtil]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700

# Sekme merkezleri. Yonetici modunda uyari seridi YOK, o yuzden sekmeler
# 52 piksel yukarida: y = 121 - 52 = 69.
$tabY = 69
$tabs = @(
  @{ n = "01-olcum";     x = 222 },
  @{ n = "02-sonuclar";  x = 310 },
  @{ n = "03-grafik";    x = 397 },
  @{ n = "04-mavi-ekran";x = 494 },
  @{ n = "05-sistem";    x = 591 },
  @{ n = "06-ayarlar";   x = 681 }
)

foreach ($t in $tabs) {
  [ShotUtil]::Click($h, $t.x, $tabY)
  Start-Sleep -Milliseconds 1100
  $file = Join-Path $out "$($t.n).png"
  [ShotUtil]::Shot($h, $file)
  Write-Host "  $($t.n).png" -ForegroundColor Green
}

Write-Host ""
Write-Host "Bitti: $out" -ForegroundColor Cyan
Get-ChildItem $out -Filter *.png | Select-Object Name, Length | Format-Table -AutoSize
