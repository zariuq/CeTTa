!(import! &self system)
!(import! &self fs)
!(import! &self str)
!(import! &self text_stream)
!(import! &self path)

(= (reverse-atom-expr $items)
   (reverse-atom-expr-acc $items ()))

(= (reverse-atom-expr-acc () $acc)
   $acc)

(= (reverse-atom-expr-acc $items $acc)
   (let ($head $tail) (decons-atom $items)
     (reverse-atom-expr-acc $tail (cons-atom $head $acc))))

(= (append-atom-expr $lhs $rhs)
   (function
     (chain (decons-atom $lhs) $ht
       (unify ($head $tail) $ht
         (chain (eval (append-atom-expr $tail $rhs)) $tail-appended
           (chain (cons-atom $head $tail-appended) $res
             (return $res)))
         (return $rhs)))))

(= (expr-contains () $needle)
   False)

(= (expr-contains $items $needle)
   (let ($head $tail) (decons-atom $items)
     (if (== $head $needle)
         True
         (expr-contains $tail $needle))))

(= (resolve-input-path $filename)
   (if (path-is-absolute? $filename)
       (path-normalize $filename)
       (path-join (system-cwd) $filename)))

(= (mm-boundary-ok? $stream)
   (let $ch (text-stream-peek $stream)
     (if (== $ch Empty)
         True
         (text-char-whitespace? $ch))))

(= (mm-control-token? $token)
   (or (== $token "$c")
       (or (== $token "$v")
           (or (== $token "$f")
               (or (== $token "$e")
                   (or (== $token "$a")
                       (or (== $token "$p")
                           (or (== $token "$d")
                               (or (== $token "${")
                                   (or (== $token "$}")
                                       (or (== $token "$[")
                                           (or (== $token "$]")
                                               (or (== $token "$=")
                                                   (or (== $token "$.")
                                                       (or (== $token "$(")
                                                           (== $token "$)"))))))))))))))))

(= (mm-read-plain-token $stream $acc)
   (let $ch (text-stream-peek $stream)
     (if (== $ch Empty)
         (if (== $acc "")
             parse-unsupported
             (TokenRead $acc $stream))
         (if (text-char-whitespace? $ch)
             (if (== $acc "")
                 parse-unsupported
                 (TokenRead $acc $stream))
             (if (== $ch "$")
                 parse-unsupported
                 (mm-read-plain-token
                   (text-stream-advance $stream 1)
                   (str-concat $acc $ch)))))))

(= (mm-skip-comment $stream)
   (let $peek2 (text-stream-peek-n $stream 2)
     (if (== $peek2 "")
         parse-unsupported
         (if (== $peek2 "$)")
             (text-stream-advance $stream 2)
             (if (== $peek2 "$(")
                 parse-unsupported
                 (mm-skip-comment (text-stream-advance $stream 1)))))))

(= (mm-tokenize-text $text)
   (mm-tokenize-stream (text-stream $text) ()))

(= (mm-tokenize-stream $stream $acc)
   (let $cursor (text-stream-skip-whitespace $stream)
     (if (text-stream-done? $cursor)
         (reverse-atom-expr $acc)
         (let $peek2 (text-stream-peek-n $cursor 2)
           (if (== $peek2 "$(")
               (let $after-comment (mm-skip-comment (text-stream-advance $cursor 2))
                 (case $after-comment
                   ((parse-unsupported parse-unsupported)
                    ($after
                      (if (mm-boundary-ok? $after)
                          (mm-tokenize-stream $after $acc)
                          parse-unsupported)))))
               (if (mm-control-token? $peek2)
                   (let $after-token (text-stream-advance $cursor 2)
                     (if (mm-boundary-ok? $after-token)
                         (mm-tokenize-stream $after-token (cons-atom $peek2 $acc))
                         parse-unsupported))
                   (let $read (mm-read-plain-token $cursor "")
                     (case $read
                       (((TokenRead $token $after)
                         (mm-tokenize-stream $after (cons-atom $token $acc)))
                        (parse-unsupported parse-unsupported))))))))))

(= (map-metamath-token $token)
   (if (== $token "=")
       "mm.="
       (if (== $token "+")
           "mm.+"
           (if (== $token "-")
               "mm.-"
               (if (== $token "*")
                   "mm.*"
                   (if (== $token "/")
                       "mm./"
                       (if (== $token "<")
                           "mm.<"
                           (if (== $token ">")
                               "mm.>"
                               (if (== $token "->")
                                   "mm.->"
                                   $token)))))))))

(= (map-metamath-tokens ())
   ())

(= (map-metamath-tokens $tokens)
   (let ($head $tail) (decons-atom $tokens)
     (let $tail-mapped (map-metamath-tokens $tail)
       (cons-atom (map-metamath-token $head) $tail-mapped))))

(= (inline-block-token? $token)
   (or (== $token "$[")
       (or (== $token "$]")
           (or (== $token "${")
               (== $token "$}")))))

(= (collect-until-token $tokens $stop)
   (collect-until-token-acc $tokens $stop ()))

