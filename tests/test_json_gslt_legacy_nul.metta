!(import! &self json)

; The compatibility codec cannot silently erase U+0000.
!(json-parse "\"\\u0000\"")
