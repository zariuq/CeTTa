; Binding-sensitive controls and result-shape controls for HE consumers.
(= (bindy $v) (unify $v 5 ok nok))
!(case (unify $y 5 ok nok) ((ok $y) (nok none)))
!(switch (unify $y 6 ok nok) ((ok $y) (nok none)))
!(case (bindy $z) ((ok (got $z)) (nok none)))
!(let $r (nop (unify $y 9 ok nok)) ($r $y))
; A scrutinee binding also constrains branch selection, not only its body.
!(case (unify $y 5 ok nok) (($y incorrectly-unbound) (ok correctly-bound)))
; Distinct alternatives keep distinct binding environments.
!(let $answers (collapse (case (superpose ((unify $y 1 ok nok) (unify $y 2 ok nok))) ((ok $y)))) (sort-atom $answers))
; An ordinary function returning unit still has alternatives and errors.
!(nop (empty))
!(collapse (nop (superpose (1 2))))
!(let $answers (collapse (let $_ (nop (superpose ((unify $y 1 ok nok) (unify $y 2 ok nok)))) $y)) (sort-atom $answers))
; Collapse deliberately collects values without exporting inner bindings.
!(let $r (collapse (unify $y 8 ok nok)) ($r $y))
; Ordinary let is the positive binding-propagation control.
!(let $r (unify $y 10 ok nok) ($r $y))
; Keep the explicit error last: file runners may stop after reporting it.
!(nop (Error probe reason))
