#!/bin/bash
#
# submodule_restore_dryrun.sh
#
# READ ONLY. Makes no changes to the repository, ever.
#
# Background: this repo was created by flattening a NOS3 checkout (root commit
# "first commit", 2025-10-17). That converted all 46 git submodules into plain
# tracked files. .gitmodules survived verbatim but no gitlinks exist, so there
# is no link to upstream and no way to pull upstream fixes.
#
# This script answers one question, per module, before anyone attempts to
# convert those directories back into real submodules:
#
#     "If we replaced this directory with a submodule pinned at an upstream
#      commit, would we lose any of our own work?"
#
# It does that by checking three things:
#   1. Can we find an upstream commit whose source matches our copy exactly?
#      If yes, our copy is unmodified upstream code and is safe to replace.
#   2. Did any of our own commits touch files inside this directory?
#      If yes, replacing it would discard that work.
#   3. Does our code reference this module at all (build applist, startup
#      script, sim config)? An unreferenced module is safe regardless.
#
# Binary files are excluded from the source comparison on purpose. The flatten
# ran CRLF normalization over binaries and corrupted 71 of them (docs and
# images only, no functional files). Those differences are real but are damage
# we WANT the restore to repair, so they must not mask the source comparison.
#
# Usage:
#   scripts/dev/submodule_restore_dryrun.sh [path-to-reference-nos3-clone]
#
# The reference clone must be a normal clone of nasa/nos3 with submodules
# initialized:
#   git clone --recursive https://github.com/nasa/nos3.git ~/projects/nasa-nos3
#

set -u

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REF="${1:-$HOME/projects/nasa-nos3}"

# How far back to search each submodule's history for a matching commit.
HISTORY_DEPTH=150

# Binary types excluded from source comparison (see header).
BIN_EXCLUDES=(-x '*.png' -x '*.jpg' -x '*.jpeg' -x '*.gif' -x '*.icns'
              -x '*.pdf' -x '*.doc' -x '*.docx' -x '*.xls' -x '*.xlsx'
              -x '*.ppt' -x '*.pptx' -x '*.zip' -x '*.tgz' -x '*.gz'
              -x '*.a' -x '*.so' -x '*.o' -x '*.bin' -x '*.dat' -x '.git')

RED=$'\033[0;31m'; YEL=$'\033[0;33m'; GRN=$'\033[0;32m'; DIM=$'\033[2m'; RST=$'\033[0m'

cd "$BASE_DIR" || exit 1

if [ ! -d "$REF/.git" ]; then
    echo "${RED}ERROR${RST}: reference clone not found at $REF"
    echo "  Clone it with:  git clone --recursive https://github.com/nasa/nos3.git $REF"
    exit 1
fi

# The root commit is the flatten. Anything changed after it is our own work.
FIRST_COMMIT="$(git rev-list --max-parents=0 HEAD | tail -1)"

echo "=============================================================================="
echo " SUBMODULE RESTORE - DRY RUN (no changes will be made)"
echo "=============================================================================="
echo " repo:            $BASE_DIR"
echo " reference:       $REF"
echo " flatten commit:  $FIRST_COMMIT ($(git log -1 --format=%ad --date=short "$FIRST_COMMIT"))"
echo " our commits:     $(( $(git rev-list --count HEAD) - 1 )) since the flatten"
echo

# Files our own commits changed, used to detect work that would be lost.
OURS="$(mktemp)"; trap 'rm -f "$OURS"' EXIT
git diff --name-only "$FIRST_COMMIT" HEAD > "$OURS"

safe=0; review=0; blocked=0; absent=0

printf "%-38s %-14s %-12s %s\n" "MODULE" "PIN" "OUR EDITS" "VERDICT"
printf "%-38s %-14s %-12s %s\n" "$(printf '%.0s-' {1..38})" "$(printf '%.0s-' {1..14})" "$(printf '%.0s-' {1..12})" "-------"

