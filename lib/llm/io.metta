; Session-local ownership for the buffered codec. Submit once, then retain the
; returned phase. Numeric handles cannot be restored into another session.
!(import! &self llm)
!(import! &self io)

; A JSON bridge avoids foreign-language list conventions (nondeterministic
; results in HE versus recursive data in PeTTa). Reify only these two tags.
(: llm:v1:headers-value (-> Atom %Undefined%))
(= (llm:v1:headers-value $items)
   (if (== $items ()) ()
     (let* (($head (car-atom $items)) ($tail (cdr-atom $items))
            ($header (llm:v1:header-value $head)) ($rest (llm:v1:headers-value $tail)))
       (cons-atom $header $rest))))
(: llm:v1:header-value (-> Atom %Undefined%))
(= (llm:v1:header-value $item)
   (case $item
     (((JsonArray ((JsonString "http:header") $name $value))
       (let* (($name-text (llm:v1:text $name)) ($value-text (llm:v1:text $value)))
         (http:header $name-text $value-text)))
      ((JsonArray ((JsonString "http:credential") $alias))
       (let $name (llm:v1:nonempty-text $alias) (http:credential $name)))
      ($other (llm:v1:failure invalid-header-data)))))
(: llm:v1:headers-json (-> %Undefined% %Undefined%))
(= (llm:v1:headers-json $text)
   (function
     (case
       (let* (($value (llm:v1:parse $text)) ($items (llm:v1:array $value))
              ($headers (llm:v1:headers-value $items))) (llm:v1:checked $headers))
       (((llm:v1:failure $code) (return (llm:v1:error $code)))
        ((Error $source $reason) (return (llm:v1:error invalid-header-data)))
        ($headers (return (llm:v1:ok $headers)))))))

(: llm:v1:submit (-> Atom Atom %Undefined% %Undefined% Atom %Undefined% Number Number %Undefined%))
(= (llm:v1:submit $epoch $correlation $protocol $base $headers $request $timeout $cap)
   (let* (($name (llm:v1:protocol $protocol)) ($encoded (llm:v1:encode $name $request)))
     (llm:v1:submit-encoded $epoch $correlation $name $base $headers $encoded $timeout $cap)))
(: llm:v1:submit-encoded (-> Atom Atom Atom Atom Atom Atom Atom Atom %Undefined%))
(= (llm:v1:submit-encoded $epoch $correlation $protocol $base $headers $encoded $timeout $cap)
   (case $encoded
     (((llm:v1:ok (llm:v1:encoded $path $body))
       (let $url (str:concat $base $path)
         (llm:v1:start $epoch $correlation $protocol $url $headers $body $timeout $cap)))
      ((llm:v1:error $code) (llm:v1:failed $epoch $correlation $code not-started))
      ($other (llm:v1:failed $epoch $correlation invalid-request not-started)))))
(: llm:v1:start (-> Atom Atom Atom Atom Atom Atom Atom Atom %Undefined%))
(= (llm:v1:start $epoch $correlation $protocol $url $headers $body $timeout $cap)
   (function
     (case (io:submit (http:request "POST" $url $headers $body $timeout $cap))
       (((io:pending $id) (return (llm:v1:pending $epoch $correlation $protocol $id)))
        ((Error $source $reason) (return (llm:v1:failed $epoch $correlation submit-error not-started)))
        ($other (return (llm:v1:failed $epoch $correlation submit-error not-started)))))))
(: llm:v1:observe (-> Atom Atom %Undefined%))
(= (llm:v1:observe $state $event)
   (case $state
     (((llm:v1:pending $epoch $correlation $protocol $id)
       (case $event
         (((io:event $event-id $outcome)
           (if (== $event-id $id)
             (llm:v1:io-outcome $epoch $correlation $protocol $outcome)
             (llm:v1:foreign $state $event)))
          ((io:unknown $event-id)
           (if (== $event-id $id)
             (llm:v1:failed $epoch $correlation retired-handle remote-outcome-unknown)
             (llm:v1:foreign $state $event)))
          ((io:idle) $state)
          ($foreign (llm:v1:foreign $state $event)))))
      ($terminal $state))))
(: llm:v1:io-outcome (-> Atom Atom Atom Atom %Undefined%))
(= (llm:v1:io-outcome $epoch $correlation $protocol $outcome)
   (case $outcome
     (((http:response $status $body)
       (if (and (>= $status 200) (< $status 300))
         (let $decoded (llm:v1:decode $protocol $body)
           (case $decoded
             (((llm:v1:ok $result) (llm:v1:completed $epoch $correlation $result))
              ((llm:v1:error $code) (llm:v1:failed $epoch $correlation $code response-received)))))
         (llm:v1:failed $epoch $correlation (http-status $status) response-received)))
      ($other (llm:v1:failed $epoch $correlation transport-error remote-outcome-unknown)))))
(: llm:v1:poll (-> Atom %Undefined%))
(= (llm:v1:poll $state)
   (case $state
     (((llm:v1:pending $epoch $correlation $protocol $id)
       (llm:v1:poll-id $state $epoch $correlation $id))
      ($terminal $state))))
(: llm:v1:wait (-> Atom Number %Undefined%))
(= (llm:v1:wait $state $milliseconds)
   (case $state
     (((llm:v1:pending $epoch $correlation $protocol $id)
       (llm:v1:wait-id $state $epoch $correlation $id $milliseconds))
      ($terminal $state))))
; PeTTa raises Error values returned by natives; the generated spelling catches
; only this selected handle's call. Diagnostics never become provider data.
(: llm:v1:poll-id (-> Atom Atom Atom Atom %Undefined%))
(= (llm:v1:poll-id $state $epoch $correlation $id)
   (function
     (case (io:poll $id)
       (((Error $source $reason) (return (llm:v1:failed $epoch $correlation transport-error remote-outcome-unknown)))
        ($event (return (llm:v1:observe $state $event)))))))
(: llm:v1:wait-id (-> Atom Atom Atom Atom Atom %Undefined%))
(= (llm:v1:wait-id $state $epoch $correlation $id $milliseconds)
   (function
     (case (io:wait $id $milliseconds)
       (((Error $source $reason) (return (llm:v1:failed $epoch $correlation transport-error remote-outcome-unknown)))
        ($event (return (llm:v1:observe $state $event)))))))
(: llm:v1:cancel (-> Atom %Undefined%))
(= (llm:v1:cancel $state)
   (case $state
     (((llm:v1:pending $epoch $correlation $protocol $id) (llm:v1:cancel-id $epoch $correlation $id))
      ($terminal $state))))
(: llm:v1:cancel-id (-> Atom Atom Atom %Undefined%))
(= (llm:v1:cancel-id $epoch $correlation $id)
   (function
     (case (io:cancel $id)
       ((() (return (llm:v1:cancelled $epoch $correlation remote-outcome-unknown)))
        ((Error $source $reason) (return (llm:v1:failed $epoch $correlation retired-handle remote-outcome-unknown)))
        ($other (return (llm:v1:failed $epoch $correlation cancel-error remote-outcome-unknown)))))))
