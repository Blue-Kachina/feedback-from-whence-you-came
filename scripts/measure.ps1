<#
.SYNOPSIS
    Phase 5 measurements: replay recorded sessions through the suppressor and report.

.DESCRIPTION
    Reads a session manifest (measurements\sessions.csv), runs fwyc_analyze over every recording
    and writes a report: time from feedback onset to detection, from detection to a 10 dB drop,
    overshoot, false and missed detections, emergency gain use, stable-gain improvement
    (measurements\stable-gain.csv) and CPU use.

    Each recording must be the plug-in's input: the dry mic signal, recorded before any
    processing. The suppressor is deterministic, so replaying that input reproduces exactly
    what the plug-in did live, and in a session recorded with the plug-in on, the drop after a
    detection is what the notches achieved in the room. See docs\phase5-measurement.md.

    Manifest columns (paths relative to the manifest):
      file      WAV file
      plugin    on | bypassed | none   (none: recorded without the plug-in in the loop)
      kind      feedback | vocal | mixed
                vocal: no feedback happened, so every detection is false
                feedback / mixed: list the feedback windows; detections outside them are false
      feedback  feedback windows in seconds, e.g. "5.5-9.6;14.0-15.5" (from a waveform editor)
      buffer    host buffer size in samples (replayed with the same block size)
      position, room, notes   free text, copied to the report

.PARAMETER Manifest
    Session manifest (default measurements\sessions.csv).

.PARAMETER StableGain
    Stable-gain results (default measurements\stable-gain.csv); skipped if missing or empty.

.PARAMETER OutDir
    Where the report, per-episode CSV, JSON and processed audio go (default build\measurements).

.PARAMETER BinDir
    Folder with fwyc_analyze.exe. Defaults to the first Release build found.

.EXAMPLE
    .\scripts\measure.ps1
#>
[CmdletBinding()]
param(
    [string]$Manifest,
    [string]$StableGain,
    [string]$OutDir,
    [string]$BinDir
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Manifest) { $Manifest = Join-Path $RepoRoot 'measurements\sessions.csv' }
if (-not $StableGain) { $StableGain = Join-Path $RepoRoot 'measurements\stable-gain.csv' }
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot 'build\measurements' }

# Detections up to this long before a labelled window, or after it, still belong to it:
# labels come from eyeballing a waveform.
$WindowLeadSeconds = 0.25
$WindowTrailSeconds = 0.5