(= (collect-until-token-acc () $stop $acc)
   parse-unsupported)

(= (collect-until-token-acc $tokens $stop $acc)
   (let ($head $tail) (decons-atom $tokens)
     (if (== $head $stop)
         (collected (reverse-atom-expr $acc) $tail)
         (if (inline-block-token? $head)
             parse-unsupported
             (collect-until-token-acc $tail $stop (cons-atom $head $acc))))))

(= (split-at-proof-marker $tokens)
   (split-at-proof-marker-acc $tokens ()))

(= (split-at-proof-marker-acc () $acc)
   parse-unsupported)

(= (split-at-proof-marker-acc $tokens $acc)
   (let ($head $tail) (decons-atom $tokens)
     (if (== $head "$=")
         (proof-parts (reverse-atom-expr $acc) $tail)
         (if (inline-block-token? $head)
             parse-unsupported
             (split-at-proof-marker-acc $tail (cons-atom $head $acc))))))

(= (parse-c-statement $tokens)
   (let $result (collect-until-token $tokens "$.")
     (case $result
       (((collected $body $rest)
         (let $mapped-body (map-metamath-tokens $body)
           (parsed (c $mapped-body) $rest)))
        (parse-unsupported parse-unsupported)))))

(= (parse-v-statement $tokens)
   (let $result (collect-until-token $tokens "$.")
     (case $result
       (((collected $body $rest)
         (let $mapped-body (map-metamath-tokens $body)
           (parsed (v $mapped-body) $rest)))
        (parse-unsupported parse-unsupported)))))

(= (parse-d-statement $tokens)
   (let $result (collect-until-token $tokens "$.")
     (case $result
       (((collected $body $rest)
         (let $mapped-body (map-metamath-tokens $body)
           (parsed (d $mapped-body) $rest)))
        (parse-unsupported parse-unsupported)))))

(= (parse-f-statement $label $tokens)
   (let $result (collect-until-token $tokens "$.")
     (case $result
       (((collected $body $rest)
         (if (== (size-atom $body) 2)
             (let ($type $rest-body) (decons-atom $body)
               (let ($var $_) (decons-atom $rest-body)
                 (parsed (f $label (map-metamath-token $type) (map-metamath-token $var)) $rest)))
             parse-unsupported))
        (parse-unsupported parse-unsupported)))))

(= (parse-e-statement $label $tokens)
   (parse-typed-math-statement e $label $tokens))

(= (parse-a-statement $label $tokens)
   (parse-typed-math-statement a $label $tokens))

(= (parse-typed-math-statement $tag $label $tokens)
   (let $result (collect-until-token $tokens "$.")
     (case $result
       (((collected $body $rest)
         (if (> (size-atom $body) 0)
             (let ($type $math) (decons-atom $body)
               (let $mapped-math (map-metamath-tokens $math)
                 (parsed ($tag $label (map-metamath-token $type) $mapped-math) $rest)))
             parse-unsupported))
        (parse-unsupported parse-unsupported)))))

(= (finish-parse-p $label $rest parse-unsupported)
   parse-unsupported)

(= (finish-parse-p $label $rest (proof-parts $math-and-type $proof))
   (if (== $proof ())
       parse-unsupported
       (if (== (car-atom $proof) "(")
           parse-unsupported
           (if (> (size-atom $math-and-type) 0)
               (let ($type $math) (decons-atom $math-and-type)
                 (let $mapped-math (map-metamath-tokens $math)
                   (let $mapped-proof (map-metamath-tokens $proof)
                     (parsed
                       (p $label
                          (map-metamath-token $type)
                          $mapped-math
                          $mapped-proof)
                       $rest))))
               parse-unsupported))))

(= (parse-p-statement $label $tokens)
   (let $result (collect-until-token $tokens "$.")
     (case $result
       (((collected $body $rest)
         (finish-parse-p $label $rest (split-at-proof-marker $body)))
        (parse-unsupported parse-unsupported)))))

(= (parse-labeled-statement $label $tokens)
   (if (== $tokens ())
       parse-unsupported
       (let ($keyword $tail) (decons-atom $tokens)
         (if (== $keyword "$f")
             (parse-f-statement $label $tail)
             (if (== $keyword "$e")
                 (parse-e-statement $label $tail)
                 (if (== $keyword "$a")
                     (parse-a-statement $label $tail)
                     (if (== $keyword "$p")
                         (parse-p-statement $label $tail)
                         parse-unsupported)))))))

(= (parse-next-statement $tokens)
   (let ($head $tail) (decons-atom $tokens)
     (if (== $head "$c")
         (parse-c-statement $tail)
         (if (== $head "$v")
             (parse-v-statement $tail)
             (if (== $head "$d")
                 (parse-d-statement $tail)
                 (if (== $head "${")
                     (parsed (open_frame) $tail)
                     (if (== $head "$}")
                         (parsed (close_frame) $tail)
                         (parse-labeled-statement $head $tail))))))))

(= (parse-metamath-file-after-read $filename $seen $loading (Error $info $msg))
   (Error $info $msg))

