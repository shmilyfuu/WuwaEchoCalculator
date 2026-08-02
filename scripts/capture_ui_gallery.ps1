param(
    [Parameter(Mandatory = $true)]
    [string]$PreviewDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [string]$SampleImage
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class UiCaptureNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
}
'@

function Get-ProcessWindows {
    param([int]$ProcessId)
    $windows = [System.Collections.Generic.List[object]]::new()
    $callback = [UiCaptureNative+EnumWindowsProc]{
        param([IntPtr]$hwnd, [IntPtr]$lParam)
        [uint32]$ownerPid = 0
        [void][UiCaptureNative]::GetWindowThreadProcessId($hwnd, [ref]$ownerPid)
        if ($ownerPid -eq $ProcessId -and [UiCaptureNative]::IsWindowVisible($hwnd)) {
            $title = [Text.StringBuilder]::new(512)
            $class = [Text.StringBuilder]::new(128)
            [void][UiCaptureNative]::GetWindowText($hwnd, $title, $title.Capacity)
            [void][UiCaptureNative]::GetClassName($hwnd, $class, $class.Capacity)
            $windows.Add([pscustomobject]@{ Handle = $hwnd; Title = $title.ToString(); Class = $class.ToString() })
        }
        return $true
    }
    [void][UiCaptureNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $windows
}

function Wait-Window {
    param(
        [Diagnostics.Process]$Process,
        [string]$Class = '',
        [int]$TimeoutMilliseconds = 10000
    )
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        $Process.Refresh()
        $windows = Get-ProcessWindows -ProcessId $Process.Id
        $match = if ($Class) { $windows | Where-Object Class -eq $Class | Select-Object -First 1 } else { $windows | Select-Object -First 1 }
        if ($match) { return $match.Handle }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline -and -not $Process.HasExited)
    throw "Window not found for process $($Process.Id), class '$Class'"
}

