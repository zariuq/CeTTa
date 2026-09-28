; The words of the language's own term syntax are not names a declaration
; may take.  The authored forms (lam, app, fst, pair, ...) are read as syntax
; where a name heads an application, so no constructor or definition may
; take one; the kernel's spelling (App, Lam, FVar, U0, ...) is read as syntax
; everywhere, so no declaration may take one.  Each is refused with the name,
; and nothing of the declaration is published.
!(bind! &s (new-space))
!(set:inductive &s nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:inductive &s bool (u 0) (: true bool) (: false bool))

!(try (set:inductive &s term (u 0) (: var (-> nat term)) (: lam (-> term term))))
!(try (set:define &s app (-> nat nat) (= (app $n) $n)))
!(try (set:define &s fst (-> nat nat) (= (fst $n) $n)))
!(try (set:inductive &s form (u 0) (: FVar (-> nat form)) (: Bot form)))
!(try (set:define &s App (-> nat nat) (= (App $n) $n)))
!(try (set:inductive &s U0 (u 0) (: only U0)))
!(== (collapse (match &s (: lam $t) $t)) ())
!(== (collapse (match &s (: fst $t) $t)) ())
!(== (collapse (match &s (: form $t) $t)) ())

; A datatype is not applied: it may be called pair.  Names beside the
; vocabulary compute as any other, in judgments and when run.
!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:inductive &self bool (u 0) (: true bool) (: false bool))
!(set:inductive &self pair (u 0) (: mkpair (-> nat bool pair)))
!(set:define &self first (-> pair nat) (= (first (mkpair $a $b)) $a))
!(first (mkpair (succ zero) true))
!(type:eq &self (first (mkpair (succ zero) true)) (succ zero))
!(set:inductive &self frm (u 0) (: Fv (-> nat frm)) (: Fb frm))
!(set:define &self var-of (-> nat frm) (= (var-of $n) (Fv $n)))
!(var-of zero)
!(type:eq &self (var-of zero) (Fv zero))