if (-not $BinDir) {
    foreach ($candidate in @('build\windows-x64\bin\Release', 'build\ci\bin\Release')) {
        $path = Join-Path $RepoRoot $candidate
        if (Test-Path (Join-Path $path 'fwyc_analyze.exe')) { $BinDir = $path; break }
    }
}
if (-not $BinDir -or -not (Test-Path (Join-Path $BinDir 'fwyc_analyze.exe'))) {
    throw "fwyc_analyze.exe not found. Build first: .\scripts\build.ps1"
}
$Analyzer = Join-Path $BinDir 'fwyc_analyze.exe'
if (-not (Test-Path $Manifest)) { throw "Manifest not found: $Manifest" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$manifestDir = Split-Path -Parent (Resolve-Path $Manifest)

function Get-Median([double[]]$Values) {
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    $sorted = $Values | Sort-Object
    $mid = [int][math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2) { return [double]$sorted[$mid] }
    return ([double]$sorted[$mid - 1] + [double]$sorted[$mid]) / 2
}

function Format-Number($Value, [string]$Format = '0') {
    if ($null -eq $Value) { return '-' }
    return ([double]$Value).ToString($Format, [Globalization.CultureInfo]::InvariantCulture)
}

function Format-Stat([double[]]$Values, [string]$Format = '0') {
    if (-not $Values -or $Values.Count -eq 0) { return '-' }
    $max = ($Values | Measure-Object -Maximum).Maximum
    return "$(Format-Number (Get-Median $Values) $Format) / $(Format-Number $max $Format)"
}

function ConvertTo-Windows([string]$Text) {
    $windows = @()
    if (-not $Text) { return ,$windows }
    foreach ($part in $Text.Split(';')) {
        $bounds = $part.Trim().Split('-')
        if ($bounds.Count -ne 2) { throw "Bad feedback window '$part' (expected start-end in seconds)" }
        $windows += [pscustomobject]@{
            Start = [double]::Parse($bounds[0], [Globalization.CultureInfo]::InvariantCulture)
            End   = [double]::Parse($bounds[1], [Globalization.CultureInfo]::InvariantCulture)
        }
    }
    return ,$windows
}

$sessions = @(Import-Csv $Manifest)
if ($sessions.Count -eq 0) { throw "No sessions in $Manifest" }

$rows = @()
$allEpisodes = @()
foreach ($s in $sessions) {
    $wav = Join-Path $manifestDir $s.file
    if (-not (Test-Path $wav)) { throw "Recording not found: $wav" }
    $name = [IO.Path]::GetFileNameWithoutExtension($wav)
    $buffer = if ($s.buffer) { [int]$s.buffer } else { 64 }
    $plugin = if ($s.plugin) { $s.plugin.Trim().ToLower() } else { 'on' }
    $kind = if ($s.kind) { $s.kind.Trim().ToLower() } else { 'feedback' }
    if ($plugin -notin @('on', 'bypassed', 'none')) { throw "$($s.file): plugin must be on, bypassed or none" }
    if ($kind -notin @('feedback', 'vocal', 'mixed')) { throw "$($s.file): kind must be feedback, vocal or mixed" }
    $windows = ConvertTo-Windows $s.feedback

    Write-Host "Analysing $($s.file) (buffer $buffer, plug-in $plugin)" -ForegroundColor Cyan
    $json = Join-Path $OutDir "$name.json"
    $log = Join-Path $OutDir "$name.log"
    & $Analyzer $wav --block $buffer --json $json --out (Join-Path $OutDir "$name.processed.wav") > $log
    if ($LASTEXITCODE -ne 0) { throw "fwyc_analyze failed on $wav (see $log)" }
    $result = Get-Content $json -Raw | ConvertFrom-Json

    # Classify each episode against the labelled windows.
    $windowHit = @{}
    $episodes = @()
    foreach ($e in $result.episodes) {
        $window = $null
        for ($i = 0; $i -lt $windows.Count; $i++) {
            $w = $windows[$i]
            if ($e.detectSeconds -ge $w.Start - $WindowLeadSeconds -and $e.detectSeconds -le $w.End + $WindowTrailSeconds) {
                $window = $i; break
            }
        }
        $verdict = if ($kind -eq 'vocal') { 'false' }
                   elseif ($null -ne $window) { 'true' }
                   elseif ($windows.Count -gt 0) { 'false' }
                   else { 'unlabelled' }
        if ($verdict -eq 'true' -and -not $windowHit.ContainsKey($window)) { $windowHit[$window] = $e.detectSeconds }

        $attenuatedMs = $null
        if ($plugin -eq 'on' -and $null -ne $e.attenuatedSeconds) {
            $attenuatedMs = 1000 * ($e.attenuatedSeconds - $e.detectSeconds)
        }
        $episode = [pscustomobject]@{
            file              = $s.file
            plugin            = $plugin
            buffer            = $buffer
            verdict           = $verdict
            frequency_hz      = [math]::Round($e.frequencyHz, 1)
            onset_s           = $e.bornSeconds
            detect_s          = $e.detectSeconds
            onset_to_detect_ms = [math]::Round(1000 * ($e.detectSeconds - $e.bornSeconds), 1)
            detect_level_db   = $e.detectLevelDb
            peak_level_db     = $e.peakLevelDb
            overshoot_db      = [math]::Round($e.peakLevelDb - $e.detectLevelDb, 2)
            detect_to_10db_ms = if ($null -ne $attenuatedMs) { [math]::Round($attenuatedMs, 1) } else { $null }
            faded             = $e.faded
            emergency         = $e.emergency
        }
        $episodes += $episode
    }
    $allEpisodes += $episodes

    $hits = @($episodes | Where-Object verdict -eq 'true')
    $labelledDelays = @()
    foreach ($key in $windowHit.Keys) { $labelledDelays += 1000 * ($windowHit[$key] - $windows[$key].Start) }
    $falseCount = @($episodes | Where-Object verdict -eq 'false').Count

    $rows += [pscustomobject]@{
        File          = $s.file
        Plugin        = $plugin
        Kind          = $kind
        Buffer        = $buffer
        Position      = $s.position
        Room          = $s.room
        Notes         = $s.notes
        Minutes       = $result.durationSeconds / 60
        Windows       = $windows.Count
        Missed        = $windows.Count - $windowHit.Count
        True          = $hits.Count
        False         = $falseCount
        Unlabelled    = @($episodes | Where-Object verdict -eq 'unlabelled').Count
        OnsetToDetect = @($hits | ForEach-Object { [double]$_.onset_to_detect_ms })
        LabelToDetect = $labelledDelays
        DetectTo10dB  = @($hits | Where-Object { $null -ne $_.detect_to_10db_ms } | ForEach-Object { [double]$_.detect_to_10db_ms })
        Overshoot     = @($hits | ForEach-Object { [double]$_.overshoot_db })
        DetectLevel   = @($hits | ForEach-Object { [double]$_.detect_level_db })
        Emergency     = $result.emergency
        Levels        = $result.levels
    }
}

$allEpisodes | Export-Csv -NoTypeInformation -Path (Join-Path $OutDir 'episodes.csv')

# Report.
$md = New-Object System.Collections.Generic.List[string]
$md.Add('# Phase 5 measurements')
$md.Add('')
$md.Add("Generated $(Get-Date -Format 'yyyy-MM-dd HH:mm') from ``$Manifest`` with ``$Analyzer``.")
$md.Add('Times are median / max. "Onset" is when the detector first tracked the peak; "labelled" is from')
$md.Add('the start of the feedback window in the manifest. Detection to 10 dB drop is only measured for')
$md.Add('sessions recorded with the plug-in on (the notches only act on feedback in a live loop).')
$md.Add('')
$md.Add('## Sessions')
$md.Add('')
$md.Add('| File | Plug-in | Buffer | Position | Detections (true / false) | Missed windows | Onset to detect (ms) | Labelled to detect (ms) | Level at detect (dBFS) | Overshoot (dB) | Detect to -10 dB (ms) | Emergency (times, s) |')
$md.Add('| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |')
foreach ($r in $rows) {
    $detections = "$($r.True) / $($r.False)"
    if ($r.Unlabelled) { $detections += " (+$($r.Unlabelled) unlabelled)" }
    $missed = if ($r.Windows) { "$($r.Missed) of $($r.Windows)" } else { '-' }
    $emergency = "$($r.Emergency.engagements), $(Format-Number $r.Emergency.seconds '0.0')"
    $md.Add("| $($r.File) | $($r.Plugin) | $($r.Buffer) | $($r.Position) | $detections | $missed | $(Format-Stat $r.OnsetToDetect) | $(Format-Stat $r.LabelToDetect) | $(Format-Stat $r.DetectLevel '0.0') | $(Format-Stat $r.Overshoot '0.0') | $(Format-Stat $r.DetectTo10dB) | $emergency |")
}

$md.Add('')
$md.Add('## Overall')
$md.Add('')
$md.Add('| Group | Sessions | True detections | Missed windows | False per minute (vocal + mixed takes) | Onset to detect (ms) | Overshoot (dB) | Detect to -10 dB (ms) |')
$md.Add('| --- | --- | --- | --- | --- | --- | --- | --- |')
$groups = @()
foreach ($p in @('on', 'bypassed', 'none')) { $groups += ,@("plug-in $p", @($rows | Where-Object Plugin -eq $p)) }
foreach ($b in ($rows | Select-Object -ExpandProperty Buffer -Unique | Sort-Object)) { $groups += ,@("buffer $b", @($rows | Where-Object Buffer -eq $b)) }
foreach ($g in $groups) {
    $set = $g[1]
    if ($set.Count -eq 0) { continue }
    $vocalMinutes = ($set | Where-Object Kind -ne 'feedback' | Measure-Object -Property Minutes -Sum).Sum
    $falseCount = ($set | Measure-Object -Property False -Sum).Sum
    $falseRate = if ($vocalMinutes -gt 0) { Format-Number ($falseCount / $vocalMinutes) '0.00' } else { '-' }
    $md.Add("| $($g[0]) | $($set.Count) | $(($set | Measure-Object -Property True -Sum).Sum) | $(($set | Measure-Object -Property Missed -Sum).Sum) of $(($set | Measure-Object -Property Windows -Sum).Sum) | $falseRate | $(Format-Stat ($set | ForEach-Object { $_.OnsetToDetect })) | $(Format-Stat ($set | ForEach-Object { $_.Overshoot }) '0.0') | $(Format-Stat ($set | ForEach-Object { $_.DetectTo10dB })) |")
}

$md.Add('')
$md.Add('## Maximum stable gain')
$md.Add('')
$gains = @()
if (Test-Path $StableGain) { $gains = @(Import-Csv $StableGain) }
if ($gains.Count -eq 0) {
    $md.Add("No entries in ``$StableGain`` yet.")
}
else {
    $md.Add('| Condition | Buffer | Bypassed (dB) | Enabled (dB) | Improvement (dB) | Notes |')
    $md.Add('| --- | --- | --- | --- | --- | --- |')
    $improvements = @()
    foreach ($g in $gains) {
        $off = [double]::Parse($g.bypassed_gain_db, [Globalization.CultureInfo]::InvariantCulture)
        $on = [double]::Parse($g.enabled_gain_db, [Globalization.CultureInfo]::InvariantCulture)
        $improvements += $on - $off
        $md.Add("| $($g.condition) | $($g.buffer) | $(Format-Number $off '0.0') | $(Format-Number $on '0.0') | $(Format-Number ($on - $off) '+0.0;-0.0') | $($g.notes) |")
    }
    $md.Add('')
    $md.Add("Improvement median / min: $(Format-Number (Get-Median $improvements) '+0.0;-0.0') / $(Format-Number (($improvements | Measure-Object -Minimum).Minimum) '+0.0;-0.0') dB")
}

$md.Add('')
$md.Add('## CPU (offline benchmark, this machine)')
$md.Add('')
$perf = Join-Path $BinDir 'realtime_performance.exe'
if (Test-Path $perf) {
    $md.Add('```')
    & $perf | Where-Object { $_ -match 'real time' } | ForEach-Object { $md.Add($_.Trim()) }
    $md.Add('```')
}
else {
    $md.Add('realtime_performance.exe not found.')
}

$report = Join-Path $OutDir 'report.md'
$md | Set-Content -Path $report -Encoding UTF8
Write-Host "`nReport:   $report" -ForegroundColor Green
Write-Host "Episodes: $(Join-Path $OutDir 'episodes.csv')" -ForegroundColor Green
