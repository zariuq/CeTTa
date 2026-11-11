!(import! &self str)

(= (path:reverse-atom-expr $items)
   (path:reverse-atom-expr-acc $items ()))

(= (path:reverse-atom-expr-acc $items $acc)
   (if (== $items ())
       $acc
       (let ($head $tail) (decons-atom $items)
         (path:reverse-atom-expr-acc $tail (cons-atom $head $acc)))))

;; Paths are strings. Anything else is answered with an error at once; the
;; parts below would otherwise take it apart without end. The refused call is
;; quoted, so that a dialect evaluating Error's arguments does not repeat it.
(= (path:text? $path)
   (== (get-type $path) String))

(= (path:is-absolute $path)
   (if (path:text? $path)
       (str:starts-with $path "/")
       (Error (quote (path:is-absolute $path)) "expected a path string")))

(= (path:pop-stack $stack True)
   (if (== $stack ())
       ()
       (cdr-atom $stack)))

(= (path:pop-stack $stack False)
   (if (== $stack ())
       ("..")
       (let ($head $tail) (decons-atom $stack)
         (if (== $head "..")
             (cons-atom ".." $stack)
             $tail))))

(= (path:normalize-parts $parts $stack $absolute)
   (if (== $parts ())
       $stack
       (let ($head $tail) (decons-atom $parts)
         (if (or (== $head "") (== $head "."))
             (path:normalize-parts $tail $stack $absolute)
             (if (== $head "..")
                 (path:normalize-parts $tail (path:pop-stack $stack $absolute) $absolute)
                 (path:normalize-parts $tail (cons-atom $head $stack) $absolute))))))

(= (path:build-from-parts $parts $absolute)
   (let $joined (str:join "/" $parts)
     (if $absolute
         (if (== $joined "")
             "/"
             (str:concat "/" $joined))
         (if (== $joined "")
             "."
             $joined))))

(= (path:parts $path)
   (if (path:text? $path)
       (let $absolute (path:is-absolute $path)
         (path:reverse-atom-expr
           (path:normalize-parts (str:split "/" $path) () $absolute)))
       (Error (quote (path:parts $path)) "expected a path string")))

(= (path:normalize $path)
   (if (path:text? $path)
       (let $absolute (path:is-absolute $path)
         (path:build-from-parts
           (path:parts $path)
           $absolute))
       (Error (quote (path:normalize $path)) "expected a path string")))

(= (path:drop-last $parts)
   (if (== $parts ())
       ()
       (let ($head $tail) (decons-atom $parts)
         (if (== $tail ())
             ()
             (let $rest (path:drop-last $tail)
               (cons-atom $head $rest))))))

(= (path:dirname $path)
   (if (path:text? $path)
       (let $norm (path:normalize $path)
         (path:build-from-parts
           (path:drop-last (path:parts $norm))
           (path:is-absolute $norm)))
       (Error (quote (path:dirname $path)) "expected a path string")))

(= (path:join $base $rel)
   (if (and (path:text? $base) (path:text? $rel))
       (path:join-text $base $rel)
       (Error (quote (path:join $base $rel)) "expected a path string")))

(= (path:join-text $base $rel)
   (if (path:is-absolute $rel)
       (path:normalize $rel)
       (if (or (== $base "") (== $base "."))
           (path:normalize $rel)
           (if (== $base "/")
               (path:normalize (str:concat "/" $rel))
               (path:normalize
                 (if (str:ends-with $base "/")
                     (str:concat $base $rel)
                     (str:concat (str:concat $base "/") $rel)))))))

(= (path:resolve-from $basefile $rel)
   (if (and (path:text? $basefile) (path:text? $rel))
       (if (path:is-absolute $rel)
           (path:normalize $rel)
           (path:join (path:dirname $basefile) $rel))
       (Error (quote (path:resolve-from $basefile $rel)) "expected a path string")))
