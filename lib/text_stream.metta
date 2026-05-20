!(import! &self str)
!(import! &self text_cursor)

(= (text-stream:new $text)
   (text-cursor:new $text))

(= (text-stream:from $text $pos)
   (text-cursor:from $text $pos))

(= (text-stream:text $stream)
   (text-cursor:text $stream))

(= (text-stream:pos $stream)
   (text-cursor:pos $stream))

(= (text-stream:is-done $stream)
   (text-cursor:is-done $stream))

(= (text-stream:rest $stream)
   (text-cursor:rest $stream))

(= (text-stream:peek $stream)
   (text-cursor:peek $stream))

(= (text-stream:peek-n $stream $count)
   (text-cursor:peek-n $stream $count))

(= (text-stream:advance $stream $count)
   (text-cursor:advance $stream $count))

(= (text-stream:skip-whitespace $stream)
   (text-cursor:skip-whitespace $stream))

(= (text-stream:read-token $stream)
   (let $cursor (text-stream:skip-whitespace $stream)
     (if (text-stream:is-done $cursor)
         Empty
         (text-stream:read-token-acc $cursor ""))))

(= (text-stream:read-token-acc $stream $acc)
   (let $ch (text-stream:peek $stream)
     (if (== $ch Empty)
         (TokenResult $acc $stream)
         (if (text-char:is-whitespace $ch)
             (TokenResult $acc $stream)
             (text-stream:read-token-acc
               (text-stream:advance $stream 1)
               (str:concat $acc $ch))))))

(= (text-stream:read-until $stream $needle)
   (let $idx (text-cursor:find $stream $needle)
     (if (== $idx Empty)
         Empty
         (let $start (text-stream:pos $stream)
           (let $text (text-stream:text $stream)
             (ReadUntilResult
               (str:slice $text $start $idx)
               (text-stream:from $text $idx)))))))

(= (text-stream:has-prefix $stream $prefix)
   (text-cursor:has-prefix $stream $prefix))

(= (text-stream:consume-prefix $stream $prefix)
   (text-cursor:consume-prefix $stream $prefix))
