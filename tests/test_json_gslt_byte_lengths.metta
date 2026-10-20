!(import! &self str)
!(import! &self json)

; Construct actual NUL bytes without relying on dialect-specific escapes.
; Raw NUL is not JSON whitespace or a valid unescaped string character.
(= (json-byte-length-status $result)
   (case $result
     (((JsonFailureV1 $operation $status $call $message) $status)
      ($other $other))))
!(json-byte-length-status
   (json:inspect (str:from-codepoints (49 0 32 102 97 108 115 101))))
!(json-byte-length-status
   (json:inspect (str:from-codepoints (110 117 108 108 0))))
!(json-byte-length-status
   (json:inspect (str:from-codepoints (0 49))))
!(json-byte-length-status
   (json:inspect (str:from-codepoints (34 97 0 98 34))))

; Escaped U+0000 is valid JSON and survives canonical round trips.
!(json:inspect-stringify (json:inspect "\"a\\u0000b\""))
!(let $text (str:from-codepoints (97 0 98))
   (json:stringify $text))
!(let $key (str:from-codepoints (120 0 121))
   (json:stringify (JsonObject ((JsonPair $key "λ")))))

; Lookup distinguishes a prefix, an embedded-NUL key, and a missing key;
; duplicate full keys retain their order and occurrence identities.
(= (json-byte-length-object)
   (json:inspect "{\"x\":1,\"x\\u0000y\":2,\"x\\u0000y\":3}"))
!(json:inspect-get-all (json-byte-length-object)
               (str:from-codepoints (120 0 121)))
!(json:inspect-get-first (json-byte-length-object)
                 (str:from-codepoints (120 0 121)))
!(json:inspect-get-last (json-byte-length-object)
                (str:from-codepoints (120 0 121)))
!(json:inspect-get-first (json-byte-length-object) "x")
!(json:inspect-get-first (json-byte-length-object)
                 (str:from-codepoints (120 0 122)))
!(let $key (str:from-codepoints (120 0 121))
   (json:get
     (JsonObject ((JsonPair "x" 1)
                  (JsonPair $key 2)))
     $key))

; Symbols are not native strings in the operational view.
!(json:inspect null)
!(json:stringify hello)
!(json:inspect-get-first (json-byte-length-object) x)

; The number validator must inspect the suffix after NUL as well.
!(let $text (str:from-codepoints (49 0 106))
   (json-byte-length-status (json:inspect-stringify (JsonNumberV1 $text))))