function Save-WindowScreenshot {
    param([IntPtr]$Handle, [string]$Path)
    Write-Output "Capturing: $Path"
    $rect = New-Object UiCaptureNative+RECT
    if (-not [UiCaptureNative]::GetWindowRect($Handle, [ref]$rect)) { throw "GetWindowRect failed" }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { throw "Invalid window size: ${width}x${height}" }
    $bitmap = [Drawing.Bitmap]::new($width, $height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $hdc = $graphics.GetHdc()
        try {
            if (-not [UiCaptureNative]::PrintWindow($Handle, $hdc, 2)) { throw 'PrintWindow failed' }
        } finally {
            $graphics.ReleaseHdc($hdc)
        }
        $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Stop-PreviewProcess {
    param([Diagnostics.Process]$Process)
    if ($Process.HasExited) { return }
    [void]$Process.CloseMainWindow()
    if (-not $Process.WaitForExit(1500)) { $Process.Kill(); $Process.WaitForExit() }
}

function Send-LogicalClick {
    param([IntPtr]$Handle, [int]$X, [int]$Y)
    $client = New-Object UiCaptureNative+RECT
    if (-not [UiCaptureNative]::GetClientRect($Handle, [ref]$client)) { throw 'GetClientRect failed' }
    $pixelX = [Math]::Round($X * ($client.Right - $client.Left) / 1188.0)
    $pixelY = [Math]::Round($Y * ($client.Bottom - $client.Top) / 772.0)
    $packed = [int32](($pixelY -shl 16) -bor ($pixelX -band 0xffff))
    $lParam = [IntPtr]$packed
    [void][UiCaptureNative]::PostMessage($Handle, 0x0201, [IntPtr]1, $lParam)
    Start-Sleep -Milliseconds 80
    [void][UiCaptureNative]::PostMessage($Handle, 0x0202, [IntPtr]0, $lParam)
}

function Capture-AppState {
    param([string]$State, [string]$FileName)
    $target = Join-Path $OutputDirectory $FileName
    if (Test-Path -LiteralPath $target) { return }
    $arguments = @('--ui-preview', $State, '--preview-image', $SampleImage)
    $process = Start-Process -FilePath $script:appExe -ArgumentList $arguments -PassThru
    try {
        $handle = Wait-Window -Process $process
        Start-Sleep -Milliseconds 500
        Save-WindowScreenshot -Handle $handle -Path $target
    } finally {
        Stop-PreviewProcess -Process $process
    }
}

function Capture-UpdaterState {
    param([string]$State, [string]$FileName, [switch]$Dialog)
    $target = Join-Path $OutputDirectory $FileName
    if (Test-Path -LiteralPath $target) { return }
    $process = Start-Process -FilePath $script:updaterExe -ArgumentList @('--ui-preview', $State) -PassThru
    try {
        $handle = Wait-Window -Process $process -Class $(if ($Dialog) { 'WuwaEchoUpdaterFailureWindow' } else { '' })
        Start-Sleep -Milliseconds 500
        Save-WindowScreenshot -Handle $handle -Path $target
        if ($Dialog) { [void][UiCaptureNative]::SendMessage($handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
    } finally {
        Stop-PreviewProcess -Process $process
    }
}

$script:appExe = Join-Path $PreviewDirectory 'WuwaEchoUiPreview.exe'
$script:updaterExe = Join-Path $PreviewDirectory 'WuwaEchoUpdaterUiPreview.exe'
foreach ($required in @($script:appExe, $script:updaterExe, $SampleImage)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required file missing: $required" }
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$appStates = @(
    @('main-empty',             '01_main_empty.png'),
    @('main-recognized',        '02_main_recognized_and_five_records.png'),
    @('ocr-recognizing',        '03_ocr_recognizing.png'),
    @('ocr-error',              '04_ocr_no_valid_attributes.png'),
    @('dropdown-attribute',     '05_dropdown_attribute.png'),
    @('dropdown-value',         '06_dropdown_value.png'),
    @('dropdown-slot',          '07_dropdown_record_slot.png'),
    @('confirm-clear',          '08_confirm_clear_all.png'),
    @('confirm-delete',         '09_confirm_delete_one.png'),
    @('confirm-overwrite',      '10_confirm_overwrite.png'),
    @('export-normal',          '11_export_dialog_normal.png'),
    @('export-invalid',         '12_export_dialog_title_too_long.png'),
    @('update-checking',        '13_update_checking_no_modal.png'),
    @('update-available',       '14_update_available.png'),
    @('update-available-long',  '14b_update_available_long_scroll.png'),
    @('update-preparing',       '15_update_preparing.png'),
    @('update-downloading',     '16_update_downloading.png'),
    @('update-fallback',        '17_update_fallback_gitee_to_github.png'),
    @('update-verifying',       '18_update_verifying.png'),
    @('update-ready',           '19_update_ready.png'),
    @('update-ready-deferred',  '20_update_deferred_main_window.png'),
    @('update-latest',          '21_update_up_to_date.png'),
    @('update-error',           '22_update_error.png'),
    @('update-cancelled',       '23_update_cancelled.png')
)
foreach ($entry in $appStates) { Capture-AppState -State $entry[0] -FileName $entry[1] }

$updaterStates = @(
    @('updater-waiting',    '26_updater_waiting_for_app.png'),
    @('updater-preparing',  '27_updater_preparing_files.png'),
    @('updater-checking',   '28_updater_checking_files.png'),
    @('updater-backup',     '29_updater_backup.png'),
    @('updater-replacing',  '30_updater_replacing_files.png'),
    @('updater-cleaning',   '31_updater_cleaning.png'),
    @('updater-restarting', '32_updater_restarting.png'),
    @('updater-complete',   '33_updater_complete.png'),
    @('updater-error',      '34_updater_error_state.png')
)
foreach ($entry in $updaterStates) { Capture-UpdaterState -State $entry[0] -FileName $entry[1] }
Capture-UpdaterState -State 'updater-error-dialog' -FileName '35_custom_dialog_updater_error.png' -Dialog

# Windows common dialogs are triggered through the real controls so their screenshots match the host OS.
$openProcess = Start-Process -FilePath $script:appExe -ArgumentList @('--ui-preview','main-recognized','--preview-image',$SampleImage) -PassThru
try {
    $main = Wait-Window -Process $openProcess
    Send-LogicalClick -Handle $main -X 100 -Y 142
    $dialog = Wait-Window -Process $openProcess -Class '#32770'
    Save-WindowScreenshot -Handle $dialog -Path (Join-Path $OutputDirectory '24_system_dialog_open_image.png')
    [void][UiCaptureNative]::SendMessage($dialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
} finally { Stop-PreviewProcess -Process $openProcess }

$saveProcess = Start-Process -FilePath $script:appExe -ArgumentList @('--ui-preview','export-normal','--preview-image',$SampleImage) -PassThru
try {
    $main = Wait-Window -Process $saveProcess
    Send-LogicalClick -Handle $main -X 610 -Y 446
    $dialog = Wait-Window -Process $saveProcess -Class '#32770'
    Save-WindowScreenshot -Handle $dialog -Path (Join-Path $OutputDirectory '25_system_dialog_save_png.png')
    [void][UiCaptureNative]::SendMessage($dialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
} finally { Stop-PreviewProcess -Process $saveProcess }

Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.png' | Sort-Object Name | Select-Object Name,Length
