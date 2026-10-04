param(
    [Parameter(Mandatory=$true)][int]$ParentId,
    [Parameter(Mandatory=$true)][string]$Staged,
    [Parameter(Mandatory=$true)][string]$Target,
    [Parameter(Mandatory=$true)][string]$FailureLog
)
$ErrorActionPreference = 'Stop'
$work = $null
$backup = $null
$oldMoved = $false
try {
    $parent = Get-Process -Id $ParentId -ErrorAction SilentlyContinue
    if ($parent -and -not $parent.WaitForExit(120000)) { throw 'JPet did not exit within 120 seconds.' }
    if (-not (Test-Path -LiteralPath (Join-Path $Staged 'JPet.exe'))) { throw 'JPet.exe is missing.' }
    if (-not (Test-Path -LiteralPath $Target -PathType Container)) { throw 'The installation directory is missing.' }
    $work = Join-Path (Split-Path -Parent $Target) ('.jpet-update-' + [guid]::NewGuid().ToString('N'))
    $prepared = Join-Path $work 'new'
    $backup = Join-Path $work 'previous'
    New-Item -ItemType Directory -Path $work | Out-Null
    Copy-Item -LiteralPath $Staged -Destination $prepared -Recurse
    # Keep Inno Setup's uninstaller and its installation log across ZIP updates.
    Get-ChildItem -LiteralPath $Target -File | Where-Object {
        $_.Name -match '^unins\d+\.(exe|dat|msg)$'
    } | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $prepared -Force }
    Move-Item -LiteralPath $Target -Destination $backup
    $oldMoved = $true
    Move-Item -LiteralPath $prepared -Destination $Target
    Start-Process -FilePath (Join-Path $Target 'JPet.exe') -WorkingDirectory $Target
    Remove-Item -LiteralPath $backup -Recurse -Force
    Remove-Item -LiteralPath $FailureLog -Force -ErrorAction SilentlyContinue
} catch {
    $_ | Out-String | Set-Content -LiteralPath $FailureLog -Encoding UTF8
    if ($oldMoved -and (Test-Path -LiteralPath $backup)) {
        try {
            if (Test-Path -LiteralPath $Target) { Move-Item -LiteralPath $Target -Destination (Join-Path $work 'failed') }
            Move-Item -LiteralPath $backup -Destination $Target
        } catch {
            "旧版本保存在：$backup" | Add-Content -LiteralPath $FailureLog -Encoding UTF8
        }
    }
    if (Test-Path -LiteralPath (Join-Path $Target 'JPet.exe')) {
        Start-Process -FilePath (Join-Path $Target 'JPet.exe') -WorkingDirectory $Target
    }
    exit 1
} finally {
    # Keep the backup if recovery could not restore it.
    if ($work -and -not (Test-Path -LiteralPath $backup)) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
}
