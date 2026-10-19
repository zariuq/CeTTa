; petta@1.1.c runs a function's declarations as its call contract.  A typed
; call checks each argument, evaluated as its demand says, and its result,
; at every position whose declared type is not a variable.  A check keeps
; the first type of the value that the declared type meets, else the
; value's metatype; a value with neither raises
; (Error (literal_type_mismatch Value Type) typecheck).  The contract is the
; same on every route a call takes, and no run-time dispatch recovers its
; error.  Each `same` line compares a route with the written call it stands
; for, both under catch.

(: cc-id (-> Number Number))
(= (cc-id $x) $x)
(: cc-bad (-> Number Number))
(= (cc-bad $x) not-a-number)
(: cc-sum (-> Number Number Number))
(= (cc-sum $x $y) (+ $x $y))
(: cc-poly (-> $a $a))
(= (cc-poly $x) $x)
(: cc-same (-> $a $a Bool))
(= (cc-same $x $y) True)
(: cc-sym (-> Symbol Bool))
(= (cc-sym $x) True)
(CcHeld id cc-id)
(CcHeld bad cc-bad)
(CcHeld sum cc-sum)
(= (cc-fetch $key) (match &self (CcHeld $key $f) $f))
(= (cc-wrong) wrong)
(= (cc-apply $f $x) ($f $x))
(= (cc-apply-call (Call $f $x)) ($f $x))
(= (cc-route $route $written $via)
   (let $w (msort $written)
     (let $v (msort $via)
       (if (== $w $v) (Route $route same $w) (Route $route differs $w $v)))))

; A written call: a good argument passes; a wrong argument or a wrong
; result raises.
!(cc-id 3)
!(catch (cc-id (cc-wrong)))
!(catch (cc-bad 1))
!(collapse (catch (cc-sum 1 (cc-wrong))))

; Positive: a metatype is a type its values meet.
!(cc-sym foo)
!(catch (cc-sym 1))

; A position whose declared type is a variable is not checked: two such
; positions are not compared, and no value's type is read there.
!(cc-poly anything)
!(cc-same 1 "one")

; A declared type with variables inside is checked: its shape must meet.
(: Pair (-> $a $b (PairT $a $b)))
(: cc-first (-> (PairT $a $b) $a))
(= (cc-first (Pair $x $y)) $x)
!(cc-first (Pair 1 2))
!(let (Error (literal_type_mismatch $v $t) $c) (catch (cc-first 5)) (Mismatch $v $c))

; Every route raises the written call's error.
!(cc-route via-let (collapse (catch (cc-id (cc-wrong))))
          (collapse (catch (let $f cc-id ($f (cc-wrong))))))
!(cc-route via-case (collapse (catch (cc-id (cc-wrong))))
          (collapse (catch (case (cc-fetch id) (($f ($f (cc-wrong))))))))
!(cc-route via-space (collapse (catch (cc-id (cc-wrong))))
          (collapse (catch (let $f (cc-fetch id) ($f (cc-wrong))))))
!(cc-route via-space-result (collapse (catch (cc-bad 1)))
          (collapse (catch (let $f (cc-fetch bad) ($f 1)))))
!(cc-route via-parameter (collapse (catch (cc-id (cc-wrong))))
          (collapse (catch (cc-apply cc-id (cc-wrong)))))
!(cc-route via-retrieved-parameter (collapse (catch (cc-bad 1)))
          (collapse (catch (let $f (cc-fetch bad) (cc-apply $f 1)))))
!(cc-route via-head-pattern (collapse (catch (cc-id wrong)))
          (collapse (catch (cc-apply-call (Call cc-id wrong)))))
!(cc-route via-partial (collapse (catch (cc-sum 1 (cc-wrong))))
          (collapse (catch (let $p (cc-sum 1) ($p (cc-wrong))))))
!(cc-route via-eval (collapse (catch (cc-id wrong)))
          (collapse (catch (let $e (cons-atom cc-id (wrong)) (eval $e)))))
!(cc-route via-reduce (collapse (catch (cc-id wrong)))
          (collapse (catch (reduce (cc-id wrong)))))
!(cc-route via-closure (collapse (catch (cc-bad 1)))
          (collapse (catch (let $f cc-bad (let $g (|-> ($x) ($f $x)) ($g 1))))))
!(cc-route via-map-atom (collapse (catch (cc-id wrong)))
          (collapse (catch (let $f (cc-fetch id) (map-atom (1 wrong) $x ($f $x))))))
!(cc-route via-foldall-step (collapse (catch (cc-sum wrong 1)))
          (collapse (catch (foldall cc-sum (superpose (1 wrong)) 0))))

; Inside an equation body, too.
(= (cc-body-let $x) (let $f cc-id ($f $x)))
(= (cc-body-space $x) (let $f (cc-fetch id) ($f $x)))
!(cc-route via-body-let (collapse (catch (cc-id wrong)))
          (collapse (catch (cc-body-let wrong))))
!(cc-route via-body-space (collapse (catch (cc-id wrong)))
          (collapse (catch (cc-body-space wrong))))

; A run-time dispatch still recovers an error that is not a contract's:
; that path alone fails, while the written call raises.
(= (cc-empty-head $x) (car-atom ()))
!(collapse (let $f cc-empty-head ($f 1)))
!(catch (cc-empty-head 1))

; A contract's error leaves a collection, and catch inside the body takes
; it there.
!(catch (collapse (cc-id (superpose (1 wrong)))))
(= (cc-guarded $x) (catch (cc-id $x)))
!(cc-guarded wrong)

; An unbound argument passes its check, and no check follows its binding.
!(let $r (cc-id $u) (let $u wrong (Got $r)))

; Several declarations are alternatives whose checks filter: a call that
; no declaration admits has no answer.
(: cc-ov (-> Number Number))
(: cc-ov (-> String String))
(= (cc-ov $x) $x)
!(collapse (cc-ov 1))
!(collapse (cc-ov wrong))

; A typed recursion runs its version after a checked entry, and a wrong
; argument at the entry raises.
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: cc-count (-> Nat Number))
(= (cc-count Z) 0)
(= (cc-count (S $k)) (+ 1 (cc-count $k)))
!(cc-count (S (S (S Z))))
!(catch (cc-count (S (S wrong))))
