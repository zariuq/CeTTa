; A closed universe level is an ordinal below epsilon-zero.  A universe is
; printed with its level in Cantor normal form:
;
;   n                    a numeral
;   omega                also written with the Greek letter
;   (^ omega e)          omega to the power of a closed level e
;   (* omega n)          a power of omega times a numeral n >= 1
;   (* (^ omega e) n)
;   (+ t1 t2 ...)        a sum of such terms, exponents strictly decreasing
;
; Inside (u ...) the symbols +, * and ^ are level syntax: the sum, product
; and power of ordinals.  A level may be written as any expression over
; numerals and omega with these; it is read as the normal form of its value.
; So a normal form reads as itself, and what is printed reads back as the
; same level.

; The type of a universe is the universe one level up: a limit notation gains
; a last term 1, and a last numeral grows by one.
!(type:of (u omega))
!(type:of (u (+ omega 1)))
!(type:of (u (* omega 2)))
!(type:of (u (+ (* omega 2) 3)))
!(type:of (u (^ omega 2)))
!(type:of (u (+ (^ omega 2) omega 1)))
!(type:of (u (+ (^ omega 2) (* omega 3) 5)))
!(type:of (u (^ omega omega)))
!(type:of (u (^ omega (+ omega 1))))
!(type:of (u (* (^ omega (^ omega 2)) 4)))

; The Greek letter is the same level, and is printed as omega.
!(type:of (u ω))
!(type:eq (u ω) (u omega))
!(type:eq (u (+ (^ ω 2) (* ω 3) 5)) (u (+ (^ omega 2) (* omega 3) 5)))

; A sum may be nested: it is the same notation when its terms, read in
; order, have strictly decreasing exponents.
!(type:eq (u (+ (+ (^ omega 2) omega) 1)) (u (+ (^ omega 2) omega 1)))
!(type:eq (u (+ (^ omega 2) (+ omega 1))) (u (+ (^ omega 2) omega 1)))
; A finite level written as a power of omega is that numeral.
!(type:eq (u (^ omega 0)) (u 1))
!(type:eq (u (* (^ omega 0) 5)) (u 5))
!(type:of (u (* (^ omega 0) 5)))

; What is printed reads back as the same level: the printed type of a
; universe is a universe that universe is a term of, and it is the level one
; up, written out.
!(let $t (type:of (u (+ (^ omega 2) (* omega 3) 5)))
   (type:check (u (+ (^ omega 2) (* omega 3) 5)) $t))
!(let $t (type:of (u (^ omega (+ omega 1))))
   (type:eq $t (u (+ (^ omega (+ omega 1)) 1))))
!(let $t (type:of (u (* (^ omega (^ omega 2)) 4)))
   (type:of $t))

; In the kernel a level beyond the numerals is one constant,
; (LevelCantor exponent coefficient remainder) for
; omega^exponent * coefficient + remainder, and a numeral stays
; (LevelConst n).  The query a judgment poses to the kernel shows it, and the
; kernel judges the same constant written directly.
!(type:kernel-query (type:check (u omega) (u (+ (* omega 2) 3))))
!(type:check (PrimeScoped PrimeCtxNil
               (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0))))
             (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 1))))
!(type:of (PrimeScoped PrimeCtxNil
            (Sort (LevelCantor (LevelConst 2) 1
                    (LevelCantor (LevelConst 1) 3 (LevelConst 5))))))
; A constant whose exponents do not decrease, here 1 + omega as a tree, is
; not a level constant: the kernel does not judge it.
!(type:check (PrimeScoped PrimeCtxNil
               (Sort (LevelCantor (LevelConst 0) 1
                       (LevelCantor (LevelConst 1) 1 (LevelConst 0)))))
             (Sort (LevelCantor (LevelConst 1) 2 (LevelConst 0))))

