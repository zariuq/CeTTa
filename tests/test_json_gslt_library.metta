!(import! &self json)

; GödelClaw JSON compatibility: nested values may be produced by ordinary
; MeTTa definitions before they reach the compatibility stringifier.
(= (json-nested-inner)
   (JsonObject
     ((JsonPair "write" (JsonArray ((JsonString "/repo/runtime")))))))
(= (json-nested-outer)
   (JsonObject
     ((JsonPair "permissions" (json-nested-inner))
      (JsonPair "scope" (JsonString "turn")))))

!(json:parse "{\"x\":1,\"x\":2}")
!(json-parse "{\"x\":1,\"x\":2}")

!(json:get-all (json:parse "{\"x\":1,\"x\":2}") "x")
!(json:get-first (json:parse "{\"x\":1,\"x\":2}") "x")
!(json:get-last (json:parse "{\"x\":1,\"x\":2}") "x")
!(json:get-first (json:parse "{\"x\":1}") "missing")

!(json-object-get (json-parse "{\"x\":1,\"x\":2}") "x")
!(json-object-get (json-parse "{\"x\":1}") "missing")
!(json-string-value
   (json-object-get (json-parse "{\"kind\":\"message\"}") "kind"))
!(assertEqualToResult
   (json-bool-value
     (json-object-get (json-parse "{\"ok\":true}") "ok"))
   (True))

!(json:stringify (json:parse "[\"\\u0000\",-12.50e+2]"))
!(json-stringify (json-parse "{\"x\":1,\"x\":2}"))
!(json-stringify (json-nested-outer))

; Canonical failures are data rather than dialect-specific engine errors.
!(json:parse "01")
!(json:stringify (JsonNumberV1 "01"))

; Conversion preserves ordered occurrences and exact values without a wire trip.
!(json-stringify
   (json:to-legacy (json:parse "{\"x\":-12.50e+2,\"x\":[true,false,null,\"λ\\u0000z\"]}")))
!(case (json:to-legacy (JsonStringV1 ((cp 55296))))
   (((JsonFailureV1 $operation $status $call $message) $status)
    ($other UnexpectedJsonConversion)))
