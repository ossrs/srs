#!/bin/bash
#
# Check that a change touches only comments in C++, Go and JavaScript files.
#
# Usage:
#   bash skills/srs-develop/scripts/comment-only-check.sh <repo-dir> [<commit>]
#
#   repo-dir    The git checkout to check, such as ~/projects/srs-comment-audit.
#   commit      Check this commit against its parent. Default: the working tree against HEAD.
#
# For each changed .hpp, .cpp, .h, .c, .go or .js file, it fails when:
#   - the code differs once comments are removed and whitespace outside strings is squashed;
#   - a code line changes its indentation or spacing, or two code lines are joined or split, once comments and
#     blank lines are removed (for Go, only the indentation and line breaks, since gofmt realigns the spacing);
#   - a line with SPDX, Copyright or clang-format is added or removed, or the count of TODO and FIXME lines
#     changes (misspelled TDOO and FIMXE count as markers, so fixing the typo passes; wording changes inside
#     such a comment are fine);
#   - a tool directive comment changes, such as //go:build, // +build, //go:generate, //nolint, the
#     "Code generated" header, or an eslint comment;
#   - a URL in the file is gone.
# Any other changed file fails too, so a comment task commits only source files.
#
# Exit code is 0 when every file passes.

DIR=${1:?Usage: comment-only-check.sh <repo-dir> [<commit>]}
COMMIT=$2
cd "$DIR" || exit 1

if [[ -n $COMMIT ]]; then
    OLD="$COMMIT^"; NEW="$COMMIT"
    FILES=$(git diff --name-only "$OLD" "$NEW")
else
    OLD=HEAD; NEW=
    FILES=$(git diff --name-only HEAD; git ls-files --others --exclude-standard)
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

FAIL=0
for f in $(echo "$FILES" | LC_ALL=C sort -u); do
    case $f in
        *.hpp|*.cpp|*.h|*.c|*.go|*.js) ;;
        *) echo "FAIL $f: not a C++, Go or JavaScript source file"; FAIL=1; continue ;;
    esac
    git show "$OLD:$f" > "$TMP/old" 2>/dev/null || { echo "FAIL $f: new file"; FAIL=1; continue; }
    if [[ -n $NEW ]]; then
        git show "$NEW:$f" > "$TMP/new" 2>/dev/null || { echo "FAIL $f: deleted"; FAIL=1; continue; }
    else
        [[ -f $f ]] || { echo "FAIL $f: deleted"; FAIL=1; continue; }
        cp "$f" "$TMP/new"
    fi
    python3 -I - "$f" "$TMP/old" "$TMP/new" <<'EOF' || FAIL=1
import re, sys

name, old_path, new_path = sys.argv[1:4]
lang = 'go' if name.endswith('.go') else 'js' if name.endswith('.js') else 'cpp'

def strip(text, squash=True):
    """Remove comments, keep strings verbatim, squash whitespace outside strings to one space.
    With squash=False, keep the whitespace as it is, so the layout of each code line stays."""
    if lang == 'js':
        # A JSX comment {/* ... */} leaves empty braces behind; drop the whole expression.
        text = re.sub(r'\{\s*/\*.*?\*/\s*\}', ' ', text, flags=re.S)
    out, i, n = [], 0, len(text)
    space = False
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            space = True
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            space = True
            continue
        if c.isspace():
            space = True
            if not squash:
                out.append(c)
            i += 1
            continue
        if squash and space and out:
            out.append(' ')
        space = False
        if lang == 'cpp' and c == 'R' and text.startswith('"', i + 1):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if m:
                end = text.find(')' + m.group(1) + '"', i)
                end = n if end < 0 else end + len(m.group(1)) + 2
                out.append(text[i:end]); i = end
                continue
        if c in '"\'' or (c == '`' and lang in ('go', 'js')):
            j = i + 1
            while j < n and text[j] != c:
                # A Go raw string has no escapes.
                if text[j] == '\\' and not (c == '`' and lang == 'go'):
                    j += 1
                elif text[j] == '\n' and c != '`':
                    break
                j += 1
            out.append(text[i:j + 1]); i = j + 1
            continue
        out.append(c); i += 1
    return ''.join(out)

old = open(old_path, encoding='utf-8', errors='replace').read()
new = open(new_path, encoding='utf-8', errors='replace').read()
ok = True

if strip(old) != strip(new):
    a, b = strip(old), strip(new)
    k = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
    print(f'FAIL {name}: code changed near: {a[max(0, k - 60):k + 60]!r} -> {b[max(0, k - 60):k + 60]!r}')
    ok = False

def code_lines(text):
    lines = [line.rstrip() for line in strip(text, squash=False).splitlines() if line.strip()]
    if lang == 'go':
        # gofmt realigns struct fields and const blocks when a comment line between them is removed, so only the
        # indentation and line breaks are compared; gofmt -l checks the spacing.
        lines = [re.match(r'\s*', line).group(0) + re.sub(r'\s+', ' ', line.strip()) for line in lines]
    return lines

if ok and code_lines(old) != code_lines(new):
    a, b = code_lines(old), code_lines(new)
    k = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
    print(f'FAIL {name}: code line layout changed: {a[k] if k < len(a) else None!r} -> {b[k] if k < len(b) else None!r}')
    ok = False

for word in ('SPDX', 'Copyright', 'clang-format'):
    a = sum(1 for line in old.splitlines() if word in line)
    b = sum(1 for line in new.splitlines() if word in line)
    if a != b:
        print(f'FAIL {name}: {word} lines {a} -> {b}')
        ok = False

# Comments the tools read are code: Go build and generate directives, lint switches, generated-file headers.
# Each must stay byte for byte.
directive = re.compile(r'^\s*//(go:|\s*\+build|\s*nolint|\s*Code generated|\s*eslint)|/\*\s*eslint|//\s*@ts-')
a = [line for line in old.splitlines() if directive.search(line)]
b = [line for line in new.splitlines() if directive.search(line)]
if a != b:
    print(f'FAIL {name}: tool directive comments changed: {len(a)} -> {len(b)} lines')
    ok = False

# TODO and FIXME lines are counted together with their misspellings, so fixing a typo such as TDOO or FIMXE
# passes, while deleting a marker line fails.
marker = re.compile(r'TODO|FIXME|TDOO|FIMXE')
a = sum(1 for line in old.splitlines() if marker.search(line))
b = sum(1 for line in new.splitlines() if marker.search(line))
if a != b:
    print(f'FAIL {name}: TODO/FIXME lines {a} -> {b}')
    ok = False

url = re.compile(r'https?://[^\s"\'<>)]+')
gone = sorted(set(url.findall(old)) - set(url.findall(new)))
for u in gone:
    print(f'FAIL {name}: URL removed: {u}')
    ok = False

if ok:
    print(f'OK {name}')
sys.exit(0 if ok else 1)
EOF
done

exit $FAIL