; A closed level need not be written in normal form: the level is the value
; of the sum, product and power of ordinals, read as the normal form of that
; value.  1 + omega is omega: the tree 1 + omega is not a normal form
; (one_add_omega_not_nf), and no level lies where its comparison would put it
; (no_level_between_one_two).  So (u (+ 1 omega)) is (u omega): it is formed,
; its type is (u (+ omega 1)), and (u 0) is a member of it.
!(type:formed (u (+ 1 omega)))
!(try (type:formed (u (+ 1 omega))))
!(try (type:of (u (+ 1 omega))))
!(type:check (u 0) (u (+ 1 omega)))
!(try (type:check (u 0) (u (+ 1 omega))))
; The order of the summands matters: (u omega) is a member of (u (+ omega 1)),
; and not of (u (+ 1 omega)), which is (u omega) itself.
!(type:check (u omega) (u (+ omega 1)))
!(type:check (u omega) (u (+ 1 omega)))
!(type:of (u (+ 1 omega)))
!(type:of (u (+ omega 1)))
; Likewise: omega + omega is (* omega 2), omega + omega^2 is (^ omega 2),
; omega + 1 + 2 is (+ omega 3), omega + 0 is omega, omega * 0 is 0,
; 2 * omega is omega, and 2^omega is omega.
!(try (type:formed (u (+ omega omega))))
!(try (type:formed (u (+ omega (^ omega 2)))))
!(try (type:formed (u (+ omega 1 2))))
!(try (type:formed (u (+ omega 0))))
!(try (type:formed (u (* omega 0))))
!(try (type:formed (u (* 2 omega))))
!(try (type:formed (u (^ 2 omega))))
; A product and a power of a sum: (omega + 1) * 2 is omega * 2 + 1, and
; (omega + 1)^2 is omega^2 + omega + 1.  The type of each universe is the
; universe one level up.
!(type:of (u (* (+ omega 1) 2)))
!(type:of (u (^ (+ omega 1) 2)))
!(type:check (u (* omega 2)) (u (* (+ omega 1) 2)))
!(type:check (u (+ (* omega 2) 1)) (u (* (+ omega 1) 2)))
; A declaration at such a level: its declared type is reported in normal
; form.  Later is a type of (u omega), and so of (u (+ omega 1)); it is not a
; type of (u 5).
!(bind! &later (new-space))
!(add-atom &later (: Later (u (+ 1 omega))))
!(add-atom &later (: later Later))
!(type:of &later Later)
!(type:of &later later)
!(type:check &later Later (u omega))
!(try (type:check &later Later (u omega)))
!(try (type:check &later Later (u (+ omega 1))))
!(type:check &later Later (u 5))

; Not a level at all: this is refused.
!(type:formed (u (+ omega banana)))
!(try (type:formed (u (+ omega banana))))

; A level is a level whatever its size: a numeral of any length, a tower of
; omegas of any height, and a value of any number of terms.  Nothing about a
; level is cut off: it is formed, compared and printed in full.
!(type:formed (u 99999999999999999999999999))
!(try (type:formed (u 99999999999999999999999999)))
!(try (type:formed (u (* omega 99999999999999999999999999))))
!(try (type:check (u 0) (u (+ omega 99999999999999999999999999))))
; The type of the universe at a numeral of twenty-six digits is the universe
; at the next number, and the numeral before it is one level down.
!(type:of (u 99999999999999999999999999))
!(type:of (u (* omega 99999999999999999999999999)))
!(type:check (u 99999999999999999999999998) (u 99999999999999999999999999))
!(type:eq (u (+ 99999999999999999999999998 1)) (u 99999999999999999999999999))
; Negative: a universe is no member of itself or of a lower one, however
; long the numerals; and numerals that differ in their last digit differ.
!(type:check (u 99999999999999999999999999) (u 99999999999999999999999999))
!(type:check (u 99999999999999999999999999) (u 99999999999999999999999998))
!(type:eq (u 99999999999999999999999999) (u 99999999999999999999999998))
!(try (type:check (u 99999999999999999999999999) (u 9223372036854775807)))
; The power of omega + 1 to 5000 has 5001 terms.  It is a level like any
; other: the power to 4999 is below it, it is that power times omega + 1,
; and it lies strictly between omega^5000 and omega^5001.
!(type:formed (u (^ (+ omega 1) 5000)))
!(type:check (u (^ (+ omega 1) 4999)) (u (^ (+ omega 1) 5000)))
!(type:eq (u (^ (+ omega 1) 5000)) (u (* (^ (+ omega 1) 4999) (+ omega 1))))
!(type:check (u (^ omega 5000)) (u (^ (+ omega 1) 5000)))
!(type:check (u (^ (+ omega 1) 5000)) (u (^ omega 5001)))
; Negative: it is not below the power to 4999, and it is not omega^5000.
!(type:check (u (^ (+ omega 1) 5000)) (u (^ (+ omega 1) 4999)))
!(type:eq (u (^ (+ omega 1) 5000)) (u (^ omega 5000)))
!(type:check (u (^ omega 5001)) (u (^ (+ omega 1) 5000)))
; Its constant in the kernel, all 5001 terms of it.
!(try (type:formed (u (^ (+ omega 1) 5000))))
!(try (type:formed (u (^ omega 5000))))
; A declaration at a long numeral: its declared type is reported as written,
; it is a type of that universe and of every higher one, and of no lower
; one.
!(add-atom &later (: Huge (u 99999999999999999999999999)))
!(type:of &later Huge)
!(type:check &later Huge (u 99999999999999999999999999))
!(type:check &later Huge (u 100000000000000000000000000))
!(type:check &later Huge (u omega))
!(type:check &later Huge (u 99999999999999999999999998))
!(try (type:check &later Huge (u 0)))
; A tower of thirty-two omegas lies above omega.
!(type:check (u omega)
   (u
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega omega)))))))))))))))))))))))))))))))))
!(try (type:check (u omega)
   (u
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega omega))))))))))))))))))))))))))))))))))
; So does a tower of thirty-one omegas, and one of forty; and a taller tower
; is above a shorter one, never below it.
!(type:check (u omega)
   (u
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega omega))))))))))))))))))))))))))))))))
!(type:check
   (u
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega omega))))))))))))))))))))))))))))))))
   (u
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega omega)))))))))))))))))))))))))))))))))))))))))
!(type:check
   (u
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega omega))))))))))))))))))))))))))))))))))))))))
   (u
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega (^ omega
      (^ omega (^ omega (^ omega omega)))))))))))))))))))))))))))))))))

