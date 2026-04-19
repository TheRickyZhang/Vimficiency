#!/usr/bin/env bash
# Post-process doc/vimficiency.txt after panvimdoc runs. Fixes four
# upstream limitations / design choices (panvimdoc v4.0.1):
#
#   (1) `*vimficiency.txt*` header has no `*vimficiency*` alias, so
#       `:help vimficiency` doesn't resolve to this file without one.
#
#   (2) panvimdoc's slug generator lowercases the heading and turns
#       whitespace into dashes but keeps all other characters, so `?`,
#       `/`, `(`, `)`, `:`, `.`, pandoc's smart-quote substitution,
#       `<Plug>`, `C++`, etc. all leak into both `|tag|` references and
#       `*tag*` anchors. Vim help tags should be `[A-Za-z0-9_-]` only.
#
#   (3) panvimdoc pads heading-and-tag pairs with
#       `string.rep(" ", 78 - used)` in `scripts/panvimdoc.lua`
#       upstream. Lua's `string.rep` with a non-positive count returns
#       empty, so long entries render with text jammed against the tag.
#
#   (4) Every H2 section anchor becomes a top-level `:help` tag. This
#       clutters `<Tab>` completion with ~60 deep tags for a 15-chapter
#       file. We drop H2 anchors from the body (keeping the heading
#       text as a visual separator) and drop their matching TOC
#       level-2 entries. Users can still `:help vimficiency-<chapter>`
#       and scroll/search within.
#
# panvimdoc is pinned at v4.0.1 in CI so local and remote builds agree,
# which means we don't patch upstream — corrections live here. (2) and
# (4) rewrite both `|...|` and `*...*` forms with identical logic so
# cross-references stay in sync.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
target="$repo_root/doc/vimficiency.txt"

if [[ ! -f "$target" ]]; then
  echo "error: $target not found (did panvimdoc run?)" >&2
  exit 1
fi

# (1) Header alias. Idempotent — guard checks for the post-condition
# before inserting. Pattern matches the line *start* only (no `$`
# anchor): panvimdoc's actual header is `*vimficiency.txt* <padding>
# For Neovim >= 0.9  Last change: ...`, which the previous
# `^\*vimficiency\.txt\*$` pattern silently failed to match.
if ! grep -q '^\*vimficiency\.txt\*  \*vimficiency\*' "$target"; then
  sed -i '1s|^\*vimficiency\.txt\*|*vimficiency.txt*  *vimficiency*|' "$target"
fi

# (2) + (3) + (4) in a single perl pass with state. Line-by-line with
# `-ne` (no auto-print) so we can skip dropped lines cleanly; `BEGIN`
# initializes a one-line lookback ($prev_rule) and a TOC-region flag
# ($in_toc) needed to distinguish H1 chapter anchors from H2 section
# anchors.
perl -CSD -i -ne '
  BEGIN { $prev_rule = 0; $in_toc = 0; }
  my $line = $_;
  my $cur_is_rule = ($line =~ /^=+$/);

  # --- (2) Sanitize tag slugs ------------------------------------------
  # Match only `vimficiency-...` forms (with a hyphen). This deliberately
  # skips the bare `*vimficiency*` / `*vimficiency.txt*` header tags.
  sub sanitize {
    my ($s) = @_;
    $s =~ s/[^A-Za-z0-9_-]+/-/g;
    $s =~ s/-+/-/g;
    $s =~ s/-+$//;
    return $s;
  }
  $line =~ s{\|(vimficiency-[^|]+)\|}{ "|" . sanitize($1) . "|" }ge;
  $line =~ s{\*(vimficiency-[^*]+)\*}{ "*" . sanitize($1) . "*" }ge;

  # --- TOC region boundaries ------------------------------------------
  # TOC starts at the "Table of Contents" line and ends at the next
  # `===` rule. Needed for (4) to distinguish level-2 TOC entries from
  # (nonexistent) body refs.
  if ($line =~ /^Table of Contents\b/) { $in_toc = 1; }
  elsif ($cur_is_rule && $in_toc)      { $in_toc = 0; }

  # --- (4a) Drop level-2 TOC entries ----------------------------------
  if ($in_toc && $line =~ /^  - .*\|vimficiency-[^|]+\|/) {
    $prev_rule = $cur_is_rule;
    next;  # no print
  }

  # --- (4b) Strip H2 body anchors -------------------------------------
  # H1 chapter anchors are immediately preceded by a `===` rule line.
  # H2 section anchors are preceded by a blank line or body text. So
  # `$prev_rule` as a one-line lookback is the whole discriminator.
  # When we strip, we keep the heading text (it still serves as a
  # visual section separator in the file).
  #
  # `\s*` (not `\s+`) between heading and anchor: panvimdoc jams long
  # headings directly against the tag (the same `string.rep(" ", 78 -
  # used)` bug as the TOC padding one, at another callsite). The
  # re-pad block below would happily insert whitespace — but that runs
  # AFTER this one, so we need to tolerate zero-whitespace here too,
  # otherwise strip misses every jammed anchor.
  if (!$in_toc && !$prev_rule
      && $line =~ /^([^=\n].*?)\s*(\*vimficiency-[A-Za-z0-9_-]+\*)\s*$/) {
    $line = "$1\n";
  }

  # --- (3) Re-pad whatever heading-and-tag pairs survived -------------
  # Runs after the strip so H2 anchors that were dropped do not get
  # re-padded (the pattern no longer matches). H1 chapter anchors +
  # level-1 TOC entries go through here and get column-78 alignment.
  if ($line =~ /^(\S[^|]*?)(\|vimficiency-[^|]+\|)\s*$/) {
    my ($text, $tag) = ($1, $2);
    $text =~ s/\s+$//;
    my $used = length($text) + length($tag);
    my $pad  = $used >= 78 ? " " : " " x (78 - $used);
    $line = "$text$pad$tag\n";
  } elsif ($line =~ /^(\S[^*]*?)(\*vimficiency-[^*]+\*)\s*$/) {
    my ($text, $anchor) = ($1, $2);
    $text =~ s/\s+$//;
    my $used = length($text) + length($anchor);
    my $pad  = $used >= 78 ? " " : " " x (78 - $used);
    $line = "$text$pad$anchor\n";
  }

  print $line;
  $prev_rule = $cur_is_rule;
' "$target"

# Sanity check: every `|vimficiency-xxx|` reference should have a
# matching `*vimficiency-xxx*` anchor. Mismatches indicate the drop-H2
# pass removed an anchor that was still referenced by a TOC entry,
# which means (4a) and (4b) fell out of sync.
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
