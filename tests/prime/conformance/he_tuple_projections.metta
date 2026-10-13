; The product signature the HE tuple fragment reads in the hosted HOL Light
; checker: HOL Light's pair type `prod`, its unit `1`, the pairing `,`, the
; projections `FST` and `SND`, and `one`.  An HE tuple (x0 x1 ... xn) is the
; pair x0, (x1, ... (xn, one)).  Each projection of a tuple has the type of
; its component, so a component's position is visible to the checker; a
; projection read at another component's type is refused.
(scope:profile lexical-fresh per-call reference)
!(import! &self ../../../lib/prime/hol_light/data.metta)
!(import! &self ../../../lib/prime/hol_light/syntax.metta)
!(import! &self ../../../lib/prime/hol_light/kernel.metta)
!(import! &self ../../../lib/prime/hol_light/definitions.metta)

(= (base $name) (Tyapp $name HOL:Nil))
(= (pair-of $left $right)
   (Tyapp "prod" (HOL:Cons $left (HOL:Cons $right HOL:Nil))))
(= (arrow $domain $codomain)
   (Tyapp "fun" (HOL:Cons $domain (HOL:Cons $codomain HOL:Nil))))
(= (unit) (Tyapp "1" HOL:Nil))

; The signature: HE's base types Bool and Nat with constants Truth and Z,
; and the pair and unit signature.
(= (signature)
   (let* (($a (Tyvar "A")) ($b (Tyvar "B")) ($ab (pair-of $a $b)))
     (HOL:Cons (HOL:Step "type:Bool" (DECLARE_TYPE "he:Bool" 0))
     (HOL:Cons (HOL:Step "type:Nat" (DECLARE_TYPE "he:Nat" 0))
     (HOL:Cons (HOL:Step "type:prod" (DECLARE_TYPE "prod" 2))
     (HOL:Cons (HOL:Step "type:1" (DECLARE_TYPE "1" 0))
     (HOL:Cons (HOL:Step "constant:," (DECLARE_CONSTANT "," (arrow $a (arrow $b $ab))))
     (HOL:Cons (HOL:Step "constant:FST" (DECLARE_CONSTANT "FST" (arrow $ab $a)))
     (HOL:Cons (HOL:Step "constant:SND" (DECLARE_CONSTANT "SND" (arrow $ab $b)))
     (HOL:Cons (HOL:Step "constant:one" (DECLARE_CONSTANT "one" (unit)))
     (HOL:Cons (HOL:Step "constant:Truth" (DECLARE_CONSTANT "he:Truth" (base "he:Bool")))
     (HOL:Cons (HOL:Step "constant:Z" (DECLARE_CONSTANT "he:Z" (base "he:Nat")))
     HOL:Nil))))))))))))

; The tuple (Truth Z) of type (Bool Nat): Truth, (Z, one).
(= (rest-type) (pair-of (base "he:Nat") (unit)))
(= (tuple-type) (pair-of (base "he:Bool") (rest-type)))
(= (pairing $left $right)
   (Const "," (arrow $left (arrow $right (pair-of $left $right)))))
(= (tuple)
   (Comb (Comb (pairing (base "he:Bool") (rest-type))
               (Const "he:Truth" (base "he:Bool")))
         (Comb (Comb (pairing (base "he:Nat") (unit))
                     (Const "he:Z" (base "he:Nat")))
               (Const "one" (unit)))))

(= (first $pair-type $component $term)
   (Comb (Const "FST" (arrow $pair-type $component)) $term))
(= (second $pair-type $rest $term)
   (Comb (Const "SND" (arrow $pair-type $rest)) $term))

; The type the checker computes for a term, or Refused.
(= (checked-type $term)
   (case (hol-light:run HOL:Nil (signature))
     (((Some $state)
        (case (hol-light:read $state $term)
          (((Some $value) (hol-light:term-type $value))
           ($refused Refused))))
      ($refused NoSignature))))

!(Tuple (checked-type (tuple)))
!(FirstComponent
   (checked-type (first (tuple-type) (base "he:Bool") (tuple))))
!(SecondComponent
   (checked-type
     (first (rest-type) (base "he:Nat")
       (second (tuple-type) (rest-type) (tuple)))))
!(SwappedFirst
   (checked-type (first (tuple-type) (base "he:Nat") (tuple))))
!(SwappedPair
   (checked-type
     (Comb (Comb (pairing (base "he:Nat") (rest-type))
                 (Const "he:Truth" (base "he:Bool")))
           (Comb (Comb (pairing (base "he:Nat") (unit))
                       (Const "he:Z" (base "he:Nat")))
                 (Const "one" (unit))))))
