!(import! &self json)

; GödelClaw compatibility: nested values may be produced by ordinary MeTTa
; definitions before they reach the historical stringifier.
(= (json-godel-inner)
   (JsonObject
     ((JsonPair "write" (JsonArray ((JsonString "/repo/runtime")))))))
(= (json-godel-outer)
   (JsonObject
     ((JsonPair "permissions" (json-godel-inner))
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
!(json-stringify (json-godel-outer))

; Canonical failures are data rather than dialect-specific engine errors.
!(json:parse "01")
!(json:stringify (JsonNumberV1 "01"))
