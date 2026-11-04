# File: tools/ci/check_clipboard.ps1
# Purpose: Check the Windows clipboard bridge: a translated program reads what Windows copied and its own copy reaches Windows
# Author: Akilesh
# Date: 2026-11-04
#
# Usage: pwsh tools/ci/check_clipboard.ps1 <bundle-exe>
# Sets the Windows clipboard, runs the translated xlib_clipboard program, then checks both what the program printed and
# what is on the clipboard afterwards. Needs a Windows session with a clipboard.

param([Parameter(Mandatory = $true)][string]$Executable)

$ErrorActionPreference = 'Stop'
$expectedRead = 'windows_text=from windows|line two'
$expectedWritten = "from crossport`r`nsecond line"

Set-Clipboard -Value @('from windows', 'line two')
$lines = & $Executable
if ($LASTEXITCODE -ne 0) { throw "the program exited with $LASTEXITCODE" }

if ($lines -notcontains $expectedRead) { throw "the program did not read the Windows text: $($lines -join ' / ')" }
if ($lines -notcontains 'owner_is_us=1') { throw 'the program did not become the clipboard owner' }
if ($lines -notcontains 'served=1') { throw 'the bridge never asked the program for its text' }

$written = Get-Clipboard -Raw
if ($written.TrimEnd("`r", "`n") -ne $expectedWritten) { throw "Windows did not receive the program's text: $written" }
Write-Output 'clipboard bridge ok'
