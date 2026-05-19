<#
.SYNOPSIS
    Compile GLSL shaders to SPIR-V.

.PARAMETER Clear
    Delete all existing .spv files then recompile everything.
#>
param(
    [Alias("c")]
    [switch]$Clear
)

$SCRIPT_DIR  = Split-Path -Parent $MyInvocation.MyCommand.Path
$SHADER_EXTS = @("vert","frag","comp","geom","tesc","tese","rgen","rchit","rmiss","rint","rahit","rcall","mesh","task")
$TARGET_ENV  = "vulkan1.3"

# ── Verify glslc is on PATH ───────────────────────────────────────────────────
if (-not (Get-Command "glslc" -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: glslc not found. Install the Vulkan SDK and add its bin folder to PATH." -ForegroundColor Red
    exit 1
}

# ── --clear: remove all .spv files ───────────────────────────────────────────
if ($Clear) {
    $spvFiles = Get-ChildItem -Path $SCRIPT_DIR -Filter "*.spv" -File
    if ($spvFiles.Count -gt 0) {
        Write-Host "Clearing $($spvFiles.Count) .spv file(s)..."
        $spvFiles | Remove-Item -Force
    } else {
        Write-Host "No .spv files to clear."
    }
}

# ── BOM stripping ─────────────────────────────────────────────────────────────
$BOM_EXTS = @("vert","frag","comp","geom","tesc","tese","rgen","rchit","rmiss","rint","rahit","rcall","mesh","task","glsl","inc","hlsl")

function Remove-BOM([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        [System.IO.File]::WriteAllBytes($Path, $bytes[3..($bytes.Length - 1)])
        return $true
    }
    return $false
}

Write-Host "Checking for UTF-8 BOMs..."
foreach ($ext in $BOM_EXTS) {
    foreach ($file in Get-ChildItem -Path $SCRIPT_DIR -Filter "*.$ext" -File) {
        if (Remove-BOM $file.FullName) {
            Write-Host "  BOM stripped: $($file.Name)" -ForegroundColor Yellow
        }
    }
}

# ── Compile ───────────────────────────────────────────────────────────────────
$compiled = 0
$skipped  = 0
$failed   = 0

foreach ($ext in $SHADER_EXTS) {
    foreach ($src in Get-ChildItem -Path $SCRIPT_DIR -Filter "*.$ext" -File) {
        $dst  = "$($src.FullName).spv"
        $name = $src.Name

        if ((Test-Path $dst) -and -not $Clear) {
            Write-Host "  SKIP     $name"
            $skipped++
            continue
        }

        Write-Host "  COMPILE  $name"
        & glslc "--target-env=$TARGET_ENV" -I "$SCRIPT_DIR" $src.FullName -o $dst
        if ($LASTEXITCODE -ne 0) {
            Write-Host "           -> FAILED" -ForegroundColor Red
            $failed++
        } else {
            Write-Host "           -> OK" -ForegroundColor Green
            $compiled++
        }
    }
}

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "Compiled: $compiled   Skipped: $skipped   Failed: $failed"
if ($failed -gt 0) { exit 1 }
