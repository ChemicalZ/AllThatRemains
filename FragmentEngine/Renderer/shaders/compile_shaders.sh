#!/usr/bin/env bash
# Compile GLSL shaders to SPIR-V.
# Usage: ./compile_shaders.sh [--clear | -c]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHADER_EXTS=(vert frag comp geom tesc tese rgen rchit rmiss rint rahit rcall mesh task)
TARGET_ENV="vulkan1.3"
SHOULD_CLEAR=false

# ── Parse arguments ───────────────────────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --clear|-c) SHOULD_CLEAR=true ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

# ── Verify glslc is on PATH ───────────────────────────────────────────────────
if ! command -v glslc &>/dev/null; then
    echo "error: glslc not found. Install the Vulkan SDK and add its bin folder to PATH." >&2
    exit 1
fi

# ── --clear: remove all .spv files ───────────────────────────────────────────
if $SHOULD_CLEAR; then
    mapfile -t spv_files < <(find "$SCRIPT_DIR" -maxdepth 1 -name "*.spv" -type f)
    if [ ${#spv_files[@]} -gt 0 ]; then
        echo "Clearing ${#spv_files[@]} .spv file(s)..."
        rm -f "${spv_files[@]}"
    else
        echo "No .spv files to clear."
    fi
fi

# ── BOM stripping ─────────────────────────────────────────────────────────────
BOM_EXTS=(vert frag comp geom tesc tese rgen rchit rmiss rint rahit rcall mesh task glsl inc hlsl)

strip_bom() {
    local file="$1"
    local bom
    bom="$(od -A n -t x1 -N 3 "$file" | tr -d ' \n')"
    if [ "$bom" = "efbbbf" ]; then
        local tmp
        tmp="$(mktemp)"
        tail -c +4 "$file" > "$tmp" && mv "$tmp" "$file"
        return 0
    fi
    return 1
}

echo "Checking for UTF-8 BOMs..."
for ext in "${BOM_EXTS[@]}"; do
    for file in "$SCRIPT_DIR"/*."$ext"; do
        [ -f "$file" ] || continue
        if strip_bom "$file"; then
            printf "  BOM stripped: %s\n" "$(basename "$file")"
        fi
    done
done

# ── Compile ───────────────────────────────────────────────────────────────────
compiled=0
skipped=0
failed=0

for ext in "${SHADER_EXTS[@]}"; do
    for src in "$SCRIPT_DIR"/*."$ext"; do
        [ -f "$src" ] || continue
        dst="${src}.spv"
        name="$(basename "$src")"

        if [ -f "$dst" ] && ! $SHOULD_CLEAR; then
            printf "  SKIP     %s\n" "$name"
            skipped=$((skipped + 1))
            continue
        fi

        printf "  COMPILE  %s\n" "$name"
        if glslc "--target-env=$TARGET_ENV" -I "$SCRIPT_DIR" "$src" -o "$dst"; then
            printf "           -> OK\n"
            compiled=$((compiled + 1))
        else
            printf "           -> FAILED\n"
            failed=$((failed + 1))
        fi
    done
done

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "Compiled: $compiled   Skipped: $skipped   Failed: $failed"
[ "$failed" -eq 0 ] || exit 1
