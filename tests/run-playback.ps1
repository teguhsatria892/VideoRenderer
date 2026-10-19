param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\_bin\playback-tests'))
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$exe = Join-Path $repo '_bin\ambilight-playback.exe'
$dll = Join-Path $repo '_bin\Filter_x64\MpcVideoRenderer64.ax'
if (!(Test-Path $exe) -or !(Test-Path $dll)) { throw 'Run build-ambilight.cmd first.' }
Get-Command ffmpeg -ErrorAction Stop | Out-Null
$scenarios = @(
    @{ Name='wide'; Size='1280x540' },
    @{ Name='portrait'; Size='540x960' },
    @{ Name='4k'; Size='3840x1600' },
    @{ Name='exact'; Size='960x540' },
    @{ Name='hdr'; Size='640x272' }
)
foreach ($scenario in $scenarios) {
    $clip = Join-Path $output ($scenario.Name + '.mp4')
    $encode = @('-hide_banner','-loglevel','error','-f','lavfi','-i',"testsrc2=size=$($scenario.Size):rate=30",'-t','8')
    if ($scenario.Name -eq 'hdr') {
        $encode += @('-vf','format=yuv420p10le','-c:v','libx265','-preset','ultrafast','-x265-params','log-level=error:hdr10=1:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc')
    } else { $encode += @('-c:v','libx264','-preset','ultrafast','-crf','18','-pix_fmt','yuv420p') }
    & ffmpeg @encode '-y' $clip
    if ($LASTEXITCODE) { throw "Fixture generation failed: $($scenario.Name)" }
    $testArguments = @($dll, $clip, (Join-Path $output $scenario.Name))
    if ($scenario.Name -eq 'hdr') { $testArguments += 'hdr' }
    & $exe @testArguments
    if ($LASTEXITCODE) { throw "Playback test failed: $($scenario.Name)" }
}