(= (parse-metamath-file-after-read $filename $seen $loading $text)
   (parse-metamath-file-after-tokens
     $filename
     (cons-atom $filename $seen)
     (cons-atom $filename $loading)
     (mm-tokenize-text $text)))

(= (parse-metamath-file-after-tokens $filename $seen $loading parse-unsupported)
   parse-unsupported)

(= (parse-metamath-file-after-tokens $filename $seen $loading $tokens)
   (parse-metamath-statements-file $tokens $filename $seen $loading 0))

(= (parse-metamath-file $filename $seen $loading)
   (if (expr-contains $loading $filename)
       parse-unsupported
       (if (expr-contains $seen $filename)
           (ParsedStmts () $seen)
           (parse-metamath-file-after-read
             $filename
             $seen
             $loading
             (fs-read-text $filename)))))

(= (parse-include-result $rest parse-unsupported)
   parse-unsupported)

(= (parse-include-result $rest (Error $info $msg))
   (Error $info $msg))

(= (parse-include-result $rest (ParsedStmts $inc-stmts $seen2))
   (IncludeParse $inc-stmts $rest $seen2))

(= (parse-include-directive $tokens $current-file $seen $loading)
   (if (< (size-atom $tokens) 2)
       parse-unsupported
       (let ($include-name $rest1) (decons-atom $tokens)
         (let ($close $rest2) (decons-atom $rest1)
           (if (== $close "$]")
               (let $resolved (path-resolve-from $current-file $include-name)
                 (parse-include-result
                   $rest2
                   (parse-metamath-file $resolved $seen $loading)))
               parse-unsupported)))))

(= (parse-metamath-statements-empty $seen $depth)
   (if (== $depth 0)
       (ParsedStmts () $seen)
       parse-unsupported))

(= (parse-metamath-statements-file () $current-file $seen $loading $depth)
   (parse-metamath-statements-empty $seen $depth))

(= (parse-rest-result $stmt parse-unsupported)
   parse-unsupported)

(= (parse-rest-result $stmt (Error $info $msg))
   (Error $info $msg))

(= (parse-rest-result $stmt (ParsedStmts $rest-stmts $seen2))
   (ParsedStmts (cons-atom $stmt $rest-stmts) $seen2))

(= (parse-include-step-result $current-file $seen $loading $depth parse-unsupported)
   parse-unsupported)

(= (parse-include-step-result $current-file $seen $loading $depth (Error $info $msg))
   (Error $info $msg))

(= (parse-include-step-result $current-file $seen $loading $depth (IncludeParse $inc-stmts $rest-after $seen2))
   (parse-include-step-merge
     $inc-stmts
     (parse-metamath-statements-file $rest-after $current-file $seen2 $loading $depth)))

(= (parse-include-step-merge $inc-stmts parse-unsupported)
   parse-unsupported)

(= (parse-include-step-merge $inc-stmts (Error $info $msg))
   (Error $info $msg))

(= (parse-include-step-merge $inc-stmts (ParsedStmts $rest-stmts $seen3))
   (function
     (chain (eval (append-atom-expr $inc-stmts $rest-stmts)) $merged
       (return (ParsedStmts $merged $seen3)))))

(= (parse-statement-step-result $depth $current-file $seen $loading parse-unsupported)
   parse-unsupported)

(= (parse-statement-step-result $depth $current-file $seen $loading (Error $info $msg))
   (Error $info $msg))

(= (parse-statement-step-result $depth $current-file $seen $loading (parsed $stmt $rest))
   (if (== $stmt (open_frame))
       (parse-rest-result
         $stmt
         (parse-metamath-statements-file $rest $current-file $seen $loading (+ $depth 1)))
       (if (== $stmt (close_frame))
         (if (== $depth 0)
             parse-unsupported
             (parse-rest-result
               $stmt
               (parse-metamath-statements-file $rest $current-file $seen $loading (- $depth 1))))
           (parse-rest-result
             $stmt
             (parse-metamath-statements-file $rest $current-file $seen $loading $depth)))))

(= (parse-metamath-statements-file $tokens $current-file $seen $loading $depth)
   (let ($head $tail) (decons-atom $tokens)
     (if (== $head "$[")
         (if (== $depth 0)
             (parse-include-step-result
               $current-file
               $seen
               $loading
               $depth
               (parse-include-directive $tail $current-file $seen $loading))
             parse-unsupported)
         (parse-statement-step-result
           $depth
           $current-file
           $seen
           $loading
           (parse-next-statement $tokens)))))

(= (parse-metamath-statements $tokens)
   (case (parse-metamath-statements-file $tokens "." () () 0)
     (((ParsedStmts $stmts $seen) $stmts)
      (parse-unsupported parse-unsupported)
      ((Error $info $msg) (Error $info $msg)))))

(= (parse_mm_file_textual $filename)
   (let $resolved (resolve-input-path $filename)
     (case (parse-metamath-file $resolved () ())
       (((ParsedStmts $stmts $seen) $stmts)
        (parse-unsupported parse-unsupported)
        ((Error $info $msg) (Error $info $msg))))))
