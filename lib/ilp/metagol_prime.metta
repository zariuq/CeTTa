; Prime MIL search: a template is an untrusted search proposal; Prime typing
; prunes impossible second-order substitutions before examples execute.
; Example consistency remains the final ILP criterion.
;
; This is intentionally template-first, matching Metagol's metarule discipline.
; A prior unrestricted recursive search-inhabitants prototype was rejected as
; the default because homogeneous relation types can generate a huge redundant
; inhabitant language before examples are consulted.  Templates expose the
; structure that MIL already assumes and keep search finite and inspectable.

!(import! &self metagol_core.metta)

(= (mil:prime:template-candidate
     $bias $template $target-type $fuel)
   (mil:instantiate-template $bias $template))

(= (mil:prime:filled-candidate
     $bias $skeleton $target-type $fuel)
   (mil:fill-holes $bias $skeleton))

(= (mil:prime:consistent-template-candidate
     $bias $sem $examples $template $target-type $fuel)
   (let $program
        (mil:prime:template-candidate
          $bias $template $target-type $fuel)
     (if (mil:consistent $sem $program $examples)
         $program
         (superpose ()))))

(= (mil:prime:consistent-filled-candidate
     $bias $sem $examples $skeleton $target-type $fuel)
   (let $program
        (mil:prime:filled-candidate
          $bias $skeleton $target-type $fuel)
     (if (mil:consistent $sem $program $examples)
         $program
         (superpose ()))))

(= (mil:prime:learn-template-first
     $bias $sem $examples $template $target-type $fuel)
   (select
     (mil:prime:consistent-template-candidate
       $bias $sem $examples $template $target-type $fuel)))

(= (mil:prime:learn-skeleton-first
     $bias $sem $examples $skeleton $target-type $fuel)
   (select
     (mil:prime:consistent-filled-candidate
       $bias $sem $examples $skeleton $target-type $fuel)))

; ---------- Type-directed second-order filling ----------
;
; Instead of enumerating every symbol and rejecting most complete programs,
; the expected relation type is threaded into the skeleton.  Matching over the
; bias space acts as an index over declared relation types.  These functions
; are producers only: the calling problem performs final `type:check` admission
; in its own judgment context.

(= (mil:prime:typed-fill $bias (MIL.Hole $name) (MIL.Rel $a $b))
   (match $bias (: $program (MIL.Rel $a $b)) $program))

(= (mil:prime:typed-fill $bias (MIL.Chain $left $right) (MIL.Rel $a $c))
   (let $left-program
        (mil:prime:typed-fill $bias $left (MIL.Rel $a $b))
     (let $right-program
          (mil:prime:typed-fill $bias $right (MIL.Rel $b $c))
       (MIL.Chain $left-program $right-program))))

(= (mil:prime:typed-fill $bias (MIL.Choice $left $right) (MIL.Rel $a $b))
   (let $left-program
        (mil:prime:typed-fill $bias $left (MIL.Rel $a $b))
     (let $right-program
          (mil:prime:typed-fill $bias $right (MIL.Rel $a $b))
       (MIL.Choice $left-program $right-program))))

(= (mil:prime:typed-fill $bias (MIL.Closure $inner) (MIL.Rel $a $a))
   (MIL.Closure
     (mil:prime:typed-fill $bias $inner (MIL.Rel $a $a))))

(= (mil:prime:typed-fill
     $bias (MIL.HO1 MIL.MapOp $inner)
     (MIL.Rel (MIL.List $a) (MIL.List $b)))
   (MIL.HO1 MIL.MapOp
     (mil:prime:typed-fill $bias $inner (MIL.Rel $a $b))))

; A closed primitive may occur in an LLM-supplied skeleton.  It is retained
; only when its declared type matches the expected relation type exactly.
(= (mil:prime:typed-fill $bias $closed (MIL.Rel $a $b))
   (if (== (get-metatype $closed) Symbol)
       (match $bias (: $closed (MIL.Rel $a $b)) $closed)
       (superpose ())))

