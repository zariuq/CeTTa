!(import! &self json)

; Nested values may be produced by ordinary MeTTa definitions before they
; reach the operational stringifier.
(= (json-nested-inner)
   (JsonObject
     ((JsonPair "write" (JsonArray ("/repo/runtime"))))))
(= (json-nested-outer)
   (JsonObject
     ((JsonPair "permissions" (json-nested-inner))
      (JsonPair "scope" "turn"))))

!(json:inspect "{\"x\":1,\"x\":2}")
!(json:parse "{\"x\":1,\"x\":2}")

!(json:inspect-get-all (json:inspect "{\"x\":1,\"x\":2}") "x")
!(json:inspect-get-first (json:inspect "{\"x\":1,\"x\":2}") "x")
!(json:inspect-get-last (json:inspect "{\"x\":1,\"x\":2}") "x")
!(json:inspect-get-first (json:inspect "{\"x\":1}") "missing")

!(json:get (json:parse "{\"x\":1,\"x\":2}") "x")
!(json:get (json:parse "{\"x\":1}") "missing")
!(json:get (json:parse "{\"kind\":\"message\"}") "kind")
!(assertEqualToResult
   (json:get (json:parse "{\"ok\":true}") "ok")
   (True))

!(json:inspect-stringify (json:inspect "[\"\\u0000\",-12.50e+2]"))
!(json:stringify (json:parse "{\"x\":1,\"x\":2}"))
!(json:stringify (json-nested-outer))

; Canonical failures are data rather than dialect-specific engine errors.
!(json:inspect "01")
!(json:inspect-stringify (JsonNumberV1 "01"))

; Conversion preserves ordered occurrences and exact values without a wire trip.
!(json:stringify
   (json:from-inspection (json:inspect "{\"x\":-12.50e+2,\"x\":[true,false,null,\"λ\\u0000z\"]}")))
!(case (json:from-inspection (JsonStringV1 ((cp 55296))))
   (((JsonFailureV1 $operation $status $call $message) $status)
    ($other UnexpectedJsonConversion)))
