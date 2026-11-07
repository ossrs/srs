#!/bin/bash
#
# List every class, struct and global function of the SRS C++ media server, for the memory audit.
#
# Usage:
#   bash skills/srs-develop/scripts/cpp-memory-inventory.sh
#
# Scope: trunk/src/{core,kernel,protocol,app,main}. Not utest, not 3rdparty, and not the vendored llhttp parser
# (srs_protocol_http_stack_llhttp{,api,http}.cpp, copied from nodejs/llhttp).
# Skipped: pure interfaces (only pure virtuals and an empty constructor or destructor, no data) and forward
# declarations. Nested classes are their own items. Methods defined out of line belong to their class. Overloads
# and per-platform variants of a function or class in one file are one item.
#
# Output, one item per line, in dependency order (core, kernel, protocol, app, main, then by file and line):
#   kind name file:line lines
# where kind is class, struct or func, and lines is the size of the definition: for a class, its declaration
# plus every out-of-line method, so the size of the audit.

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT" || exit 1

FILES=()
for d in core kernel protocol app main; do
    # A header before its source, so a class comes before the functions of its file.
    for f in $(ls trunk/src/$d | grep -E '\.(hpp|cpp)$' | sed -E 's/\.(hpp|cpp)$/ \1/' | LC_ALL=C sort -k1,1 -k2,2r | awk '{print $1"."$2}'); do
        case $f in
            srs_protocol_http_stack_llhttp.cpp|srs_protocol_http_stack_llhttpapi.cpp|srs_protocol_http_stack_llhttphttp.cpp) continue ;;
            srs_protocol_http_stack_llhttp.hpp|srs_protocol_http_stack_llhttpapi.hpp|srs_protocol_http_stack_llhttphttp.hpp) continue ;;
        esac
        FILES+=("trunk/src/$d/$f")
    done
done

LC_ALL=C awk -v SQ="'" '
function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
function squash(s) { gsub(/[ \t]+/, " ", s); return trim(s) }

# Remove comments, strings and preprocessor lines from one line, keeping block comment state across lines.
function strip(line,    out, i, c, q) {
    if (inpp) {
        inpp = (line ~ /\\$/)
        return ""
    }
    if (!incomment && line ~ /^[ \t]*#/) {
        inpp = (line ~ /\\$/)
        return ""
    }
    out = ""
    while (line != "") {
        if (incomment) {
            i = index(line, "*/")
            if (i == 0) return out
            line = substr(line, i + 2)
            incomment = 0
            out = out " "
            continue
        }
        if (match(line, "//|/[*]|[\"" SQ "]") == 0) return out line
        out = out substr(line, 1, RSTART - 1)
        c = substr(line, RSTART, RLENGTH)
        line = substr(line, RSTART + RLENGTH)
        if (c == "//") return out
        if (c == "/*") { incomment = 1; continue }
        # A string or char literal: skip to its closing quote, honoring escapes.
        q = c
        while (line != "") {
            c = substr(line, 1, 1)
            if (c == "\\") { line = substr(line, 3); continue }
            line = substr(line, 2)
            if (c == q) break
        }
        out = out q q
    }
    return out
}

function is_pure_stmt(t) {
    t = squash(t)
    gsub(/(public|private|protected) *:/, "", t)
    t = trim(t)
    if (t == "") return 1
    if (t ~ /= *0$/) return 1
    if (t ~ /^(virtual +)?~?[A-Za-z_][A-Za-z0-9_]* *\( *\)$/) return 1
    if (t ~ /^(friend|typedef|using)( |$)/) return 1
    return 0
}

