(: payload-values (-> Atom %Undefined%))
(= (payload-values $items)
   (case $items
     ((End End)
      ((Link (Payload $key $value) $rest)
       (let $tail (payload-values $rest)
         (Link $value $tail))))))

; Selecting another field is another recurrence and stays ordinary here.
(: payload-keys (-> Atom %Undefined%))
(= (payload-keys $items)
   (case $items
     ((End End)
      ((Link (Payload $key $value) $rest)
       (let $tail (payload-keys $rest)
         (OtherLink $key $tail))))))

!(payload-values (Link (Payload a 1) (Link (Payload b 2) End)))
!(payload-keys (Link (Payload a 1) (Link (Payload b 2) End)))
!(payload-values Other)
!(once (payload-values $open))