; A level whose value no memory holds is still a level: 2 to the power 2^100
; has more than 2^100 binary digits, and omega + 1 to the power of a numeral
; of twenty-six digits has more terms than that numeral says.  The judgment
; is incomplete.  It is never refuted, and it stays as written.
!(type:formed (u (^ 2 (^ 2 100))))
!(try (type:formed (u (^ 2 (^ 2 100)))))
!(try (type:formed (u (^ (+ omega 1) 99999999999999999999999999))))
!(try (type:check (u 0) (u (+ omega (^ 2 (^ 2 100))))))
!(try (type:eq (u (^ 2 (^ 2 100))) (u 0)))
; A declaration at such a level: its declared type is given back as written,
; and a judgment that needs the value of the level is incomplete.
!(add-atom &later (: Beyond (u (^ 2 (^ 2 100)))))
!(type:of &later Beyond)
!(try (type:check &later Beyond (u 0)))
; The same powers to numbers that memory holds are computed.
!(type:of (u (^ 2 (^ 2 7))))
!(type:check (u (^ (+ omega 1) 26)) (u (^ (+ omega 1) 27)))

; The arithmetic of a level counts against the budget of its judgment.  The
; last operand of a judgment may be a number of steps.  omega + 1 to the
; power of a thousand million has more than a thousand million terms, and
; two to the power 2^40 more than a hundred thousand million digits: within
; a hundred thousand steps the judgment is incomplete.  It is never refuted,
; it stays as written, and the program goes on.
!(type:formed (u (^ (+ omega 1) 1000000000)) 100000)
!(type:check (u 0) (u (^ (+ omega 1) 1000000000)) 100000)
!(type:eq (u (^ (+ omega 1) 1000000000)) (u 0) 100000)
!(type:formed (u (^ 2 (^ 2 40))) 100000)
; The power to 5000 takes seventy thousand steps and its universe some more:
; a hundred thousand steps are too few, and ten million are enough.
!(type:formed (u (^ (+ omega 1) 5000)) 100000)
!(type:formed (u (^ (+ omega 1) 5000)) 10000000)
; Within a budget that its level takes a judgment is answered, yes or no.
!(type:formed (u (^ (+ omega 1) 50)) 100000)
!(type:check (u 5) (u (^ (+ omega 1) 50)) 100000)
!(type:check (u (^ (+ omega 1) 50)) (u 5) 100000)
; So is a judgment about a declaration at such a level.
!(add-atom &later (: Far (u (^ (+ omega 1) 1000000000))))
!(type:check &later Later (u (^ (+ omega 1) 1000000000)) 100000)
!(type:check &later Far (u 0) 100000)
!(type:check &later Later (u (^ (+ omega 1) 50)) 100000)
!(type:check &later Later (u 5) 100000)
; A judgment written without a budget gives its level arithmetic a default
; allowance.  The power of omega + 1 to a thousand million takes more: the
; judgment is incomplete for that reason, and the next line still runs.
; The power to twenty thousand fits the allowance and is formed.  A
; declaration at the larger level is reported as written, and a judgment
; that needs the value of its level is incomplete.
!(type:formed (u (^ (+ omega 1) 1000000000)))
!(try (type:formed (u (^ (+ omega 1) 1000000000))))
!(try (type:check (u 0) (u (^ (+ omega 1) 1000000000))))
!(type:formed (u (^ (+ omega 1) 20000)))
!(type:check (u (^ (+ omega 1) 19999)) (u (^ (+ omega 1) 20000)))
!(type:check (u (^ (+ omega 1) 20000)) (u (^ (+ omega 1) 19999)))
!(type:of &later Far)
!(try (type:check &later Far (u 0)))
; A level of a hundred thousand terms fits the allowance too.  However many
; terms a level has, its judgment is answered: the type of the universe and
; a function over it are formed, and a declaration at that level is a member
; of the universe one above it and not of the least one.
!(type:formed (u (^ (+ omega 1) 100000)))
!(type:check (lam (x : (u (^ (+ omega 1) 100000))) x)
             (-> (u (^ (+ omega 1) 100000)) (u (^ (+ omega 1) 100000))))
