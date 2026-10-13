(hol:profile core)

; A program may name its own constants with the words of the term syntax
; (lam, app, fst, pair) and of the kernel's own spelling (App, Lam, FVar,
; Pi, U0).  Such a name is an ordinary name: declared, it is applied like any
; other constant, in judgments and when run, and it prints as itself.  The
; term syntax spelled the same is read only where no declaration of that name
; takes the arguments given, so a constructor `lam` of one argument and the
; lambdas of the same theory live side by side.
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile core)) $profile-space)))
!(hol:inductive &s nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:inductive &s bool (u 0) (: true bool) (: false bool))

; The names once refused are declared.
!(hol:inductive &s term (u 0) (: var (-> nat term)) (: lam (-> term term)))
!(hol:define &s app (-> nat nat) (= (app $n) $n))
!(hol:define &s fst (-> nat nat) (= (fst $n) $n))
!(hol:inductive &s form (u 0) (: FVar (-> nat form)) (: Bot form))
!(hol:define &s App (-> nat nat) (= (App $n) $n))
!(hol:inductive &s U0 (u 0) (: only U0))
!(hol:inductive &s Pi (u 0) (: Sort (-> nat Pi)) (: LevelConst Pi))

; In judgments they are the program's constants.
!(type:check &s (lam (var zero)) term)
!(type:of &s (lam (var zero)))
!(type:eq &s (app (succ zero)) (succ zero))
!(hol:eq &s (fst (succ zero)) (succ zero))
!(type:eq &s (App zero) zero)
!(type:check &s (FVar zero) form)
!(type:of &s only)
!(type:of &s (Sort zero))
!(type:check &s LevelConst Pi)
; A lambda of the same theory is still a lambda.
!(type:check &s (lam x x) (-> term term))
!(type:eq &s ((lam x (lam x)) (var zero)) (lam (var zero)))

; When run, they compute as their equations say.
!(hol:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:inductive &self box2 (u 0) (: pair (-> nat nat box2)))
!(hol:define &self fst (-> box2 nat) (= (fst (pair $a $b)) $b))
!(fst (pair zero (succ zero)))
!(type:eq &self (fst (pair zero (succ zero))) (succ zero))
!(hol:define &self App (-> nat nat nat)
   (= (App $x zero) $x)
   (= (App $x (succ $y)) (succ (App $x $y))))
!(App (succ zero) (succ zero))
!(hol:define &self app (-> nat nat) (= (app zero) (succ zero)) (= (app (succ $n)) $n))
!(app zero)
!(hol:define &self idx (-> nat nat) (= (idx zero) zero) (= (idx (succ $n)) $n))
!(idx (succ zero))
!(type:eq &self (idx (succ zero)) zero)
