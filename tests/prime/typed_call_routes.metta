; One typed relation reached by every route a Prime program has: written in
; the body, held by a variable, and named by a meaning that the body or an
; imported runner applies.  Each caller first reads the program's
; declarations, as a learner's candidate search does, and the program
; changes between rounds.  Every route gives the same answers: a declaration
; whose result type leaves the value undetermined accepts it, and a refused
; argument is an error value, never a missing answer.

!(import! &self typed_call_routes_runner.metta)

(: route:sort (u 0))
(: route:person route:sort)
(: route:vocabulary (-> route:sort (-> route:sort (u 1))))
(: route:parent-symbol (route:vocabulary route:person route:person))

(: route:parent (-> u0 (-> u0 u1)))
(: alice u0)
(: bob u0)
(: eve u0)
(: carol u0)
(: alice-bob (route:parent alice bob))
(: alice-eve (route:parent alice eve))
(: alice-carol (route:parent alice carol))

(= (route:meaning route:parent-symbol) route:parent)
(= (route:parent alice) (route:edge bob alice-bob))
(= (route:parent alice) (route:edge eve alice-eve))

(= (route:symbols)
   (match &self (: $symbol (route:vocabulary $source $target)) $symbol))

(= (route:written $who)
   (let $symbols (collapse (route:symbols))
     (collapse
       (let (route:edge $target $evidence) (route:parent $who)
         (route:out $target $evidence)))))

(= (route:variable $who)
   (let $symbols (collapse (route:symbols))
     (collapse
       (let $relation route:parent
         (let (route:edge $target $evidence) ($relation $who)
           (route:out $target $evidence))))))

(= (route:meaning-head $who)
   (let $symbols (collapse (route:symbols))
     (collapse
       (let $relation (route:meaning route:parent-symbol)
         (let (route:edge $target $evidence) ($relation $who)
           (route:out $target $evidence))))))

(= (route:module $who)
   (let $symbols (collapse (route:symbols))
     (collapse (route:run route:parent-symbol $who))))

(: route:box (-> Number route:Boxed))
(= (route:boxed $value)
   (let $symbols (collapse (route:symbols))
     (collapse (route:box $value))))

(= (route:all $who)
   ((route:written $who) (route:variable $who)
    (route:meaning-head $who) (route:module $who)))

; The caller's own effect happens once per call, whatever route its typed
; call takes.
(= (route:noted $who)
   (let $noted (add-atom &self (route:seen $who))
     (route:variable $who)))

; Round one: the program as written.
!(route:module alice)
!(route:all alice)
!(route:noted alice)
!(collapse (match &self (route:seen $who) $who))

; A refused argument is the declaration's error on every route, for a
; function and for a typed constructor alike.
!(route:written 5)
!(route:variable 5)
!(route:boxed 3)
!(route:boxed alice)

; Round two: the program gains an answer after the callers were planned.
!(add-atom &self (= (route:parent alice) (route:edge carol alice-carol)))
!(route:all alice)
!(route:noted alice)
!(collapse (match &self (route:seen $who) $who))

; A caller with no answers gives none on every route.
!(route:all bob)
