; Conversion verdicts need operands that the declarations type.
!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))

; An undeclared head is not a typed term, so no verdict is minted.
!(type:eq &self (undeclaredF zero) (undeclaredF zero))

; Ordinary equations compute at run time but are not definitional, so
; conversion leaves their calls unresolved.
(= (dbl zero) zero)
(= (dbl (succ $n)) (succ (succ (dbl $n))))
!(dbl (succ zero))
!(type:eq &self (dbl (succ zero)) (succ (succ zero)))

; A definition that is only tried is not published.
!(let $tried (try (set:define &self twice (-> nat nat)
     (= (twice zero) zero)
     (= (twice (succ $n)) (succ (succ (twice $n))))))
   (car-atom $tried))
!(try (type:eq &self (twice (succ zero)) (succ (succ zero))))

; Once published, the definition decides both ways.
!(set:define &self twice (-> nat nat)
   (= (twice zero) zero)
   (= (twice (succ $n)) (succ (succ (twice $n)))))
!(type:eq &self (twice (succ zero)) (succ (succ zero)))
!(type:eq &self (twice (succ zero)) (succ zero))

; Grounded literals of one kind are distinct constants of one carrier.
!(type:eq 7 7)
!(type:eq 7 8)
!(type:eq "a" "a")
!(type:eq "a" "b")
!(type:eq True False)
!(type:eq 1.5 2.5)
; The carriers of the literal kinds are distinct constants too.
!(type:eq Number Number)
!(type:eq Number String)
; Literals of different kinds share no specified carrier.
!(type:eq 7 7.0)
; A grounded operation is not a conversion rule.
!(type:eq (+ 1 1) 2)

; Constants declared at a kernel universe are compared by the kernel.
(: Colour (u 0))
(: red Colour)
(: blue Colour)
!(type:eq red red)
!(type:eq red blue)

; Declarations at the ambient Type are not kernel declarations.
(: Shape Type)
(: circle Shape)
(: square Shape)
!(type:eq circle circle)
!(type:eq circle square)

; A represented kernel spelling written as source is not a declared term.
!(type:eq (App (DeclConst red) (DeclConst blue))
          (App (DeclConst red) (DeclConst blue)))