!(add-atom &later (: Deep (u (^ (+ omega 1) 100000))))
!(type:check &later Deep (u (+ (^ (+ omega 1) 100000) 1)))
!(type:check &later Deep (u 0))

; The kernel has levels above every level an author writes: the level of the
; sort of all sets, (LevelAbove 0), the level of its sort, (LevelAbove 1), and
; so on.  The notation of levels inside (u ...) has no form for them, and
; the kernel's name for one is no level there.
!(try (type:formed (u (LevelAbove 0))))
; In the kernel's own terms they are closed levels like the others.  The
; universe at a written level is a member of the sort above them all, as it
; is of the universe at omega; that sort has a sort of its own; and it is a
; member of the next one, and neither of itself nor of a universe at a
; written level.  These two sorts print by their names, `set` and `class`.
!(try (type:check (PrimeScoped PrimeCtxNil (Sort (LevelConst 0)))
                  (Sort (LevelAbove 0))))
!(try (type:of (PrimeScoped PrimeCtxNil (Sort (LevelAbove 0)))))
!(try (type:check (PrimeScoped PrimeCtxNil (Sort (LevelConst 0)))
                  (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))))
!(try (type:check (PrimeScoped PrimeCtxNil (Sort (LevelAbove 0)))
                  (Sort (LevelAbove 1))))
!(try (type:check (PrimeScoped PrimeCtxNil (Sort (LevelAbove 0)))
                  (Sort (LevelAbove 0))))
!(try (type:check (PrimeScoped PrimeCtxNil (Sort (LevelAbove 0)))
                  (Sort (LevelCantor (LevelConst 1) 1 (LevelConst 0)))))
; A level variable stands for a level an author writes.  So the universe at
; a level variable, raised by any count, is a member of the sort above every
; written level, and that sort is a member of no universe at a level
; variable.
!(try (type:check (PrimeScoped PrimeCtxNil (Sort (LevelParam 0)))
                  (Sort (LevelAbove 0))))
!(try (type:check
        (PrimeScoped PrimeCtxNil
          (Sort (LevelOffset (LevelParam 0) 99999999999999999999999999)))
        (Sort (LevelAbove 0))))
!(try (type:check (PrimeScoped PrimeCtxNil (Sort (LevelAbove 0)))
                  (Sort (LevelParam 0))))
; For the same reason a declaration over a level variable has no instance at
; such a level.  poly is declared over the level variable 0, from the
; universe at that level to itself.  At omega it has its type.  At the level
; of the sort of all sets it has none, alone or applied to that sort, and
; the reason says why.
!(try (type:of
        (PrimeScoped
          (PrimeCtxDecl (DeclConst poly (LevelParam 0))
            (Pi (Sort (LevelParam 0)) (Sort (LevelParam 0))) PrimeCtxNil)
          (DeclConst poly (LevelCantor (LevelConst 1) 1 (LevelConst 0))))))
!(try (type:of
        (PrimeScoped
          (PrimeCtxDecl (DeclConst poly (LevelParam 0))
            (Pi (Sort (LevelParam 0)) (Sort (LevelParam 0))) PrimeCtxNil)
          (DeclConst poly (LevelAbove 0)))))
!(try (type:of
        (PrimeScoped
          (PrimeCtxDecl (DeclConst poly (LevelParam 0))
            (Pi (Sort (LevelParam 0)) (Sort (LevelParam 0))) PrimeCtxNil)
          (App (DeclConst poly (LevelAbove 1)) (Sort (LevelAbove 0))))))
; The counted successor of the kernel, (LevelOffset level count), is the
; level as many successors up as the numeral counts, whatever its length.
!(type:of (PrimeScoped PrimeCtxNil
            (Sort (LevelOffset (LevelConst 3) 99999999999999999999999999))))
!(type:eq (PrimeScoped PrimeCtxNil
            (Sort (LevelOffset (LevelConst 3) 99999999999999999999999999)))
          (PrimeScoped PrimeCtxNil
            (Sort (LevelConst 100000000000000000000000002))))
!(type:eq (PrimeScoped PrimeCtxNil
            (Sort (LevelOffset (LevelConst 3) 99999999999999999999999999)))
          (PrimeScoped PrimeCtxNil
            (Sort (LevelConst 100000000000000000000000003))))
