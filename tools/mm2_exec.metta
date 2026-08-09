; Basic MM2 execution engine hosted in MeTTa.
;
; tools/mm2_to_metta.py isolates program atoms in a separate space and passes
; this engine a queue already ordered by MORK's encoded term order.  Each
; queue item carries an inert, renamed query and template.
; The supported boundary is intentionally small and fail-closed in the
; translator: comma queries; comma/add/remove outputs; and variable-guarded
; count outputs.  Dynamic exec creation and the remaining MORK sources and
; sinks are rejected rather than approximated.
;
; A step follows native MM2's important ordering: collect every instantiated
; output before performing any write, consume the queue item, finalize the
; outputs, then continue.  Reflective exec queries are rejected by the basic
; translator, so omitting exec atoms from the data space is unobservable in
; the supported subset.

(= (mm2-member $x $items)
   (if (== $items ())
       False
       (let* (($head (car-atom $items))
              ($tail (cdr-atom $items)))
         (if (== $head $x)
             True
             (mm2-member $x $tail)))))

(= (mm2-nub $items)
   (if (== $items ())
       ()
       (let* (($head (car-atom $items))
              ($tail (cdr-atom $items))
              ($rest (mm2-nub $tail)))
         (if (mm2-member $head $rest)
             $rest
             (cons-atom $head $rest)))))

(= (mm2-add $space $atom)
   (let $present (collapse (match $space $atom present))
     (if (== $present ())
         (add-atom $space $atom)
         ())))

(= (mm2-op $space (mm2add $atom))
   (mm2-add $space $atom))
(= (mm2-op $space (mm2del $atom))
   (remove-atom $space $atom))

(= (mm2-ops $space $ops)
   (foldl-atom $ops () $acc $op (mm2-op $space $op)))

(= (mm2-apply $space (mm2ops $ops))
   (mm2-ops $space $ops))

(= (mm2-apply-all $space $instances)
   (foldl-atom $instances () $acc $instance
     (mm2-apply $space $instance)))

(= (mm2-fill-count $term $value)
   (if (== $term MM2_COUNT_SLOT)
       $value
       (if (== (get-metatype $term) Expression)
           (if (== $term ())
               ()
               (let* (($car (car-atom $term))
                      ($cdr (cdr-atom $term))
                      ($head (mm2-fill-count $car $value))
                      ($tail (mm2-fill-count $cdr $value)))
                 (cons-atom $head $tail)))
           $term)))

(= (mm2-firsts $pairs)
   (if (== $pairs ())
       ()
       (let* (($pair (car-atom $pairs))
              ($head (car-atom $pair))
              ($tail (cdr-atom $pairs))
              ($rest (mm2-firsts $tail)))
         (cons-atom $head $rest))))

(= (mm2-seconds-for $key $pairs)
   (if (== $pairs ())
       ()
       (let* (($pair (car-atom $pairs))
              ($pair-key (car-atom $pair))
              ($tail (cdr-atom $pairs))
              ($rest (mm2-seconds-for $key $tail)))
         (if (== $pair-key $key)
             (let* (($pair-tail (cdr-atom $pair))
                    ($value (car-atom $pair-tail)))
               (cons-atom $value $rest))
             $rest))))

(= (mm2-emit-count-groups $space $keys $pairs)
   (if (== $keys ())
       ()
       (let* (($key (car-atom $keys))
              ($tail (cdr-atom $keys))
              ($values0 (mm2-seconds-for $key $pairs))
              ($values (mm2-nub $values0))
              ($count (size-atom $values))
              ($atom (mm2-fill-count $key $count))
              ($done (mm2-add $space $atom)))
         (mm2-emit-count-groups $space $tail $pairs))))

(= (mm2-step $space (mm2write $query $template))
   (let* (($instances (collapse (match $space $query $template)))
          ($done (mm2-apply-all $space $instances)))
     ()))

(= (mm2-step $space (mm2count $query $out $counted))
   (let* (($pairs (collapse (match $space $query ($out $counted))))
          ($keys0 (mm2-firsts $pairs))
          ($keys (mm2-nub $keys0))
          ($done (mm2-emit-count-groups $space $keys $pairs)))
     ()))

(= (mm2-run $space $queue)
   (if (== $queue ())
       ()
       (let* (($entry (car-atom $queue))
              ($tail (cdr-atom $queue))
              ($done (mm2-step $space $entry)))
         (mm2-run $space $tail))))
