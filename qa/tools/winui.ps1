# QA-STUDIO-001 : pilotage natif de Studio par de vrais événements Windows (SendInput).
# Usage : . .\qa\tools\winui.ps1 ; $w = Start-Studio -Osp x.osp ; Click $w 100 200 ; Shot $w out.png
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int hh,bool r);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int c);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f,int dx,int dy,uint d,UIntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte sc,uint f,UIntPtr e);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
}
'@
[void][W]::SetProcessDPIAware()

function Start-Studio([string]$Osp, [string[]]$Args2 = @(), [int]$Width = 0, [int]$Height = 0) {
  $exe = (Resolve-Path 'build\msvc\apps\desktop_v2\studio\Debug\openstitch.exe').Path
  $env:PATH += ";$env:QT_ROOT\bin"
  $a = @(); if ($Osp) { $a += (Resolve-Path $Osp).Path }; $a += $Args2
  $p = if ($a.Count) { Start-Process $exe -ArgumentList $a -PassThru } else { Start-Process $exe -PassThru }
  for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.MainWindowHandle -ne 0) { break } }
  Start-Sleep -Seconds 3
  if ($Width -gt 0) { Set-Size $p $Width $Height }
  [void][W]::SetForegroundWindow($p.MainWindowHandle); Start-Sleep -Milliseconds 400
  return $p
}
function Set-Size($p, [int]$cw, [int]$ch) {  # taille de la zone cliente en pixels physiques
  $h = $p.MainWindowHandle; $r = New-Object W+RECT; $c = New-Object W+RECT
  [void][W]::GetWindowRect($h, [ref]$r); [void][W]::GetClientRect($h, [ref]$c)
  $dx = ($r.R - $r.L) - $c.R; $dy = ($r.B - $r.T) - $c.B
  [void][W]::MoveWindow($h, 20, 20, $cw + $dx, $ch + $dy, $true); Start-Sleep -Milliseconds 1200
}
function Client-Origin($p) { $pt = New-Object W+POINT; [void][W]::ClientToScreen($p.MainWindowHandle, [ref]$pt); $pt }
function Client-Size($p) { $c = New-Object W+RECT; [void][W]::GetClientRect($p.MainWindowHandle, [ref]$c); @($c.R, $c.B) }
function Click($p, [int]$x, [int]$y, [switch]$Right, [switch]$Double) {
  [void][W]::SetForegroundWindow($p.MainWindowHandle); $o = Client-Origin $p
  [void][W]::SetCursorPos($o.X + $x, $o.Y + $y); Start-Sleep -Milliseconds 120
  $d = if ($Right) { 0x08 } else { 0x02 }; $u = if ($Right) { 0x10 } else { 0x04 }
  $n = if ($Double) { 2 } else { 1 }
  1..$n | % { [W]::mouse_event($d, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [W]::mouse_event($u, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80 }
  Start-Sleep -Milliseconds 350
}
function Move-Mouse($p, [int]$x, [int]$y) { $o = Client-Origin $p; [void][W]::SetCursorPos($o.X + $x, $o.Y + $y); Start-Sleep -Milliseconds 250 }
function Drag($p, [int]$x1, [int]$y1, [int]$x2, [int]$y2) {
  $o = Client-Origin $p; [void][W]::SetCursorPos($o.X + $x1, $o.Y + $y1); Start-Sleep -Milliseconds 100
  [W]::mouse_event(0x02, 0, 0, 0, [UIntPtr]::Zero)
  1..8 | % { [void][W]::SetCursorPos([int]($o.X + $x1 + ($x2 - $x1) * $_ / 8), [int]($o.Y + $y1 + ($y2 - $y1) * $_ / 8)); Start-Sleep -Milliseconds 40 }
  [W]::mouse_event(0x04, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400
}
function Wheel($p, [int]$x, [int]$y, [int]$delta) { $o = Client-Origin $p; [void][W]::SetCursorPos($o.X + $x, $o.Y + $y); [W]::mouse_event(0x0800, 0, 0, [uint32]$delta, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400 }
function Keys($p, [string]$sendKeys) { [void][W]::SetForegroundWindow($p.MainWindowHandle); Start-Sleep -Milliseconds 150; [System.Windows.Forms.SendKeys]::SendWait($sendKeys); Start-Sleep -Milliseconds 400 }
function Shot($p, [string]$path) {
  $o = Client-Origin $p; $s = Client-Size $p
  $bmp = New-Object System.Drawing.Bitmap $s[0], $s[1]; $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($o.X, $o.Y, 0, 0, $bmp.Size); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
}
function Stop-Studio($p) { try { $p.CloseMainWindow() | Out-Null; Start-Sleep 1; if (-not $p.HasExited) { $p.Kill() } } catch {} }
