# Count atoms and expression nodes in normalized POC source. This is a size
# cross-check; source admission and reconstruction belong to the native codec.
function finish_atom() {
    if (atom) { nodes++; atom = 0 }
}
{
    for (i = 1; i <= length($0); i++) {
        c = substr($0, i, 1)
        if (quoted) {
            if (escaped) escaped = 0
            else if (c == "\\") escaped = 1
            else if (c == "\"") { quoted = 0; nodes++ }
        } else if (c == ";") {
            finish_atom()
            break
        } else if (c == "\"") {
            finish_atom()
            quoted = 1
        } else if (c == "(") {
            finish_atom()
            nodes++
            depth++
        } else if (c == ")") {
            finish_atom()
            depth--
            if (depth < 0) invalid = 1
        } else if (c ~ /[[:space:]]/) finish_atom()
        else atom = 1
    }
    if (!quoted) finish_atom()
}
END {
    finish_atom()
    if (quoted || escaped || depth != 0 || invalid) {
        print "invalid normalized source" > "/dev/stderr"
        exit 2
    }
    print nodes + 0
}
