; PeTTa 1.0.6: ordinary JSON data, without scalar expansion.
!(import! &self json)
!(import! &self list)
!(import! &self str)

; A response can be queried directly, including arrays and nested objects.
!(let* (($response (json:parse "{\"choices\":[{\"message\":{\"content\":\"hello λ\"}}]}"))
        ($choices (json:array-items (json:get $response "choices")))
        ($choice (index-atom $choices 0))
        ($message (json:get $choice "message")))
   (json:get $message "content"))

; When only one subtree is needed, validate the complete document but do not
; allocate its unrelated values. The path mixes object keys and array indices.
!(json:parse-at
     "{\"choices\":[{\"message\":{\"content\":\"hello λ\"}}],\"ignored\":[1,2,3]}"
     ("choices" 0 "message" "content"))
!(json:parse-at "{\"optional\":null}" ("absent"))

; Replace all occurrences of an existing key, or append a new member.
; This is an application policy over ordinary data, not another codec.
(= (json-demo:with (JsonObject $pairs) $key $value)
   (JsonObject
     (case (json:get-first (JsonObject $pairs) $key)
       ((JsonMissingV1 (union-atom $pairs ((JsonPair $key $value))))
        ($found (map-atom $pairs $pair
          (let (JsonPair $name $old) $pair
            (if (== $name $key) (JsonPair $name $value) $pair))))))))

(= (json-demo:without (JsonObject $pairs) $key)
   (JsonObject (filter-atom $pairs $pair
     (let (JsonPair $name $value) $pair (not (== $name $key))))))

!(json:stringify
   (json-demo:with
     (json:parse "{\"name\":\"worker\",\"retries\":2}") "retries" 3))
!(json:get-first (json:parse "{\"optional\":null}") "optional")
!(json:get-first (json:parse "{\"optional\":null}") "absent")
!(json:parse "{\"broken\":}")

; Ordinary numbers already inhabit the host numeric domain.
(= (json-demo:number $value) $value)

!(let $items (json:array-items (json:parse "[1,2,3]"))
   (list:sum-by json-demo:number $items))

; JSON Lines is LF-framed, NOT Unicode-line-framed. U+2028 inside a JSON
; string must not split a record. The existing line collector retains byte
; fragments and assembles each completed record once. Byte chunks may even
; split UTF-8 scalars: validation happens after a complete record is joined.
(= (json-demo:lf-parts $parts $index $length)
   (if (< $index $length)
     (LCons (LinePartV1 (index-atom $parts $index) (< (+ $index 1) $length))
       (json-demo:lf-parts $parts (+ $index 1) $length)) LNil))

(= (json-demo:feed-lines $buffer $bytes $final)
   (let $parts (str:split (str:from-codepoints (10)) $bytes)
     (let $rows (json-demo:lf-parts $parts 0 (size-atom $parts))
       (let $segments (LineSegmentsV1 $rows false)
         (str:collect-lines $buffer $segments $final)))))

(= (json-demo:parse-lines LNil) LNil)
(= (json-demo:parse-lines (LCons (LineV1 $number $text) $rest))
   (LCons (JsonLineV1 $number (json:parse $text)) (json-demo:parse-lines $rest)))

!(let $buffer (str:line-new)
   (let (LinesCollectedV1 $lines $next)
        (json-demo:feed-lines $buffer "{\"x\":1}
{\"x\":2}" true)
     (json-demo:parse-lines $lines)))
