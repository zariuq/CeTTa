!(import! &self str)

(= (text-cursor:new $text)
   (TextCursor $text 0))

(= (text-cursor:from $text $pos)
   (TextCursor $text $pos))

(= (text-cursor:text (TextCursor $text $pos))
   $text)

(= (text-cursor:pos (TextCursor $text $pos))
   $pos)

(= (text-cursor:is-done (TextCursor $text $pos))
   (>= $pos (str:length $text)))

(= (text-cursor:rest (TextCursor $text $pos))
   (str:slice $text $pos (str:length $text)))

(= (text-cursor:peek (TextCursor $text $pos))
   (if (>= $pos (str:length $text))
       AtEof
       (str:slice $text $pos (+ $pos 1))))

(= (text-cursor:peek-n (TextCursor $text $pos) $count)
   (let $len (str:length $text)
     (if (<= $count 0)
         ""
         (if (>= $pos $len)
             ""
             (let $end (+ $pos $count)
               (str:slice $text $pos (if (> $end $len) $len $end)))))))

(= (text-cursor:advance (TextCursor $text $pos) $count)
   (let $len (str:length $text)
     (let $next (+ $pos $count)
       (TextCursor $text (if (> $next $len) $len $next)))))

(= (text-char:is-whitespace $char)
   (or (== $char " ")
       (or (== $char "\t")
           (or (== $char "\n")
               (== $char "\r")))))

(= (text-cursor:skip-whitespace $cursor)
   (case (text-cursor:peek $cursor)
     ((AtEof $cursor)
      ($ch
       (if (text-char:is-whitespace $ch)
           (text-cursor:skip-whitespace (text-cursor:advance $cursor 1))
           $cursor)))))

(= (text-cursor:has-prefix $cursor $prefix)
   (str:starts-with (text-cursor:rest $cursor) $prefix))

(= (text-cursor:consume-prefix $cursor $prefix)
   (if (text-cursor:has-prefix $cursor $prefix)
       (text-cursor:advance $cursor (str:length $prefix))
       (TextPrefixMismatch $prefix)))

(= (text-cursor:find-from $text $start $needle)
   (let $tail (str:slice $text $start (str:length $text))
     (case (str:find $tail $needle)
       (((TextNotFound $missing) (TextNotFound $missing))
        ($found (+ $start $found))))))

(= (text-cursor:find (TextCursor $text $pos) $needle)
   (text-cursor:find-from $text $pos $needle))
