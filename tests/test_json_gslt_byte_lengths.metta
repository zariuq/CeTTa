!(import! &self str)
!(import! &self json)

; Construct actual NUL bytes without relying on dialect-specific escapes.
; Raw NUL is not JSON whitespace or a valid unescaped string character.
(= (json-byte-length-status $result)
   (case $result
     (((JsonFailureV1 $operation $status $call $message) $status)
      ($other $other))))
!(json-byte-length-status
   (json:parse (str:from-codepoints (49 0 32 102 97 108 115 101))))
!(json-byte-length-status
   (json:parse (str:from-codepoints (110 117 108 108 0))))
!(json-byte-length-status
   (json:parse (str:from-codepoints (0 49))))
!(json-byte-length-status
   (json:parse (str:from-codepoints (34 97 0 98 34))))

; Escaped U+0000 is valid JSON and survives canonical round trips.
!(json:stringify (json:parse "\"a\\u0000b\""))
!(let $text (str:from-codepoints (97 0 98))
   (json-stringify (JsonString $text)))
!(let $key (str:from-codepoints (120 0 121))
   (json-stringify (JsonObject ((JsonPair $key (JsonString "λ"))))))

; Lookup distinguishes a prefix, an embedded-NUL key, and a missing key;
; duplicate full keys retain their order and occurrence identities.
(= (json-byte-length-object)
   (json:parse "{\"x\":1,\"x\\u0000y\":2,\"x\\u0000y\":3}"))
!(json:get-all (json-byte-length-object)
               (str:from-codepoints (120 0 121)))
!(json:get-first (json-byte-length-object)
                 (str:from-codepoints (120 0 121)))
!(json:get-last (json-byte-length-object)
                (str:from-codepoints (120 0 121)))
!(json:get-first (json-byte-length-object) "x")
!(json:get-first (json-byte-length-object)
                 (str:from-codepoints (120 0 122)))
!(let $key (str:from-codepoints (120 0 121))
   (json-object-get
     (JsonObject ((JsonPair "x" (JsonNumber "1"))
                  (JsonPair $key (JsonNumber "2"))))
     $key))

; Symbol text remains supported where the JSON API accepts text.
!(json:parse null)
!(json-stringify (JsonString hello))
!(json:get-first (json-byte-length-object) x)

; The number validator must inspect the suffix after NUL as well.
!(let $text (str:from-codepoints (49 0 106))
   (json-byte-length-status (json:stringify (JsonNumberV1 $text))))