# Handle the head text before an opening brace, and push the scope it opens.
function open_scope(h, line,    t, name, pre, base, kind, q, tail) {
    t = squash(h)
    gsub(/^((public|private|protected) *: *)+/, "", t)
    d = ++depth
    stype[d] = "other"; sline[d] = line; spure[d] = 1; sname[d] = ""; shead[d] = headline; sord[d] = ++seq
    # Code inside a function or an initializer never declares an item.
    if (d > 1 && (stype[d - 1] == "func" || stype[d - 1] == "other")) return
    if (d > 1 && stype[d - 1] == "class") spure[d - 1] = 0
    if (t ~ /^(template *<.*> *)?(class|struct) +([A-Z_]+_API +)?[A-Za-z_][A-Za-z0-9_]*( +final)? *(:.*)?$/ && t !~ /^[^:]*\(/) {
        q = t
        kind = (q ~ /^(template *<.*> *)?class /) ? "class" : "struct"
        sub(/^(template *<.*> *)?(class|struct) +([A-Z_]+_API +)?/, "", q)
        match(q, /^[A-Za-z_][A-Za-z0-9_]*/)
        name = substr(q, 1, RLENGTH)
        if (owner() != "") name = owner() "::" name
        stype[d] = "class"; sname[d] = name; skind[d] = kind
        return
    }
    if (t ~ /^(namespace( .*)?|extern "")$/) { stype[d] = "ns"; return }
    if (owner() != "") return
    if (index(t, "(") == 0 || index(t, ")") == 0) return
    tail = t
    sub(/^.*\)/, "", tail)
    if (tail !~ /^ *(const)? *(override|noexcept)? *$/ && t !~ /\) *:/) return
    pre = substr(t, 1, index(t, "(") - 1)
    pre = trim(pre)
    if (match(pre, /([A-Za-z_][A-Za-z0-9_:]*::)?operator *[^ A-Za-z0-9_]+$/)) {
        name = substr(pre, RSTART); pre = substr(pre, 1, RSTART - 1)
    } else if (match(pre, /[~A-Za-z_][A-Za-z0-9_:~]*$/)) {
        name = substr(pre, RSTART); pre = substr(pre, 1, RSTART - 1)
    } else {
        return
    }
    base = name; sub(/^.*::/, "", base)
    if (base ~ /^(if|for|while|switch|return|sizeof|catch|defined|else|do)$/) return
    if (t ~ /^(else|do)( |$)/ || index(pre, "=") > 0) return
    stype[d] = "func"; sname[d] = name
}

function owner(    i) {
    for (i = depth - 1; i >= 1; i--) if (stype[i] == "class") return sname[i]
    return ""
}

function close_scope(line,    d, n, key, cls) {
    d = depth--
    if (d < 1) { depth = 0; return }
    n = line - sline[d] + 1
    if (stype[d] == "class") {
        if (stmt[d] != "" && !is_pure_stmt(stmt[d])) spure[d] = 0
        if (!spure[d]) add(skind[d], sname[d], shead[d], n, sord[d])
    } else if (stype[d] == "func") {
        if (index(sname[d], "::") && sname[d] !~ /^srs_internal::/) {
            cls = sname[d]; sub(/::[^:]*$/, "", cls); sub(/^.*::/, "", cls)
            methods[cls] += n
        } else {
            key = sname[d]; sub(/^srs_internal::/, "", key)
            add("func", key, shead[d], n, sord[d])
        }
    }
    stmt[d] = ""
}

# An item keeps the order of its opening brace, so a class comes before its nested classes.
function add(kind, name, at, n, ord,    key) {
    key = (kind == "func" ? "f" : "c") SUBSEP name SUBSEP FILENAME
    if (key in seen) { size[seen[key]] += n; return }
    seen[key] = ++count
    ikind[count] = kind; iname[count] = name; ifile[count] = FILENAME; iline[count] = at; size[count] = n
    byord[ord] = count
}

FNR == 1 { depth = 0; incomment = 0; inpp = 0; head = ""; headline = 0 }
{
    s = strip($0)
    while (s != "") {
        if (match(s, /[{};]/) == 0) {
            if (headline == 0 && s ~ /[^ \t]/) headline = FNR
            head = head " " s
            break
        }
        piece = substr(s, 1, RSTART - 1)
        c = substr(s, RSTART, 1)
        s = substr(s, RSTART + 1)
        if (headline == 0 && piece ~ /[^ \t]/) headline = FNR
        head = head " " piece
        if (c == "{") {
            open_scope(head, FNR)
        } else if (c == "}") {
            close_scope(FNR)
        } else if (depth > 0 && stype[depth] == "class") {
            if (!is_pure_stmt(head)) spure[depth] = 0
        }
        head = ""; headline = 0
    }
}
END {
    for (o = 1; o <= seq; o++) {
        if (!(o in byord)) continue
        i = byord[o]
        if (ikind[i] != "func") {
            cls = iname[i]; sub(/^.*::/, "", cls)
            size[i] += methods[cls]
        }
        printf "%s %s %s:%d %d\n", ikind[i], iname[i], ifile[i], iline[i], size[i]
    }
}
' "${FILES[@]}"
