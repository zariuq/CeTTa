; A partial application keeps the evidence of its declarations for the
; application written around it, also when the head is written as a form
; that answers the partial application's value: progn, prog1, let, let*,
; once, with_mutex, transaction and match pass it through, as stable-fixed's
; translator does (e4e345f, typed_partial_arguments).  A captured argument is
; then not checked again when a later argument removes its declaration.
(: part-mark A)
(: part-witness A)
(: part-other B)
(: part-evidence-three (-> $t $t Number Number))
(= (part-evidence-three $x $y $n) 9)
(= (part-erase-evidence) (let $_ (remove-atom &self (: part-mark A)) part-witness))
(= (part-restore) (add-atom &self (: part-mark A)))

; Positive: the written head, bare and through each form.
!(collapse (((part-evidence-three part-mark) (part-erase-evidence)) 0))
!(part-restore)
!(collapse (((progn (part-evidence-three part-mark)) (part-erase-evidence)) 0))
!(part-restore)
!(collapse (((prog1 (part-evidence-three part-mark) True) (part-erase-evidence)) 0))
!(part-restore)
!(collapse (((let $unused True (part-evidence-three part-mark)) (part-erase-evidence)) 0))
!(part-restore)
!(collapse (((let* (($u True)) (part-evidence-three part-mark)) (part-erase-evidence)) 0))
!(part-restore)
!(collapse ((once (part-evidence-three part-mark (part-erase-evidence))) 0))
!(part-restore)
!(collapse ((with_mutex &head-form-lock (part-evidence-three part-mark (part-erase-evidence))) 0))
!(part-restore)
!(collapse ((transaction (part-evidence-three part-mark (part-erase-evidence))) 0))
!(part-restore)
!(collapse ((match &self (: part-witness A) (part-evidence-three part-mark (part-erase-evidence))) 0))
!(part-restore)
; Forms nested in the result position pass it through too.
!(collapse (((progn (let $u True (part-evidence-three part-mark))) (part-erase-evidence)) 0))
!(part-restore)
; In an equation's body, which the compiled tier hands to the machine.
(= (part-body-progn) (((progn (part-evidence-three part-mark)) (part-erase-evidence)) 0))
(= (part-body-match) ((match &self (: part-witness A) (part-evidence-three part-mark (part-erase-evidence))) 0))
!(collapse (part-body-progn))
!(part-restore)
!(collapse (part-body-match))
!(part-restore)
; Negative: the evidence still checks the later arguments against it.
!(collapse (((progn (part-evidence-three part-mark)) part-other) 0))
!(collapse (((progn (part-evidence-three part-mark)) part-witness) "bad"))
; Negative: an argument the form discards contributes no evidence.
(: part-owner-source (-> Number String Number))
(= (part-owner-source $x $y) 5)
(: part-owner-target (-> Number Number))
(= (part-owner-target $x) 7)
!(collapse ((progn (part-owner-source 1) part-owner-target) "bad"))
!(collapse ((progn (part-owner-target 1) (part-owner-source 1)) "ok"))
