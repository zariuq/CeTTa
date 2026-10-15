; HE's get-type answers every typing its type checker derives, without
; evaluating: tuple types through value types, declared types and their
; supertypes, then one application per argument typing. Subtypes are also
; accepted where a function is called.

; One answer per derivation, through a type variable or two declarations.
(: f (-> $t R))
(: b B1)
(: b B2)
!(get-type (f b))
(: g (-> B1 R))
(: g (-> B2 R))
!(get-type (g b))

; A head declared both as a value and as a function: tuples beside the call.
(: v V)
(: v (-> B1 R))
!(get-type (v b))

; Supertypes follow the declared type, in rounds.
(: r2i Pron)
(: r2like Verb)
(: r2music Noun)
(:< (Pron Verb Noun) Statement)
!(get-type (r2i r2like r2music))
(:< R3A R3B)
(:< R3B R3C)
(:< R3C R3D)
(: r3a R3A)
!(get-type r3a)
(: x A)
!(get-type x)
(:< A B)
!(get-type x)

; A call accepts an argument through its supertype.
(: h (-> B C))
(= (h $y) through-supertype)
!(h x)
!(get-type (h x))

; Ill-typed applications have no type; an untyped call is %Undefined%.
(: sg (-> SA SB SC))
(: sA SA)
(: sB SB)
!(get-type ((sg sA) sB))
!(get-type (sg sA))
!(get-type (sg sA sB))
!(get-type (nosuch 1))

; get-type never evaluates: no output from the body, and no type from it.
(= (foo) 1)
!(get-type (foo))
(= (bar) (let $_ (println! type-query-ran-body) 1))
!(get-type (bar))
