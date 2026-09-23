!(import! &self langdef)

; Check the active host reader itself, including its atom kind and spelling.
; A successful parse that changes Symbol True to another atom is not exact.
(= (test:host-symbol-exact $symbol)
   (let $observed (parse (repr $symbol))
     (and (== (get-metatype $observed) Symbol)
          (== $observed $symbol))))

; Construct True as a symbol without letting the source reader interpret it.
!(let $source (langdef:append-symbol T rue)
   (let $escaped (langdef:append-symbol ¤T rue)
     (let $ordinary (langdef:append-symbol c at)
       (let $post-atom-escape (langdef:append-symbol ¤ $source)
         (let $parsed-source (parse "True")
           (let $parsed-escaped (parse "¤True")
             (HostSymbolReadbackV1
               (test:host-symbol-exact $source)
               (test:host-symbol-exact $escaped)
               (test:host-symbol-exact $ordinary)
               (== $source $parsed-source)
               (== $escaped $parsed-escaped)
               (== $post-atom-escape $escaped))))))))
