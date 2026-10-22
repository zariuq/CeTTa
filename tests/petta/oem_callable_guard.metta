; Two captures plus one supplied argument exercise exact arity three.
; Values are already evaluated at OEM APPLY; no source effects are skipped.
(= (guard-build $depth $leaf)
   (if (< $depth 1) $leaf (Node (guard-build (- $depth 1) $leaf))))
(= (guard-repeat $count $f)
   (if (< $count 1) done
      (let $ignored ($f ignored) (guard-repeat (- $count 1) $f))))
(= (guard-root-loop $count $graph $stamp)
   (let $f (|-> ($unused) (Pair (is-expr $graph) $stamp))
     (guard-repeat $count $f)))
!(guard-root-loop 32 (guard-build 64 $live) stamp)

(= (guard-inspect $graph $stamp)
   (let $f (|-> ($unused) (Result (is-var $graph) $stamp)) ($f ignored)))
; A later binding must be read, while an earlier Boolean stays an observation.
!(let $before (guard-inspect $live stamp)
   (let $live done (Pair $before (guard-inspect $live stamp))))
; Each branch restores its own alias context.
!(collapse (let $value (superpose (first second))
   (let $live $value (guard-inspect $live stamp))))
; Two supports conservatively keep full resolution; aliases remain distinct.
(= (guard-return $left $right)
   (let $f (|-> ($unused) (Pair $left $right)) ($f ignored)))
!(let $answer (guard-return (Node $a $b) stamp)
   (let $a first (let $b second $answer)))
; Ordinary payloads retain their original meaning.
!(let $value capture (guard-return (Node $value) stamp))
; Duplicate producer occurrences run the consumer effect once each.
!(bind! &guard-effects (new-space))
(= (guard-effect $left $right)
   (let $f (|-> ($unused) (add-atom &guard-effects (Seen $left $right)))
     ($f ignored)))
!(collapse (let $duplicate (superpose (same same)) (guard-effect $duplicate stamp)))
!(collapse (match &guard-effects $item $item))
; Overapplication must keep the canonical route and preserve supplied effects.
!(collapse ((|-> ($a) $a) (println! arg-first) (println! arg-second)))
