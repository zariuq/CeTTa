; Distilled from hyperon-miner's index_to_var helper.
; HE expects the constructor case to thread one fresh variable through both the
; rebuilt term and the history list.
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))

(= (make_var) $x)

(= (find_val $history $val)
    (if (== $history ())
        ()
        (let* (
                (($head $value) (car-atom $history))
                ($tail (cdr-atom $history))
            )
            (if (== $head $val)
                $value
                (find_val $tail $val)
            )
        )))

(= (deb2var () $history)
    (() $history))

(= (deb2var $pattern $history)
    (let* (
        ($head (car-atom $pattern))
        ($tail (cdr-atom $pattern))
        ($history_val (find_val $history $head))
        ($newhead (if (== (get-type $head) Nat)
                      (if (== $history_val ())
                          (let $newvar (make_var)
                              ($newvar (union-atom (($head $newvar)) $history)))
                          ($history_val $history))
                      (if (and (not (== (get-type $head) Nat))
                               (== (get-metatype $head) Expression))
                          (deb2var $head $history)
                          ($head $history))))
        (($var $new_history) $newhead)
        ($newtail (deb2var $tail $new_history))
        (($rest $final_history) $newtail)
    )
        ((cons-atom $var $rest) $final_history)))

!(assertAlphaEqualToResult
    (deb2var (S Z) ())
    (((S $v) ((Z $v)))))
