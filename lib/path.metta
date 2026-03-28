!(import! &self str)

(= (path-reverse-atom-expr $items)
   (path-reverse-atom-expr-acc $items ()))

(= (path-reverse-atom-expr-acc () $acc)
   $acc)

(= (path-reverse-atom-expr-acc $items $acc)
   (let ($head $tail) (decons-atom $items)
     (path-reverse-atom-expr-acc $tail (cons-atom $head $acc))))

(= (path-is-absolute? $path)
   (str-starts-with? $path "/"))

(= (path-pop-stack () True)
   ())

(= (path-pop-stack () False)
   (".."))

(= (path-pop-stack $stack True)
   (cdr-atom $stack))

(= (path-pop-stack $stack False)
   (let ($head $tail) (decons-atom $stack)
     (if (== $head "..")
         (cons-atom ".." $stack)
         $tail)))

(= (path-normalize-parts () $stack $absolute)
   $stack)

(= (path-normalize-parts $parts $stack $absolute)
   (let ($head $tail) (decons-atom $parts)
     (if (or (== $head "") (== $head "."))
         (path-normalize-parts $tail $stack $absolute)
         (if (== $head "..")
             (path-normalize-parts $tail (path-pop-stack $stack $absolute) $absolute)
             (path-normalize-parts $tail (cons-atom $head $stack) $absolute)))))

(= (path-build-from-parts $parts $absolute)
   (let $joined (str-join "/" $parts)
     (if $absolute
         (if (== $joined "")
             "/"
             (str-concat "/" $joined))
         (if (== $joined "")
             "."
             $joined))))

(= (path-parts $path)
   (let $absolute (path-is-absolute? $path)
     (path-reverse-atom-expr
       (path-normalize-parts (str-split "/" $path) () $absolute))))

(= (path-normalize $path)
   (let $absolute (path-is-absolute? $path)
     (path-build-from-parts
       (path-parts $path)
       $absolute)))

(= (path-drop-last ())
   ())

(= (path-drop-last $parts)
   (let ($head $tail) (decons-atom $parts)
     (if (== $tail ())
         ()
         (let $rest (path-drop-last $tail)
           (cons-atom $head $rest)))))

(= (path-dirname $path)
   (let $norm (path-normalize $path)
     (path-build-from-parts
       (path-drop-last (path-parts $norm))
       (path-is-absolute? $norm))))

(= (path-join $base $rel)
   (if (path-is-absolute? $rel)
       (path-normalize $rel)
       (if (or (== $base "") (== $base "."))
           (path-normalize $rel)
           (if (== $base "/")
               (path-normalize (str-concat "/" $rel))
               (path-normalize
                 (if (str-ends-with? $base "/")
                     (str-concat $base $rel)
                     (str-concat (str-concat $base "/") $rel)))))))

(= (path-resolve-from $basefile $rel)
   (if (path-is-absolute? $rel)
       (path-normalize $rel)
       (path-join (path-dirname $basefile) $rel)))
