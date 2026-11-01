(: spine-drop (-> Number Atom %Undefined%))
(= (spine-drop $count $items)
   (if (== $count 0) $items
     (case $items
       ((End End)
        ((Link $item $rest)
         (let $previous (- $count 1)
           (spine-drop $previous $rest)))))))

; A decrement by two is a different recurrence.
(: spine-drop-two (-> Number Atom %Undefined%))
(= (spine-drop-two $count $items)
   (if (== $count 0) $items
     (case $items
       ((End End)
        ((Link $item $rest)
         (let $previous (- $count 2)
           (spine-drop-two $previous $rest)))))))

!(spine-drop 2 (Link a (Link b (Link c End))))
!(spine-drop 0 (Link a End))
!(spine-drop -1 (Link a (Link b End)))
!(spine-drop-two 2 (Link a (Link b (Link c End))))
!(spine-drop 1 Other)
!(once (spine-drop 1 $open))
