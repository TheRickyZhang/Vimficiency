#!/usr/bin/env bash
# Post-process doc/vimficiency.txt after panvimdoc runs. Fixes three
# upstream limitations (panvimdoc v4.0.1):
#
#   1. `*vimficiency.txt*` header has no `*vimficiency*` alias, so
#      `:help vimficiency` doesn't resolve to this file without one.
#   2. panvimdoc's slug generator lowercases the heading and turns
#      whitespace into dashes, but keeps all other characters — so
#      question marks, slashes, parens, colons, quotes (including
#      pandoc's smart-quote substitution), `<Plug>`, `C++`, etc. all
#      leak into both TOC `|tag|` references and `*tag*` anchors. Vim
#      help tags should be `[A-Za-z0-9_-]` only.
#   3. panvimdoc pads TOC lines with `string.rep(" ", 74 - #text - #tag)`
#      (see `scripts/panvimdoc.lua` upstream). Lua's `string.rep` with
#      a non-positive count returns empty, so long entries get no space
#      before `|tag|` and render as a single run of punctuation.
#
# Both sanitization (2) and padding (3) must run on this side: we pin
# panvimdoc at v4.0.1 in CI so the local and remote builds agree, which
# means we don't patch upstream. Sanitization rewrites `|...|` and
# `*...*` forms with the same function so cross-references stay in sync.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
target="$repo_root/doc/vimficiency.txt"

if [[ ! -f "$target" ]]; then
  echo "error: $target not found (did panvimdoc run?)" >&2
  exit 1
fi

# (1) Header alias. Idempotent — only inserts if not already there.
if ! grep -q '^\*vimficiency\.txt\*  \*vimficiency\*' "$target"; then
  sed -i '1s|^\*vimficiency\.txt\*$|*vimficiency.txt*  *vimficiency*|' "$target"
fi

# (2) + (3) in a single perl pass. Order matters: sanitize tags first so
# the padding pass measures post-sanitization lengths.
perl -CSD -i -pe '
  # --- (2) Sanitize tag slugs -------------------------------------------
  # Match only `vimficiency-...` forms (with a hyphen). This deliberately
  # skips the bare `*vimficiency*` / `*vimficiency.txt*` header tags.
  # Interior: anything except the delimiter itself — `[^|]+` / `[^*]+` —
  # because the pre-sanitization slug can contain punctuation, smart
  # quotes, etc.
  sub sanitize {
    my ($s) = @_;
    $s =~ s/[^A-Za-z0-9_-]+/-/g;
    $s =~ s/-+/-/g;
    $s =~ s/-+$//;
    return $s;
  }
  s{\|(vimficiency-[^|]+)\|}{ "|" . sanitize($1) . "|" }ge;
  s{\*(vimficiency-[^*]+)\*}{ "*" . sanitize($1) . "*" }ge;

  # --- (3) Padding around trailing tags --------------------------------
  # panvimdoc pads heading-and-tag pairs with `string.rep(" ", 78 - used)`
  # in three places: level-1 TOC entries, level-2 TOC entries, and H2
  # section anchors in the body. All three collapse to zero padding when
  # `used >= 78`, jamming text against the tag. The three arms below
  # rebuild each line with at least one space.
  if (/^( {2,}- )([^|]+?)(\|vimficiency-[^|]+\|)\s*$/) {
    # Level-2 TOC entry: `  - <text><tag>`.
    my ($indent, $text, $tag) = ($1, $2, $3);
    $text =~ s/\s+$//;
    my $used = length($indent) + length($text) + length($tag);
    my $pad  = $used >= 78 ? " " : " " x (78 - $used);
    $_ = "$indent$text$pad$tag\n";
  } elsif (/^(\S[^|]*?)(\|vimficiency-[^|]+\|)\s*$/) {
    # Level-1 TOC entry: `<chapter-title><tag>` (no leading `  - `).
    my ($text, $tag) = ($1, $2);
    $text =~ s/\s+$//;
    my $used = length($text) + length($tag);
    my $pad  = $used >= 78 ? " " : " " x (78 - $used);
    $_ = "$text$pad$tag\n";
  } elsif (/^(\S[^*]*?)(\*vimficiency-[^*]+\*)\s*$/) {
    # H2 section anchor in the body: `HEADING<anchor>` (uppercase text).
    # Skip the level-1 chapter anchor line, which is `\d+. ...` and uses
    # a different padding convention already handled by panvimdoc.
    my ($text, $anchor) = ($1, $2);
    $text =~ s/\s+$//;
    my $used = length($text) + length($anchor);
    my $pad  = $used >= 78 ? " " : " " x (78 - $used);
    $_ = "$text$pad$anchor\n";
  }
' "$target"

# Sanity check: every `|vimficiency-xxx|` reference should have a
# matching `*vimficiency-xxx*` anchor. Mismatches indicate the
# sanitization diverged between the two forms (should not happen with
# `sub sanitize` applied identically, but cheap to guard).
missing=$(
  awk '
    match($0, /\|vimficiency-[A-Za-z0-9_-]+\|/) {
      tag = substr($0, RSTART + 1, RLENGTH - 2); refs[tag] = 1
    }
    match($0, /\*vimficiency-[A-Za-z0-9_-]+\*/) {
      tag = substr($0, RSTART + 1, RLENGTH - 2); anchors[tag] = 1
    }
    END { for (t in refs) if (!(t in anchors)) print t }
  ' "$target"
)
if [[ -n "$missing" ]]; then
  echo "warning: tag references without matching anchors:" >&2
  echo "$missing" >&2
fi

echo "Post-processed $target"
