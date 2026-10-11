<#
  check_imports.ps1 - Fails if an executable depends on a DLL that does not ship
  with Windows 10/11.

  audior-gui may use only WASAPI and the libraries Windows installs by default
  (no .NET, no third-party runtime).  This check keeps it that way: every DLL an
  executable imports must be on the allowlist below, so adding a dependency is a
  deliberate edit to this file.

      check_imports.ps1 [-Allow <dll>[,<dll>...]] <exe> [<exe> ...]

  -Allow names DLLs (without ".dll") that the files given may import besides
  the base list.  Only two programs next to the recorder use it:
    - the optional transcription helper (build.bat asr): its own DLLs (whisper,
      ggml and the like, which sit next to it and are checked against a hash list
      before it is started), and for ggml-vulkan.dll the graphics driver's vulkan-1;
    - the download program audior_fetch.exe: winhttp, Windows' own HTTP library.
      It is the only file of the project that may touch the network, so winhttp
      is not on the base list: the recorder and the helper must never import it.
  The recorder itself, audior_gui.exe, is never checked with it.

  dumpbin.exe must be on PATH, as it is after vcvarsall.bat.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [string[]]$Allow = @(),
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
    [string[]]$Executables
)

# In-box Windows DLLs (all in System32 on Windows 10 and 11).  The C runtime
# arrives through the Universal CRT API sets, matched by the pattern further down.
$allowed = @(
    'kernel32', 'user32', 'gdi32', 'advapi32', 'shell32', 'shlwapi', 'ole32', 'oleaut32',
    'comctl32', 'comdlg32', 'dwmapi', 'uxtheme', 'ntdll', 'imm32',
    'd2d1', 'dwrite', 'windowscodecs', 'dxgi',
    'mmdevapi', 'avrt', 'mf', 'mfplat', 'mfreadwrite'
)
$allowedPattern = '^api-ms-win-(crt|core)-'
$allowed += @($Allow | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })

$bad = 0
foreach ($exe in $Executables) {
    $lines = & dumpbin.exe /nologo /dependents $exe 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "check_imports: dumpbin failed on $exe"
        $bad++
        continue
    }
    $dlls = @($lines | ForEach-Object { if ($_ -match '^\s+([\w.\-]+)\.dll\s*$') { $Matches[1].ToLowerInvariant() } })
    $offenders = @($dlls | Where-Object { ($allowed -notcontains $_) -and ($_ -notmatch $allowedPattern) })

    if ($offenders.Count -gt 0) {
        Write-Host "check_imports: $exe imports DLLs that do not ship with Windows:"
        $offenders | ForEach-Object { Write-Host "    $_.dll" }
        $bad++
    } else {
        Write-Host "check_imports: $exe OK ($($dlls.Count) DLLs, all in-box$(if ($Allow.Count) { ' or allowed' }))"
    }
}
exit $bad
