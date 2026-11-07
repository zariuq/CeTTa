; The declared CeTTa literal profile keeps unknown spellings and decodes
; recognized numeric escapes. repr/sread use the same value interpretation.
!(== "a\q" "a\\q")
!(== (sread (repr "a\q")) "a\\q")
!(== "\x41" "A")
!(== "\u03bb" "λ")
!(== "\u{1f600}" "😀")
!(== "\u{0001F600}" "😀")
!(== "\u{0000110000}" "\\u{0000110000}")
!(== "\uD83D\uDE00" "😀")
!(== "\uD800" "\\uD800")
!(== "\x4" "\\x4")
!(== "\b\f" "\x08\x0c")
!(== "\n" "\\n")
