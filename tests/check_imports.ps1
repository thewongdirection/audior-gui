<#
  check_imports.ps1 - Fails if an executable depends on a DLL that does not ship
  with Windows 10/11.

  audior-gui may use only WASAPI and the libraries Windows installs by default
  (no .NET, no third-party runtime).  This check keeps it that way: every DLL an
  executable imports must be on the allowlist below, so adding a dependency is a
  deliberate edit to this file.

      check_imports.ps1 <exe> [<exe> ...]

  dumpbin.exe must be on PATH, as it is after vcvarsall.bat.
#>
param(
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
    [string[]]$Executables
)

# In-box Windows DLLs (all in System32 on Windows 10 and 11).  The C runtime
# arrives through the Universal CRT API sets, matched by the pattern further down.
$allowed = @(
    'kernel32', 'user32', 'gdi32', 'advapi32', 'shell32', 'shlwapi', 'ole32', 'oleaut32',
    'comctl32', 'comdlg32', 'dwmapi', 'uxtheme', 'ntdll', 'imm32',
    'd2d1', 'dwrite', 'windowscodecs',
    'mmdevapi', 'avrt', 'mf', 'mfplat', 'mfreadwrite'
)
$allowedPattern = '^api-ms-win-(crt|core)-'

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
        Write-Host "check_imports: $exe OK ($($dlls.Count) DLLs, all in-box)"
    }
}
exit $bad
