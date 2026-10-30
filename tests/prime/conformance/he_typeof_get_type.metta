; `type:of` and `get-type` read HE-style declarations through the same HOL
; contract, so where both read the declarations alike they answer alike.  Each
; line asks both, `get-type` first:
;
;   (Law case (get-type answers) (type:of answers))
;
; The verdicts correspond: types established by `type:of` are the types
; `get-type` answers, the same bag with its multiplicity; a refutation, which
; `type:of` reports as False, is no type for `get-type`; and a judgment that
; `type:of` leaves unevaluated is `%Undefined%` for `get-type`.

(: A Type)
(: B Type)
(: Nat Type)
(: Bool Type)
(: List Type)
(: a A)
(: Z Nat)
(: Truth Bool)
(: f (-> A B))
(: g (-> A B Nat))
(: c A)
(: c B)
(: pick (-> A Nat))
(: pick (-> B Nat))
(: nil (List $t))
(: cons (-> $t (List $t) (List $t)))
(type-scheme nil)
(type-scheme cons)

; Both establish: the same types, one per derivation.
!(Law constant (collapse (get-type a)) (collapse (type:of &self a)))
!(Law arrow (collapse (get-type (f a))) (collapse (type:of &self (f a))))
!(Law literal (collapse (get-type 7)) (collapse (type:of &self 7)))
!(Law overload (collapse (get-type c)) (collapse (type:of &self c)))
!(Law overloaded-call (collapse (get-type (pick c))) (collapse (type:of &self (pick c))))
!(Law scheme (collapse (get-type (cons Z nil))) (collapse (type:of &self (cons Z nil))))
!(Law tuple (collapse (get-type (Truth Z))) (collapse (type:of &self (Truth Z))))

; Both refute: no type, and False.
!(Law partial (collapse (get-type (g a))) (collapse (type:of &self (g a))))
!(Law ill-typed (collapse (get-type (f Z))) (collapse (type:of &self (f Z))))
!(Law ill-typed-scheme (collapse (get-type (cons Z Z))) (collapse (type:of &self (cons Z Z))))

; Neither decides: the contract has no subtyping.
(: A2 Type)
(: B2 Type)
(:< A2 B2)
(: x2 A2)
!(Law subtype (collapse (get-type x2)) (collapse (type:of &self x2)))

; Outside the overlap, where `get-type` reads what `type:of` does not.  An
; open declaration that no `type-scheme` marks keeps its variables rigid for
; Prime's judgments; `get-type` reads every declaration as a scheme, as HE
; does.
(: h (-> $t B))
!(Law rigid (collapse (get-type (h a))) (collapse (type:of &self (h a))))

; An argument that nothing types is HE's `%Undefined%`, consistent with every
; parameter: `get-type` answers that gradual reading, and `type:of`, which
; answers only the contract's typings, does not.
!(Law gradual (collapse (get-type (f mystery))) (collapse (type:of &self (f mystery))))

; A parameter of meta-type `Atom` accepts every atom in HE; the contract has no
; type `Atom`.
(: m (-> Proc B))
!(Law meta-type (collapse (get-type (m a))) (collapse (type:of &self (m a))))

; `pair` is a term former Prime's kernel reserves: a native route owns
; `(pair a a)` and abstains on the program's own declaration, and `get-type`
; abstains with it.  This is a native-former name collision.
(: P (-> $x $y Type))
(: pair (-> $x $y (P $x $y)))
(type-scheme pair)
!(Law native-former-collision (collapse (get-type (pair a a))) (collapse (type:of &self (pair a a))))

; A type name stands as written: `Pair` is the program's own family.
(: Pair (-> $x $y Type))
(: twin (-> $x (Pair $x $x)))
(type-scheme twin)
!(Law own-type-name (collapse (get-type (twin a))) (collapse (type:of &self (twin a))))

; A binder `(: $x A)` names its argument: both judgments read it through the
; hosted LF kernel.
(: Fam2 (-> A Type))
(: pick2 (-> (: $x A) (Fam2 $x)))
!(Law dependent (collapse (get-type (pick2 a))) (collapse (type:of &self (pick2 a))))
