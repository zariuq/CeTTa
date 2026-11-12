; Typing judgments transcribed from the unit tests of HE's type checker (lib/src/metta/types.rs).
; Each test's space is reproduced with its names prefixed, so that the tests share one space
; without interfering.  Each comment states the Rust assertion; the judgment asks for the types.

; test_check_type: `nonsense` checks at any type; `do` has two declarations.
(: r1do Verb)
(: r1do Aux)
!(get-type r1nonsense)
!(get-type r1do)

; test_check_expr_type: (i like music) checks at (Pron Verb Noun) and, through the subtype
; declaration, at Statement; the annotated expression checks at Quest.
(: r2i Pron)
(: r2like Verb)
(: r2music Noun)
(: (r2do r2you r2like r2music) Quest)
(:< (Pron Verb Noun) Statement)
!(get-type (r2i r2like r2music))
!(get-type (r2do r2you r2like r2music))

; nested_type: a checks at D through a chain of subtypes.
(: r3a R3A)
(:< R3A R3B)
(:< R3B R3C)
(:< R3C R3D)
!(get-type r3a)

; simple_types: (blue balloon) checks at (Color Object).
(: r4blue Color)
(: r4balloon Object)
!(get-type (r4blue r4balloon))

; arrow_allows_specific_type: (a b) is valid, (a c) is not.
(: r5a (-> R5B R5A))
(: r5b R5B)
(: r5c R5C)
!(get-type (r5a r5b))
!(get-type (r5a r5c))

; simple_dep_types: type-indexed families without binders.
(: r6= (-> $t $t Prop))
(: R6Entity Prop)
(: R6Human (-> R6Entity Prop))
(: R6Socrates R6Entity)
(: (R6Human R6Socrates) Prop)
(: R6Plato R6Entity)
(: R6Mortal (-> R6Entity Prop))
(: R6HumansAreMortal (-> (R6Human $t) (R6Mortal $t)))
(: R6Time NotEntity)
(: R6SocratesIsHuman (R6Human R6Socrates))
(: R6SocratesIsMortal (R6Mortal R6Socrates))
; (Human Socrates) and (Human Plato) check at Prop; (Human Time) does not.
!(get-type (R6Human R6Socrates))
!(get-type (R6Human R6Plato))
!(get-type (R6Human R6Time))
; (= Socrates Socrates), (= Socrates Plato) and (= Socrates Untyped) check at Prop;
; (= Socrates Time) does not.
!(get-type (r6= R6Socrates R6Socrates))
!(get-type (r6= R6Socrates R6Plato))
!(get-type (r6= R6Socrates R6Untyped))
!(get-type (r6= R6Socrates R6Time))
; (HumansAreMortal SocratesIsHuman) is valid at (Mortal Socrates); the others are not valid.
!(get-type (R6HumansAreMortal R6SocratesIsHuman))
!(get-type (R6HumansAreMortal (R6Human R6Socrates)))
!(get-type (R6HumansAreMortal (R6Human R6Plato)))
!(get-type (R6HumansAreMortal R6Human))
!(get-type (r6= R6SocratesIsHuman (R6Human R6Socrates)))
!(get-type (r6= R6SocratesIsMortal (R6HumansAreMortal R6SocratesIsHuman)))
!(get-type (r6= (R6Mortal R6Socrates) (R6Mortal R6Plato)))

; dep_types_prop: two premises indexed by the same entity.
(: R7Sam R7Entity)
(: R7Frog (-> R7Entity Prop))
(: R7Green (-> R7Entity Prop))
(: R7Croaks (-> R7Entity Prop))
(: R7GreenAndCroaksIsFrog (-> (R7Green $t) (R7Croaks $t) (R7Frog $t)))
(: R7SamIsGreen (R7Green R7Sam))
(: R7SamCroaks (R7Croaks R7Sam))
!(get-type (R7GreenAndCroaksIsFrog R7SamIsGreen R7SamCroaks))

; arrow_allows_undefined_type and arrow_has_type_of_returned_value.
(: r8a (-> R8B R8A))
(: r8b R8B)
!(get-type (r8a r8undeclared))
!(get-type (r8a r8b))

; nested_arrow_type and nested_return_type.
(: r9a (-> R9B R9A))
(: r9b R9B)
(: r9h (-> (-> R9B R9A) R9C))
(: r9k (-> R9A R9C))
!(get-type (r9h r9a))
!(get-type (r9k (r9a r9b)))

; validate_non_functional_expression and check_type_non_functional_expression.
(: r10a (-> R10C R10D))
(: r10a R10A)
(: r10b R10B)
!(get-type (r10a r10b))

; get_atom_types_tuple: two declarations each, and an undeclared element.
(: r11a R11A)
(: r11a R11AA)
(: r11b R11B)
(: r11b R11BB)
!(get-type (r11a r11b))
!(get-type (r11a r11c))
!(get-type (r11c r11d))

; get_atom_types_function_call_simple: an argument of undefined type is accepted.
(: r12f (-> R12B R12C))
(: r12b R12B)
!(get-type (r12f r12b))
!(get-type (r12f r12a))

; get_atom_types_function_call_meta_types: meta-type parameters read the argument's shape.
(: r13f_atom (-> Atom R13D))
(: r13f_sym (-> Symbol R13D))
(: r13f_expr (-> Expression R13D))
(: r13f_var (-> Variable R13D))
(: r13f_gnd (-> Grounded R13D))
(: r13b R13B)
!(get-type (r13f_atom r13b))
!(get-type (r13f_sym r13b))
!(get-type (r13f_expr r13b))
!(get-type (r13f_var r13b))
!(get-type (r13f_gnd r13b))
!(get-type (r13f_atom (r13b)))
!(get-type (r13f_sym (r13b)))
!(get-type (r13f_expr (r13b)))
!(get-type (r13f_gnd 3))