(= (mil:prime:typed-filled-candidate
     $bias $skeleton $target-type $fuel)
   (mil:prime:typed-fill $bias $skeleton $target-type))

(= (mil:prime:typed-consistent-filled-candidate
     $bias $sem $examples $skeleton $target-type $fuel)
   (let $program
        (mil:prime:typed-filled-candidate
          $bias $skeleton $target-type $fuel)
     (if (mil:consistent $sem $program $examples)
         $program
         (superpose ()))))

; ---------- Type-directed canonical Metagol templates ----------
;
; These mirror mil:instantiate-template but use relation declarations as the
; second-order index.  They preserve template sharing (notably
; SharedChoiceChain) and therefore provide a fair producer comparison against
; the enumerate-then-check path.

(= (mil:prime:typed-template $bias MIL.T.Primitive (MIL.Rel $a $b))
   (mil:prime:typed-fill $bias (MIL.Hole p) (MIL.Rel $a $b)))

(= (mil:prime:typed-template $bias MIL.T.Chain (MIL.Rel $a $c))
   (let $left (match $bias (: $p (MIL.Rel $a $b)) $p)
     (let $right (match $bias (: $q (MIL.Rel $b $c)) $q)
       (MIL.Chain $left $right))))

(= (mil:prime:typed-template $bias MIL.T.Choice (MIL.Rel $a $b))
   (let $left (match $bias (: $p (MIL.Rel $a $b)) $p)
     (let $right (match $bias (: $q (MIL.Rel $a $b)) $q)
       (MIL.Choice $left $right))))

; The canonical Metagol grandparent template invents one shared relation and
; uses it on both sides of a chain.  In the dyadic homogeneous case, typing
; proves all four occurrences have one relation type before examples execute.
(= (mil:prime:typed-template $bias MIL.T.SharedChoiceChain (MIL.Rel $a $a))
   (let* (($left (match $bias (: $p (MIL.Rel $a $a)) $p))
          ($right (match $bias (: $q (MIL.Rel $a $a)) $q)))
     (MIL.Chain
       (MIL.Choice $left $right)
       (MIL.Choice $left $right))))

(= (mil:prime:typed-template $bias MIL.T.Closure (MIL.Rel $a $a))
   (MIL.Closure (match $bias (: $p (MIL.Rel $a $a)) $p)))

(= (mil:prime:typed-template
     $bias (MIL.T.HO1 MIL.MapOp)
     (MIL.Rel (MIL.List $a) (MIL.List $b)))
   (MIL.HO1 MIL.MapOp
     (match $bias (: $p (MIL.Rel $a $b)) $p)))

(= (mil:prime:typed-template
     $bias (MIL.T.HO1Chain MIL.MapOp)
     (MIL.Rel (MIL.List $a) (MIL.List $c)))
   (let $left (match $bias (: $p (MIL.Rel $a $b)) $p)
     (let $right (match $bias (: $q (MIL.Rel $b $c)) $q)
       (MIL.HO1 MIL.MapOp (MIL.Chain $left $right)))))

(= (mil:prime:typed-template
     $bias (MIL.T.HO1Chain3 MIL.MapOp)
     (MIL.Rel (MIL.List $a) (MIL.List $d)))
   (let $first (match $bias (: $p (MIL.Rel $a $b)) $p)
     (let $second (match $bias (: $q (MIL.Rel $b $c)) $q)
       (let $third (match $bias (: $r (MIL.Rel $c $d)) $r)
         (MIL.HO1 MIL.MapOp
           (MIL.Chain $first (MIL.Chain $second $third)))))))

(= (mil:prime:typed-template-candidate $bias $template $target-type $fuel)
   (mil:prime:typed-template $bias $template $target-type))

(= (mil:prime:typed-consistent-template-candidate
     $bias $sem $examples $template $target-type $fuel)
   (let $program
        (mil:prime:typed-template-candidate
          $bias $template $target-type $fuel)
     (if (mil:consistent $sem $program $examples)
         $program
         (superpose ()))))
