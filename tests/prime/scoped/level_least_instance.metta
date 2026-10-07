(set:profile hol)

; A declaration over a level variable is used at its least level instance:
; the least assignment of its level variables at which the arguments have
; the types of the domains.  Where a domain writes (u (+ $l 1)) or
; (u (max $l c)) the kernel finds that least assignment; where none exists
; it abstains, and nothing is refuted.

!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
!(add-atom &s (: Small (u 3)))
!(add-atom &s (: Big (u omega)))

; A successor in a domain.  The argument of g must be a type of
; (u (+ $l 1)).  Small is a type of (u 3), so 3 <= l + 1 and the least l is
; 2: the level under 3 (PredLevelOrder, Level.pred?).
!(add-atom &s (: g (-> (X : (u (+ $l 1))) (u $l))))
!(type:of &s (g Small))
!(type:check &s (g Small) (u 2))
!(type:check &s (g Small) (u 3))
!(type:check &s (g Small) (u 1))
; A limit has no level under it but itself: Big is a type of (u omega), so
; omega <= l + 1 and the least l is omega.
!(type:of &s (g Big))
!(type:check &s (g Big) (u omega))
!(type:check &s (g Big) (u 7))
; Zero is reached by every level: num is a type of (u 0), and l stays 0.
!(type:of &s (g num))
; A long numeral steps back like a short one: Vast is a type of the universe
; at 10^26, so 10^26 <= l + 1 and the least l is the number before 10^26.
; (g Vast) is then a type of the universe at that number, and of none below.
!(add-atom &s (: Vast (u 100000000000000000000000000)))
!(type:of &s (g Vast))
!(type:check &s (g Vast) (u 99999999999999999999999999))
!(type:check &s (g Vast) (u 99999999999999999999999998))

; For comparison, a bare level variable in both places.  Without an expected
; type the least instance is the level of the argument.  An expected type is
; a use as well: (-> (u 5) num) fixes the variable at 5, which the argument
; allows (tests/prime/scoped/level_instance_from_all_uses.metta).
!(add-atom &s (: k (-> (X : (u $l)) (-> (u $l) num))))
!(type:of &s (k Small))
!(type:check &s (k Small) (-> (u 3) num))
!(type:check &s (k Small) (-> (u 5) num))

; A maximum with a closed side.  Where the closed side already reaches the
; level of the argument, nothing is demanded of the variable: num is a type
; of (u 0), 0 <= max(l, 1), and l stays 0.
!(add-atom &s (: wrap (-> (X : (u (max $l 1))) (u (max $l 1)))))
!(type:of &s (wrap num))
!(type:check &s (wrap num) (u 1))
!(type:check &s (wrap num) (u 0))
; Where it does not, the variable is raised to the level of the argument.
!(type:of &s (wrap Small))
!(type:check &s (wrap Small) (u 3))
!(type:check &s (wrap Small) (u 2))
!(type:of &s (wrap Big))
; The same with the closed side written first.
!(add-atom &s (: wrap-left (-> (X : (u (max 1 $l))) (u (max 1 $l)))))
!(type:of &s (wrap-left num))
!(type:of &s (wrap-left Small))
; A successor over a maximum: 3 <= max(l, 1) + 1, so max(l, 1) reaches 2
; and the least l is 2.
!(add-atom &s (: over (-> (X : (u (+ (max $l 1) 1))) (u $l))))
!(type:of &s (over Small))
!(type:of &s (over num))

; A maximum with a long numeral on the closed side: the closed side reaches
; the level of Small, so nothing is demanded of the variable, and the result
; is a type of the universe at the numeral and of none below.
!(add-atom &s (: wide (-> (X : (u (max $l 99999999999999999999999999)))
                          (u (max $l 99999999999999999999999999)))))
!(type:of &s (wide Small))
!(type:check &s (wide Small) (u 99999999999999999999999999))
!(type:check &s (wide Small) (u 99999999999999999999999998))
; Over a level variable (+ $l k) is the level k successors up, for a numeral
; k of any length: one counted successor, judged as a small count is.  Small
; is a type of (u 3), so l is 3, and (far Small) is a type of the universe at
; 3 + k and of none below.
!(add-atom &s (: far (-> (X : (u $l)) (u (+ $l 99999999999999999999999999)))))
!(type:of &s (far Small))
!(try (type:of &s (far Small)))
!(try (type:check &s (far Small) (u 5)))
!(type:check &s (far Small) (u 100000000000000000000000002))
!(type:check &s (far Small) (u 100000000000000000000000001))
!(type:of &s (far Big))
; A counted successor in a domain.  The argument of leap must be a type of
; (u (+ $l 20000)).  Tall is a type of (u 20005), so 20005 <= l + 20000 and
; the least l is 5: the level 20000 under 20005.  Small is a type of (u 3),
; which every l reaches, and l stays 0.  Big is a type of (u omega), a limit,
; under which there is no level but itself: the least l is omega.
!(add-atom &s (: Tall (u 20005)))
!(add-atom &s (: leap (-> (X : (u (+ $l 20000))) (u $l))))
!(type:of &s (leap Tall))
!(type:check &s (leap Tall) (u 5))
!(type:check &s (leap Tall) (u 4))
!(type:of &s (leap Small))
!(type:of &s (leap Big))
; The same with a count of twenty-six digits: Vast is a type of the universe
; at 10^26, the count is the number before 10^26, and the least l is 1.
!(add-atom &s (: hop (-> (X : (u (+ $l 99999999999999999999999999))) (u $l))))
!(type:of &s (hop Vast))
!(type:check &s (hop Vast) (u 1))
!(type:check &s (hop Vast) (u 0))
!(type:of &s (hop Small))
; Counts add, and a count stands over a maximum: Top is a type of
; (u 40007), so 40007 <= l + 20000 + 20000 and the least l is 7; and
; 20005 <= max(l, 1) + 20000 gives the least l 5.
!(add-atom &s (: Top (u 40007)))
!(add-atom &s (: twice (-> (X : (u (+ (+ $l 20000) 20000))) (u (+ $l 1)))))
!(type:of &s (twice Top))
!(type:of &s (twice Tall))
!(add-atom &s (: high (-> (X : (u (+ (max $l 1) 20000))) (u $l))))
!(type:of &s (high Tall))
!(type:of &s (high Small))
; A count of zero is the level itself.
!(add-atom &s (: stay (-> (X : (u (+ $l 0))) (u (+ $l 0)))))
!(type:of &s (stay Small))
; A count is a numeral that is not negative.  With any other count the
; declaration is no schema, and a use of it has no type from it.
!(add-atom &s (: back (-> (X : (u (+ $l -1))) (u $l))))
!(type:of &s (back Small))
!(try (type:of &s (back Small)))
!(add-atom &s (: bend (-> (X : (u (+ $l omega))) (u $l))))
!(try (type:of &s (bend Small)))

; A level variable that no argument bounds is at zero.
!(add-atom &s (: up (-> (X : (u 0)) (u (+ $l 1)))))
!(type:of &s (up num))
!(type:check &s (up num) (u 1))
!(type:check &s (up num) (u 5))
!(type:check &s (up num) (u 0))

; No least instance: a maximum of two level variables is reached by raising
; either.  Where the argument demands nothing the declaration is used at
; zero; where it demands something the judgment is left undetermined.
!(add-atom &s (: both (-> (X : (u (max $l $m))) (u $l))))
!(type:of &s (both num))
!(type:of &s (both Small))
!(try (type:of &s (both Small)))
!(try (type:check &s (both Small) (u 3)))
