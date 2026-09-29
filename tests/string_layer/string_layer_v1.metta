; The string layer: a string is bytes plus a length.  Byte operations take
; any bytes; codepoint operations read strict UTF-8 and refuse malformed text
; at the byte offset of the first invalid sequence.  Every expected value
; below follows from UTF-8 or from an operation's stated domain.

!(import! &self str)

; ------------------------------------------------ bytes and codepoints ----
; é is U+00E9, two bytes; € is U+20AC, three.
!(assertEqual (str:byte-length "é") 2)
!(assertEqual (str:codepoint-count "é") 1)
!(assertEqual (str:byte-length "€") 3)
!(assertEqual (str:codepoint-count "€") 1)
!(assertEqual (str:byte-slice "aé" 1 3) "é")
!(assertEqual (str:codepoints "aé€") ["a" "é" "€"])
!(assertEqual (case (str:codepoints "aé") (([$c | $rest] ($c $rest)))) ("a" ["é"]))
; A symbol's codepoints are those of its spelling.
!(assertEqual (str:codepoints éa) ["é" "a"])
!(assertEqual (str:from-codepoints ["a" 233 "€"]) "aé€")
!(assertEqual (str:from-codepoints []) "")

; ----------------------------------------------------- a string with NUL ----
!(assertEqual (str:byte-length "a\x00b") 3)
!(assertEqual (== "a\x00b" "a\x00b") True)
!(assertEqual (== "a\x00b" "a\x00c") False)
!(assertEqual (str:compare "a\x00b" "a\x00c") -1)
!(assertEqual (str:compare "a" "a\x00") -1)
; Stored and matched: equal bytes hash together, and bytes after NUL count.
!(bind! &nul (new-space))
!(add-atom &nul (entry "a\x00b" left))
!(add-atom &nul (entry "a\x00c" right))
!(assertEqual (match &nul (entry "a\x00b" $side) $side) left)
!(assertEqual (match &nul (entry $text right) (str:byte-length $text)) 3)
; Printed and read back: the literal is "a\x00b", eight bytes.
!(assertEqual (str:byte-length (repr "a\x00b")) 8)
!(assertEqual (== (parse (repr "a\x00b")) "a\x00b") True)
!(assertEqual (== (parse (repr "a\x00b")) "a\x00c") False)
!(assertEqual (str:byte-length (format-args "<{}>" ("a\x00b"))) 5)
!(assertEqual (str:codepoints "a\x00") ["a" "\x00"])

; ---------------------------------------------------- malformed UTF-8 ----
; 0xC3 starts a two-byte sequence that "ok\xc3" does not finish.
!(assertEqual (str:byte-length "ok\xc3") 3)
!(assertEqual (case (str:codepoint-count "ok\xc3") (((Error $call $detail) $detail))) (StrMalformedUtf8V1 2))
!(assertEqual (case (str:codepoints "\xff") (((Error $call $detail) $detail))) (StrMalformedUtf8V1 0))
!(assertEqual (case (str:all-in? alpha "ab\xe9") (((Error $call $detail) $detail))) (StrMalformedUtf8V1 2))
!(assertEqual (case (str:string->symbol "x\xc3") (((Error $call $detail) $detail))) (StrMalformedUtf8V1 1))
!(assertEqual (case (str:string->symbol "a\x00b") (((Error $call $detail) $detail))) (StrNulInSymbolV1 1))

; ---------------------------------------------------------- round trips ----
!(assertEqual (str:symbol->string (str:string->symbol "cat")) "cat")
!(assertEqual (str:string->symbol (str:symbol->string cat)) cat)
!(assertEqual (str:unescape metta (str:escape metta "a\x00\"\\\n\xff")) "a\x00\"\\\n\xff")
!(assertEqual (str:escape tptp-single "it's") "it\\'s")
!(assertEqual (str:unescape tptp-single "it\\'s") "it's")
!(assertEqual (str:escape tptp-double "say \"hi\"") "say \\\"hi\\\"")
!(assertEqual (str:escape json "a\"b\n") "a\\\"b\\n")
!(assertEqual (str:unescape json "\\u00e9") "é")
!(assertEqual (str:unescape json (str:escape json "é\x01")) "é\x01")
!(assertEqual (case (str:escape tptp-single "é") (((Error $call $detail) $detail))) (StrUnrepresentableV1 0))
!(assertEqual (case (str:unescape json "\\q") (((Error $call $detail) $detail))) (StrInvalidEscapeV1 0))

; ------------------------------------------------------------- spelling ----
!(assertEqual (str:lower-word? "abc_1") True)
!(assertEqual (str:lower-word? "Abc") False)
!(assertEqual (str:upper-word? "X1") True)
!(assertEqual (str:alphanumeric? "a1") True)
!(assertEqual (str:alphanumeric? "a_1") False)
!(assertEqual (str:all-in? word "a_1") True)
!(assertEqual (str:all-in? digit "") True)
!(assertEqual (str:initial-in? digit "") False)
!(assertEqual (str:lower-initial? "é") False)
!(assertEqual (str:upper-initial? "Éa") False)
!(assertEqual (str:starts-with "§w" "§") True)

; ----------------------------------------------------------------- sets ----
!(assertEqual (str:set ["b" "a" "b"]) (StrSetV1 ["a" "b"]))
!(assertEqual (let $set (str:set ["fof" "cnf" "fof"]) (str:set-member? $set "cnf")) True)
!(assertEqual (let $set (str:set ["fof" "cnf"]) (str:set-member? $set "tff")) False)
!(assertEqual (let $set (str:set ["a\x00b"]) (str:set-member? $set "a\x00c")) False)

; -------------------------------------------------------------- numbers ----
!(assertEqual (str:string->number "42") 42)
!(assertEqual (str:string->number "-7") -7)
!(assertEqual (str:string->number "1.5") 1.5)
!(assertEqual (str:string->number "1e3") 1000.0)
!(assertEqual (str:number->string (str:string->number "123456789012345678901234567890")) "123456789012345678901234567890")
!(assertEqual (case (str:string->number "12x") (((Error $call $detail) $detail))) (StrNotANumberV1 2))
!(assertEqual (case (str:string->number "1e999") (((Error $call $detail) $detail))) (StrNumberOutOfRangeV1 0))
!(assertEqual (str:number->string 42) "42")

!(println! (StringLayerV1Summary 62 62 0))