while read -r path; do
    [ -n "$path" ] || continue

    # --- 3. is this module referenced by our build at all? -------------------
    name="$(basename "$path")"
    refs=$(grep -rl --include=targets.cmake --include='*.scr' --include='*.xml' \
             -w "$name" cfg/ 2>/dev/null | grep -v '^cfg/build/' | wc -l)

    # --- module missing entirely -------------------------------------------
    if [ ! -d "$path" ]; then
        printf "%-38s %-14s %-12s %s\n" "$path" "-" "-" \
            "${YEL}ABSENT${RST} (declared, not present; restore adds it; refs=$refs)"
        absent=$((absent+1)); continue
    fi

    if [ ! -d "$REF/$path" ]; then
        printf "%-38s %-14s %-12s %s\n" "$path" "-" "-" "${RED}NO REFERENCE${RST}"
        blocked=$((blocked+1)); continue
    fi

    # --- 2. did our commits touch anything inside? --------------------------
    touched=$(grep -c "^$path/" "$OURS")
    touched_src=$(grep "^$path/" "$OURS" \
        | grep -icE '\.(c|h|cpp|hpp|cmake|py|rb|sh|xml|txt|scr|json|ya?ml)$')

    # --- 1. find an upstream commit whose source matches ours ---------------
    pin=""; pindate=""
    if diff -rq --strip-trailing-cr "${BIN_EXCLUDES[@]}" \
           "$REF/$path" "$path" >/dev/null 2>&1; then
        pin="$(git -C "$REF/$path" rev-parse HEAD 2>/dev/null)"
        pindate="$(git -C "$REF/$path" log -1 --format=%ad --date=short 2>/dev/null)"
    else
        for c in $(git -C "$REF/$path" log --format=%H --all 2>/dev/null | head -$HISTORY_DEPTH); do
            tmp="$(mktemp -d)"
            git -C "$REF/$path" archive "$c" 2>/dev/null | tar -x -C "$tmp" 2>/dev/null
            if diff -rq --strip-trailing-cr "${BIN_EXCLUDES[@]}" "$tmp" "$path" >/dev/null 2>&1; then
                pin="$c"; pindate="$(git -C "$REF/$path" log -1 --format=%ad --date=short "$c")"
                rm -rf "$tmp"; break
            fi
            rm -rf "$tmp"
        done
    fi

    # --- verdict ------------------------------------------------------------
    pinshort="${pin:0:10}"; [ -n "$pin" ] || pinshort="none"
    edits="-"; [ "$touched" -gt 0 ] && edits="$touched ($touched_src src)"

    if [ -z "$pin" ]; then
        verdict="${RED}BLOCKED${RST} no upstream commit matches our source; inspect before converting"
        blocked=$((blocked+1))
    elif [ "$touched_src" -gt 0 ]; then
        verdict="${YEL}REVIEW${RST}  our commits edited source here; extract as patch first"
        review=$((review+1))
    elif [ "$touched" -gt 0 ]; then
        verdict="${GRN}SAFE${RST}    only corrupted binaries differ; restore repairs them"
        safe=$((safe+1))
    else
        verdict="${GRN}SAFE${RST}    pure upstream, untouched by us"
        safe=$((safe+1))
    fi

    printf "%-38s %-14s %-12s %b\n" "$path" "$pinshort" "$edits" "$verdict"
    [ -n "$pindate" ] && printf "%-38s ${DIM}%s${RST}\n" "" "  would pin to $pindate"

done < <(git config -f .gitmodules --get-regexp path | awk '{print $2}')

echo
echo "=============================================================================="
echo " SUMMARY"
echo "=============================================================================="
printf "  %-10s %3d  safe to convert as-is\n"                        "SAFE"    "$safe"
printf "  %-10s %3d  carry a local patch before converting\n"        "REVIEW"  "$review"
printf "  %-10s %3d  source does not match upstream; investigate\n"  "BLOCKED" "$blocked"
printf "  %-10s %3d  declared but missing; restore would add them\n" "ABSENT"  "$absent"
echo
echo "  Nothing above has been changed. This script only reads."
echo
echo "  Next step for REVIEW modules, per module:"
echo "    git diff $FIRST_COMMIT HEAD -- <path>   # see exactly what we changed"
echo "  Then decide: move the change into cfg/ as an override, or fork that"
echo "  one upstream repo and point .gitmodules at the fork."
echo
