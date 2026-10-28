!(import! &self (library str))

;; A variable-headed singleton forces the producer through open-equation
;; answers. Its consumer may still use the prepared native wrapper.
(= (fixture:tail) (let $text "chunk" ($text)))
(= (fixture:chunks) (let $text "chunk" (cons-atom $text (fixture:tail))))
!(test (== (fixture:chunks) ("chunk" "chunk")) true)
!(test (size-atom (fixture:chunks)) 2)
!(test (__cetta_lib_str join "" (fixture:chunks)) "chunkchunk")
!(test (str:join "" (fixture:chunks)) "chunkchunk")
!(test (== (str:set (fixture:chunks)) (str:set ("chunk" "chunk"))) true)

(= (fixture:code-tail) (let $code 66 ($code)))
(= (fixture:codes) (cons-atom 65 (fixture:code-tail)))
!(test (str:from-codepoints (fixture:codes)) "AB")

;; Observing data must neither rerun the producer nor execute held code.
!(bind! &fixture-count (new-state 0))
(= (fixture:tick)
   (progn (change-state! &fixture-count (+ (get-state &fixture-count) 1)) "chunk"))
(= (fixture:once) (let $text (fixture:tick) (cons-atom $text (fixture:tail))))
!(test (str:join "" (fixture:once)) "chunkchunk")
!(test (get-state &fixture-count) 1)
!(test (case (str:join "" ((quote (fixture:tick))))
          (((Error $call $why) true) ($other false))) true)
!(test (get-state &fixture-count) 1)
