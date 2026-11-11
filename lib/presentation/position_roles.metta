; The positions of builtin operations, for the compiled presentations' printers,
; given as the builtins' declared types.
;
; Only Atom and (Data T) positions hold; every other position runs.  A presentation writes an expression in a running position as
; a call, f(a, b), and in a holding position as data, f{a, b} (or f[a, b]);
; reading accepts every look anywhere, so the atoms never depend on the look.
;
; The presentation compiler derives the role of each argument from its type by
; the rule the printer applies to a program's own declarations
; (: f (-> T1 ... Tn R)): Atom and (Data T) hold, every other type runs.  A
; call matches a declaration of its head with as many arguments; an operation
; declared nowhere runs every argument, so the call look is the honest reading
; of an undeclared position.
;
; CeTTa's PeTTa lane declares no types for these operations (get-type answers
; %Undefined%), so they are declared here as that lane evaluates them, and
; tests/petta/presentation_builtin_positions.metta checks every declaration
; against the lane's behaviour.  A position evaluated as code is not Atom here,
; though it is held until the operation runs it (match's template, let's
; body); and get-type, get-metatype and eval run their argument in the PeTTa
; lane, so they are not declared.

; A space operation holds the atom it puts or removes.
(: add-atom (-> SpaceType Atom (->)))
(: remove-atom (-> SpaceType Atom (->)))
; superpose is a special form: its argument is the list written in place,
; held (never a call of its first item), and each item runs.  No type of the
; rule above says so (a parameter declared (List T) receives a list value,
; which is computed), so its position is given as a role: list.
(pres:special-form superpose (list))
; Quotation holds its argument.
(: quote (-> Atom Atom))
; A module is named by data; a token is bound to the value of a computation.
(: import! (-> SpaceType Atom (->)))
(: bind! (-> Atom %Undefined% (->)))
; Operations that bind patterns, when no form of the presentation spells them:
; the patterns are data, the rest is evaluated.
(: match (-> SpaceType Atom %Undefined% %Undefined%))
(: unify (-> Atom Atom %Undefined% %Undefined% %Undefined%))
(: let (-> Atom %Undefined% %Undefined% %Undefined%))
(: chain (-> %Undefined% Atom %Undefined% %Undefined%))
(: |-> (-> Atom %Undefined% %Undefined%))
; transform: its sources are patterns; without a destination its instances
; are evaluated, as match evaluates its template; routed into a destination
; they are made as data.
(: transform (-> SpaceType Atom %Undefined% %Undefined%))
(: transform (-> SpaceType Atom Atom SpaceType %Undefined%))
(: flow:for-match (-> SpaceType Atom %Undefined% %Undefined%))
