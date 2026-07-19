; Native Prime checks the named telescope and emits its closed canonical ABT.
; The canonical term is evidence from an elaborator, never checker authority.

(: Nat Type)

(= (canonical-binders:canonical
      (PrimeVerdict Established (Form $surface)
        (TelescopeFormation $surface (CanonicalABT $canonical))))
   $canonical)

(= (canonical-binders:status
      (PrimeVerdict $status $_judgment $_evidence))
   $status)

(= (canonical-binders:id $name)
   (prime-judge &self (Form (-> (: $name Type) $name))))

!(assertEqual
   (canonical-binders:canonical (canonical-binders:id $x))
   (Pi Type (Var 0)))
!(assertEqual
   (canonical-binders:canonical (canonical-binders:id $x))
   (canonical-binders:canonical (canonical-binders:id $renamed)))
!(assertEqual
   (canonical-binders:canonical
     (prime-judge &self
       (Form (-> (: $x Type) (: $y $x) $x))))
   (Pi Type (Pi (Var 0) (Var 1))))
!(assertEqual
   (canonical-binders:canonical
     (prime-judge &self
       (Form (-> (: $x Type) (: $x $x) $x))))
   (Pi Type (Pi (Var 0) (Var 0))))
!(assertEqual
   (canonical-binders:canonical
     (prime-judge &self (Form (-> Nat Nat))))
   (Pi Nat Nat))
!(assertEqual
   (__cetta_abt_scope_check
     (AbtSignatures) 0
     (canonical-binders:canonical (canonical-binders:id $x)))
   True)
!(assertEqual
   (__cetta_abt_scope_check (AbtSignatures) 0 (Var 0))
   False)
!(assertEqual
   (canonical-binders:status
     (prime-judge &self (Form (-> (: $x Type) $outside))))
   Undetermined)
!(assertEqual
   (canonical-binders:status
     (prime-judge &self (Form (-> (: not-a-variable Type) Type))))
   Refuted)
!(assertEqual
   (canonical-binders:canonical
     (prime-judge &self
       (Form (-> (: $outer Type) (-> (: $inner $outer) $outer)))))
   (Pi Type (Pi (Var 0) (Var 1))))

!(println! (PrimeCanonicalBindersSummary 10 10 0))
