<#
  ui_smoke.ps1 - Starts the real application, drives it like a user and looks at it.

  Runs on a Windows machine with a desktop (the CI runner, or your own PC):

      powershell -ExecutionPolicy Bypass -File tests\ui_smoke.ps1 [-Exe bin\x64\Audior-GUI.exe] [-Out shots]

  1. The command-line options: --help, --list-devices, --stop, a bad option.
  2. The window with simulated audio (--demo): clicks Start, then Stop.
  3. The window with the real WASAPI engine, started by --output: if the machine has
     a microphone it records and --stop ends it; if not (a CI runner), the window
     must report the problem instead of crashing.
  4. The settings file: a --demo or --defaults run leaves none, a normal run leaves its settings.

  The program remembers its settings in Audior-GUI.ini beside the exe, so the script runs a
  copy of the exe in a scratch folder: it neither uses nor changes the settings of your build.

  The window is custom-drawn, so it is checked by the colour of its status dot and
  by screenshots (PNG files in -Out).  Written for Windows PowerShell 5.1, which is
  what CI runs (PowerShell 7 cannot compile the System.Drawing helper below).  It
  works at any display scale: the script is per-monitor DPI aware, like the program,
  and places its clicks and its pixel probe from the window's DPI.
#>
param(
    [string]$Exe = 'bin\x64\Audior-GUI.exe',
    [string]$Out = 'shots'
)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$Work = Join-Path ([IO.Path]::GetTempPath()) ("audior-gui-smoke-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$Copy = Join-Path $Work 'program'
New-Item -ItemType Directory -Force -Path $Copy | Out-Null
Copy-Item $Exe (Join-Path $Copy 'Audior-GUI.exe')
$Exe = Join-Path $Copy 'Audior-GUI.exe'
$Ini = Join-Path $Copy 'Audior-GUI.ini'

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;

public static class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr hWnd);

    // Without this, Windows hands a script on a scaled display virtual coordinates: the client size
    // is reported smaller than it is, and a capture would cover only the top left of the window.
    public static void MakeDpiAware() { SetProcessDpiAwarenessContext((IntPtr)(-4)); }      // per-monitor v2

    // How many pixels a DIP (the unit the program lays out in) is on this window's monitor: 1.5 at 150%.
    public static double Scale(IntPtr hwnd) { return GetDpiForWindow(hwnd) / 96.0; }

    public static int[] ClientSize(IntPtr hwnd) {
        RECT r; GetClientRect(hwnd, out r);
        return new int[] { r.Right - r.Left, r.Bottom - r.Top };
    }

    // Typed characters, as the keyboard would deliver them (WM_CHAR).
    public static void Type(IntPtr hwnd, string text) {
        foreach (char c in text) PostMessage(hwnd, 0x0102, (IntPtr)(int)c, IntPtr.Zero);
    }

    // A left click at a client position (the window gets the messages even if it is not in front).
    public static void Click(IntPtr hwnd, int x, int y) {
        IntPtr pos = (IntPtr)((y << 16) | (x & 0xFFFF));
        PostMessage(hwnd, 0x0200, IntPtr.Zero, pos);          // WM_MOUSEMOVE
        PostMessage(hwnd, 0x0201, (IntPtr)1, pos);            // WM_LBUTTONDOWN
        PostMessage(hwnd, 0x0202, IntPtr.Zero, pos);          // WM_LBUTTONUP
    }

    // The window's client area as it is composed (PW_CLIENTONLY | PW_RENDERFULLCONTENT).  If that
    // comes back empty, the pixels are read off the screen instead (the window must be visible).
    // The picture carries the window's scale in its Tag.
    public static Bitmap Capture(IntPtr hwnd) {
        int[] size = ClientSize(hwnd);
        Bitmap bmp = new Bitmap(size[0], size[1], System.Drawing.Imaging.PixelFormat.Format24bppRgb);
        bmp.Tag = Scale(hwnd);
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            PrintWindow(hwnd, hdc, 3);
            g.ReleaseHdc(hdc);
        }
        if (IsBlank(bmp)) {
            POINT origin = new POINT();
            ClientToScreen(hwnd, ref origin);
            using (Graphics g = Graphics.FromImage(bmp)) g.CopyFromScreen(origin.X, origin.Y, 0, 0, bmp.Size);
        }
        return bmp;
    }

    static bool IsBlank(Bitmap bmp) {
        for (int i = 1; i <= 5; i++) {
            Color c = bmp.GetPixel(bmp.Width * i / 6, bmp.Height * i / 6);
            if (c.R != 0 || c.G != 0 || c.B != 0) return false;
        }
        return true;
    }
}
'@
[Win]::MakeDpiAware()

