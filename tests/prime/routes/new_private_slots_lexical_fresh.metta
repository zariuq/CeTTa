; `(new ($h ...) body)` under lexical-fresh: the written form of a private
; hole.  The first five rows are the translations from rule M that need
; `new` (docs/prime/scope-lexical-fresh-translations.json, rows H1, H2, 10a,
; 10b and 11i): under lexical-fresh each gives the answer rule M gives on its
; source (TemplateScope's run_toLexical).  The sources follow, under
; lexical-fresh too: a name only the body holds is one slot of the scope
; around, so H1, H2, 10b and 11i answer otherwise.
(scope:profile lexical-fresh)

; ---- The translations: rule M's answers.
!(row H1)
!(let $f (lam z (new ($hole) $hole)) (Pair ($f 1) ($f 2)))
!(row H2)
!(let $f (lam z (new ($hole) $hole)) (let $a ($f 1) (let $b ($f 2) (unify $a Left (unify $b Right (Pair $a $b) (empty)) (empty)))))
!(row 10a)
!((lam z (new ($y) (Pair (let $y z $y){$y} ((lam w (let $y w $y){$y}) 3)))) 3)
!(row 10b)
!((lam z (new ($y) (Pair (let $y z $y){$y} ((lam w (let $y w $y){$y}) 3)))) 4)
!(row 11i)
!((lam w (new ($y) (Pair ((lam z (let $y z (g $y)){$y}) 1) (let $y w $y){$y}))) 5)

; ---- The sources, under lexical-fresh: its own answers.
!(row H1-source)
!(let $f (lam z $hole) (Pair ($f 1) ($f 2)))
!(row H2-source)
!(let $f (lam z $hole) (let $a ($f 1) (let $b ($f 2) (unify $a Left (unify $b Right (Pair $a $b) (empty)) (empty)))))
!(row 10a-source)
!((lam z (Pair (let $y z $y) ((lam w (let $y w $y)) 3))) 3)
!(row 10b-source)
!((lam z (Pair (let $y z $y) ((lam w (let $y w $y)) 3))) 4)
!(row 11i-source)
!((lam w (Pair ((lam z (let $y z (g $y))) 1) (let $y w $y))) 5)
