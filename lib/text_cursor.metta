!(import! &self str)

(= (text-cursor $text)
   (TextCursor $text 0))

(= (text-cursor-from $text $pos)
   (TextCursor $text $pos))

(= (text-cursor-text (TextCursor $text $pos))
   $text)

(= (text-cursor-pos (TextCursor $text $pos))
   $pos)

(= (text-cursor-done? (TextCursor $text $pos))
   (>= $pos (str-length $text)))

(= (text-cursor-rest (TextCursor $text $pos))
   (str-slice $text $pos (str-length $text)))

(= (text-cursor-peek (TextCursor $text $pos))
   (if (>= $pos (str-length $text))
       Empty
       (str-slice $text $pos (+ $pos 1))))

(= (text-cursor-peek-n (TextCursor $text $pos) $count)
   (let $len (str-length $text)
     (if (<= $count 0)
         ""
         (if (>= $pos $len)
             ""
             (let $end (+ $pos $count)
               (str-slice $text $pos (if (> $end $len) $len $end)))))))

(= (text-cursor-advance (TextCursor $text $pos) $count)
   (let $len (str-length $text)
     (let $next (+ $pos $count)
       (TextCursor $text (if (> $next $len) $len $next)))))

(= (text-char-whitespace? $char)
   (or (== $char " ")
       (or (== $char "\t")
           (or (== $char "\n")
               (== $char "\r")))))

(= (text-cursor-skip-whitespace $cursor)
   (let $ch (text-cursor-peek $cursor)
     (if (== $ch Empty)
         $cursor
         (if (text-char-whitespace? $ch)
             (text-cursor-skip-whitespace (text-cursor-advance $cursor 1))
             $cursor))))

(= (text-cursor-prefix? $cursor $prefix)
   (str-starts-with? (text-cursor-rest $cursor) $prefix))

(= (text-cursor-consume-prefix $cursor $prefix)
   (if (text-cursor-prefix? $cursor $prefix)
       (text-cursor-advance $cursor (str-length $prefix))
       Empty))

(= (text-find-from $text $start $needle)
   (let $tail (str-slice $text $start (str-length $text))
     (let $found (str-find $tail $needle)
       (if (== $found Empty)
           Empty
           (+ $start $found)))))

(= (text-cursor-find (TextCursor $text $pos) $needle)
   (text-find-from $text $pos $needle))