$failures = New-Object System.Collections.Generic.List[string]
function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Host "  ok    $what" } else { Write-Host "  FAIL  $what"; $failures.Add($what) }
}

function Run-Cli([string[]]$arguments) {
    $o = Join-Path $Work 'out.txt'; $e = Join-Path $Work 'err.txt'
    $p = Start-Process -FilePath $Exe -ArgumentList $arguments -Wait -PassThru `
        -RedirectStandardOutput $o -RedirectStandardError $e
    [pscustomobject]@{ Code = $p.ExitCode; Out = [string](Get-Content $o -Raw); Err = [string](Get-Content $e -Raw) }
}

$started = New-Object System.Collections.Generic.List[object]    # windows this script opened
try {
    # ---- 1. the command line --------------------------------------------------
    Write-Host '== command line'
    $r = Run-Cli @('--help')
    Check ($r.Code -eq 0 -and $r.Out -match '--include-output' -and $r.Out -match '--separate') '--help lists the options'
    $r = Run-Cli @('--list-devices')
    Write-Host $r.Out
    Check ($r.Code -eq 0 -and $r.Out -match '=== Audio Devices ===') '--list-devices prints the device list'
    $r = Run-Cli @('--stop')
    Check ($r.Code -eq 1 -and $r.Err -match 'No active recording to stop') '--stop with nothing recording fails politely'
    $r = Run-Cli @('--frobnicate')
    Check ($r.Code -eq 1 -and $r.Err -match 'unknown option') 'an unknown option is rejected'
    $r = Run-Cli @('--amplify', '500', '--help')
    Check ($r.Code -eq 0 -and $r.Err -match 'Warning') 'an out-of-range value is clamped with a warning'

    # ---- the window -------------------------------------------------------------
    function Start-Window([string[]]$arguments) {
        $p = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru
        for ($i = 0; $i -lt 100 -and $p.MainWindowHandle -eq 0 -and -not $p.HasExited; $i++) {
            Start-Sleep -Milliseconds 100
            $p.Refresh()
        }
        if ($p.HasExited) { throw "Audior-GUI exited with code $($p.ExitCode) before showing a window" }
        if ($p.MainWindowHandle -eq 0) { $p.Kill(); throw 'Audior-GUI showed no window within 10 seconds' }
        $null = $p.Handle                  # keep the process handle so ExitCode can be read later
    $started.Add($p)
        Start-Sleep -Milliseconds 700      # the first frame
        $p
    }

    function Shot($p, [string]$name) {
        $bmp = [Win]::Capture($p.MainWindowHandle)
        $bmp.Save((Join-Path $Out "ui-$name.png"), [Drawing.Imaging.ImageFormat]::Png)
        $bmp
    }

    # The status pill's dot, top right: green when ready, red while recording or after a problem.
    # It sits 138 DIPs from the right edge and 32 from the top; the picture carries the scale.
    function Dot($bmp) {
        $k = $bmp.Tag
        $bmp.GetPixel([int]($bmp.Width - 138 * $k), [int](32 * $k))
    }
    function IsRed($c)   { $c.R -ge 120 -and $c.R -gt ($c.G + 40) }
    function IsGreen($c) { $c.G -ge 120 -and $c.G -gt ($c.R + 60) }

    # A click at a place given in DIPs: `right` from the window's right edge, and `y` from its top
    # edge, or from its bottom edge when `fromBottom`.
    function Click-At($p, [double]$right, [double]$y, [bool]$fromBottom) {
        $h = $p.MainWindowHandle
        $k = [Win]::Scale($h)
        $s = [Win]::ClientSize($h)
        $yPixels = if ($fromBottom) { $s[1] - $y * $k } else { $y * $k }
        [Win]::Click($h, [int]($s[0] - $right * $k), [int]$yPixels)
    }
    function Click-Start($p) { Click-At $p 119 48 $true }
    function Click-Stop($p)  { Click-At $p 281 48 $true }
    # The file name box: in the settings panel, 152 DIPs from the panel's left edge, 188 from the top.
    function Click-FileName($p) { Click-At $p 194 188 $false }
    function Close-Window($p) {
        [void]$p.CloseMainWindow()
        if (-not $p.WaitForExit(10000)) { $p.Kill(); $failures.Add('the window did not close'); return }
        Check ($p.ExitCode -eq 0) 'the window closes with exit code 0'
    }

    # ---- 2. simulated audio -------------------------------------------------------
    Write-Host '== window, simulated audio (--demo)'
    $p = Start-Window @('--demo')
    $bmp = Shot $p 'demo-idle'
    Check (IsGreen (Dot $bmp)) 'idle: the status dot is green'
    Click-FileName $p
    [Win]::Type($p.MainWindowHandle, 'weekly sync: notes?')       # ':' and '?' are not allowed in file names
    Start-Sleep -Milliseconds 400
    $null = Shot $p 'demo-typed'
    Click-Start $p
    Start-Sleep -Milliseconds 2500
    $bmp = Shot $p 'demo-recording'
    Check (IsRed (Dot $bmp)) 'after Start: the status dot is red (recording)'
    Click-Stop $p
    Start-Sleep -Milliseconds 800
    $bmp = Shot $p 'demo-stopped'
    Check (IsGreen (Dot $bmp)) 'after Stop: the status dot is green again'
    Close-Window $p
    Check (-not (Test-Path $Ini)) 'a --demo run remembers nothing'

    # ---- 3. the real engine ---------------------------------------------------------
    Write-Host '== window, real WASAPI engine (--output)'
    $file = Join-Path $Work 'ci-real.wav'
    $p = Start-Window @('--output', "`"$file`"", '--format', 'wav')
    Start-Sleep -Milliseconds 2500
    $bmp = Shot $p 'real-started'
    Check (IsRed (Dot $bmp)) 'started by --output: recording, or a reported problem (red dot)'
    $stop = Run-Cli @('--stop')
    if ($stop.Code -eq 0) {
        Write-Host '  (this machine has a microphone: the recording is running)'
        Start-Sleep -Milliseconds 2000
        $bmp = Shot $p 'real-stopped'
        Check (IsGreen (Dot $bmp)) '--stop ended the recording: the status dot is green'
        $wav = Get-Item -ErrorAction SilentlyContinue $file
        Check ($null -ne $wav -and $wav.Length -gt 44) 'the WAV file was written'
    } else {
        Write-Host '  (no microphone here: the window must report the problem and carry on)'
        Check ($stop.Err -match 'No active recording to stop') '--stop agrees that nothing is recording'
        Check (-not (Test-Path $file)) 'no empty file was left behind'
    }
    Close-Window $p

    # ---- 4. the settings file ---------------------------------------------------------
    Write-Host '== remembered settings'
    Check (Test-Path $Ini) 'the run left its settings in Audior-GUI.ini beside the exe'
    $saved = if (Test-Path $Ini) { [string](Get-Content $Ini -Raw -Encoding UTF8) } else { '' }
    Check ($saved -match '(?m)^format=wav\r?$') 'the file holds the format of the run'
    Check ($saved -match ('(?m)^folder=' + [regex]::Escape($Work))) 'the file holds the folder of the run'
    Remove-Item $Ini -ErrorAction SilentlyContinue
    $p = Start-Window @('--defaults', '--format', 'wav')
    Close-Window $p
    Check (-not (Test-Path $Ini)) 'a --defaults run remembers nothing'
} finally {
    foreach ($p in $started) { if (-not $p.HasExited) { $p.Kill() } }
    Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
}
if ($failures.Count -gt 0) {
    Write-Host ''
    Write-Host "$($failures.Count) check(s) failed:"
    $failures | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
Write-Host 'ui_smoke: all checks passed'
