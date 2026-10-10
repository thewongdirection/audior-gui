<#
  ui_smoke.ps1 - Starts the real application, drives it like a user and looks at it.

  Runs on a Windows machine with a desktop (the CI runner, or your own PC):

      powershell -ExecutionPolicy Bypass -File tests\ui_smoke.ps1 [-Exe bin\x64\audior_gui.exe] [-Out shots]

  1. The command-line options: --help, --list-devices, --stop, a bad option.
  2. The window with simulated audio (--demo): clicks Start, then Stop.
  3. The window with the real WASAPI engine, started by --output: if the machine has
     a microphone it records and --stop ends it; if not (a CI runner), the window
     must report the problem instead of crashing.

  The window is custom-drawn, so it is checked by the colour of its status dot and
  by screenshots (PNG files in -Out).  Written for Windows PowerShell 5.1 and
  PowerShell 7; assumes the 100% display scale of a CI runner.
#>
param(
    [string]$Exe = 'bin\x64\audior_gui.exe',
    [string]$Out = 'shots'
)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$Work = Join-Path ([IO.Path]::GetTempPath()) ("audior-gui-smoke-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

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
    public static Bitmap Capture(IntPtr hwnd) {
        int[] size = ClientSize(hwnd);
        Bitmap bmp = new Bitmap(size[0], size[1], System.Drawing.Imaging.PixelFormat.Format24bppRgb);
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
        if ($p.HasExited) { throw "audior_gui exited with code $($p.ExitCode) before showing a window" }
        if ($p.MainWindowHandle -eq 0) { $p.Kill(); throw 'audior_gui showed no window within 10 seconds' }
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
    function Dot($bmp) { $bmp.GetPixel($bmp.Width - 138, 32) }
    function IsRed($c)   { $c.R -ge 120 -and $c.R -gt ($c.G + 40) }
    function IsGreen($c) { $c.G -ge 120 -and $c.G -gt ($c.R + 60) }

    function Click-Start($p) {
        $s = [Win]::ClientSize($p.MainWindowHandle)
        [Win]::Click($p.MainWindowHandle, $s[0] - 119, $s[1] - 48)
    }
    function Click-Stop($p) {
        $s = [Win]::ClientSize($p.MainWindowHandle)
        [Win]::Click($p.MainWindowHandle, $s[0] - 281, $s[1] - 48)
    }
    # The file name box: in the settings panel, 152 DIPs from the panel's left edge, 188 from the top.
    function Click-FileName($p) {
        $s = [Win]::ClientSize($p.MainWindowHandle)
        [Win]::Click($p.MainWindowHandle, $s[0] - 194, 188)
    }
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
