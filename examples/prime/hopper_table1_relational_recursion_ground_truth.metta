; Exact qualification for four authored relational-recursion programs.
; Every occurrence retains its proof tree and is checked at its target.

!(import! &self ../../lib/ilp/prime_native_list_types.metta)
!(import! &self ../../lib/ilp/prime_native_list_relator_types.metta)
!(import! &self ../../lib/ilp/prime_relational_combinators_types.metta)
!(import! &self ../../lib/ilp/hopper_table1_first_order_types.metta)
!(import! &self ../../lib/ilp/hopper_table1_relational_recursion_types.metta)
!(import! &self ../../lib/ilp/hopper_table1_relational_recursion_rules.metta)

(= (hopper:rec:proof-checks (quote $goal) $occurrences)
  (collapse
    (let (occurrence $proof-data) (superpose $occurrences)
      (type:check (unquote $proof-data) $goal))))

(= (hopper:rec:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (hopper:table1:relational-recursion:package)
      1024 100000000 65536 (quote $goal))
    (let $count (- (size-atom $occurrences) 1)
      (let $checks (hopper:rec:proof-checks (quote $goal) $occurrences)
        (if (== $count 0)
            (hopper:rec:case $name not-derived $count $checks)
            (hopper:rec:case $name derived $count $checks))))))

!(hopper:rec:classify hopper:repeatn:ho:pos-1
  (quote (hopper:repeat-n:f hopper:nat (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) hopper:nat:n3 (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:nil (list hopper:nat))))))))
!(hopper:rec:classify hopper:repeatn:ho:pos-2
  (quote (hopper:repeat-n:f (list hopper:nat) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:nil (list hopper:nat))))) hopper:nat:n2 (list:cons (list (list hopper:nat)) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:nil (list hopper:nat))))) (list:cons (list (list hopper:nat)) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:nil (list hopper:nat))))) (list:nil (list (list hopper:nat))))))))
!(hopper:rec:classify hopper:repeatn:ho:pos-3
  (quote (hopper:repeat-n:f hopper:nat (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:nil hopper:nat))))) hopper:nat:n4 (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:nil hopper:nat))))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:nil hopper:nat))))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:nil hopper:nat))))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:nil hopper:nat))))) (list:nil (list hopper:nat)))))))))
!(hopper:rec:classify hopper:repeatn:ho:pos-4
  (quote (hopper:repeat-n:f hopper:nat (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))) hopper:nat:n5 (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))) (list:nil (list hopper:nat))))))))))
!(hopper:rec:classify hopper:repeatn:ho:pos-5
  (quote (hopper:repeat-n:f hopper:nat (list:nil hopper:nat) hopper:nat:n2 (list:cons (list hopper:nat) (list:nil hopper:nat) (list:cons (list hopper:nat) (list:nil hopper:nat) (list:nil (list hopper:nat)))))))
!(hopper:rec:classify hopper:repeatn:ho:pos-6
  (quote (hopper:repeat-n:f hopper:nat (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat)) hopper:nat:n0 (list:nil (list hopper:nat)))))
!(hopper:rec:classify hopper:repeatn:ho:pos-7
  (quote (hopper:repeat-n:f hopper:nat (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat)) hopper:nat:n1 (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat)) (list:nil (list hopper:nat))))))
!(hopper:rec:classify hopper:repeatn:ho:neg-8
  (quote (hopper:repeat-n:f hopper:nat (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:cons hopper:nat hopper:nat:n5 (list:nil hopper:nat)))))) hopper:nat:n3 (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:cons hopper:nat hopper:nat:n5 (list:nil hopper:nat)))))) (list:nil (list hopper:nat))))))
!(hopper:rec:classify hopper:repeatn:ho:neg-9
  (quote (hopper:repeat-n:f hopper:nat (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:cons hopper:nat hopper:nat:n5 (list:nil hopper:nat)))))) hopper:nat:n2 (list:nil (list hopper:nat)))))
!(hopper:rec:classify hopper:allseqn:ho:pos-1
  (quote (hopper:all-seq-n:f hopper:nat:n0 (list:nil (list hopper:nat)))))
!(hopper:rec:classify hopper:allseqn:ho:pos-2
  (quote (hopper:all-seq-n:f hopper:nat:n1 (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat)) (list:nil (list hopper:nat))))))
!(hopper:rec:classify hopper:allseqn:ho:pos-3
  (quote (hopper:all-seq-n:f hopper:nat:n2 (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat)) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))) (list:nil (list hopper:nat)))))))
!(hopper:rec:classify hopper:allseqn:ho:pos-4
  (quote (hopper:all-seq-n:f hopper:nat:n3 (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat)) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:nil (list hopper:nat))))))))
!(hopper:rec:classify hopper:allseqn:ho:pos-5
  (quote (hopper:all-seq-n:f hopper:nat:n4 (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat)) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat)))) (list:cons (list hopper:nat) (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:nil hopper:nat))))) (list:nil (list hopper:nat)))))))))
!(hopper:rec:classify hopper:allseqn:ho:neg-6
  (quote (hopper:all-seq-n:f hopper:nat:n2 (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:cons hopper:nat hopper:nat:n15 (list:cons hopper:nat hopper:nat:n21 (list:nil hopper:nat)))))))))
!(hopper:rec:classify hopper:allseqn:ho:neg-7
  (quote (hopper:all-seq-n:f hopper:nat:n5 (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n4 (list:nil hopper:nat)))))))
!(hopper:rec:classify hopper:allseqn:ho:neg-8
  (quote (hopper:all-seq-n:f hopper:nat:n6 (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n6 (list:nil hopper:nat)))))))
!(hopper:rec:classify hopper:firsthalf:ho:pos-1
  (quote (hopper:first-half:f (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)) (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))
!(hopper:rec:classify hopper:firsthalf:ho:pos-2
  (quote (hopper:first-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:nil hopper:atom))) (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))
!(hopper:rec:classify hopper:firsthalf:ho:pos-3
  (quote (hopper:first-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:nil hopper:atom)))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:nil hopper:atom))))))
!(hopper:rec:classify hopper:firsthalf:ho:pos-4
  (quote (hopper:first-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:nil hopper:atom))))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:nil hopper:atom))))))
!(hopper:rec:classify hopper:firsthalf:ho:pos-5
  (quote (hopper:first-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:nil hopper:atom)))))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:nil hopper:atom)))))))
!(hopper:rec:classify hopper:firsthalf:ho:pos-6
  (quote (hopper:first-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:cons hopper:atom hopper:atom:n6 (list:nil hopper:atom))))))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:nil hopper:atom)))))))
!(hopper:rec:classify hopper:firsthalf:ho:pos-7
  (quote (hopper:first-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:cons hopper:atom hopper:atom:n6 (list:cons hopper:atom hopper:atom:n7 (list:nil hopper:atom)))))))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:nil hopper:atom))))))))
!(hopper:rec:classify hopper:firsthalf:ho:pos-8
  (quote (hopper:first-half:f (list:nil hopper:atom) (list:nil hopper:atom))))
!(hopper:rec:classify hopper:firsthalf:ho:neg-9
  (quote (hopper:first-half:f (list:nil hopper:atom) (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))
!(hopper:rec:classify hopper:firsthalf:ho:neg-10
  (quote (hopper:first-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:nil hopper:atom)))))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:nil hopper:atom))))))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-1
  (quote (hopper:mul-from-suc:f hopper:nat:n1 hopper:nat:n1 hopper:nat:n1)))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-2
  (quote (hopper:mul-from-suc:f hopper:nat:n2 hopper:nat:n2 hopper:nat:n4)))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-3
  (quote (hopper:mul-from-suc:f hopper:nat:n2 hopper:nat:n9 hopper:nat:n18)))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-4
  (quote (hopper:mul-from-suc:f hopper:nat:n3 hopper:nat:n3 hopper:nat:n9)))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-5
  (quote (hopper:mul-from-suc:f hopper:nat:n3 hopper:nat:n7 hopper:nat:n21)))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-6
  (quote (hopper:mul-from-suc:f hopper:nat:n4 hopper:nat:n2 hopper:nat:n8)))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-7
  (quote (hopper:mul-from-suc:f hopper:nat:n4 hopper:nat:n4 hopper:nat:n16)))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-8
  (quote (hopper:mul-from-suc:f hopper:nat:n5 hopper:nat:n5 hopper:nat:n25)))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-9
  (quote (hopper:mul-from-suc:f hopper:nat:n5 hopper:nat:n3 hopper:nat:n15)))
!(hopper:rec:classify hopper:mulfromsuc:ho:pos-10
  (quote (hopper:mul-from-suc:f hopper:nat:n6 hopper:nat:n6 hopper:nat:n36)))
!(hopper:rec:classify hopper:mulfromsuc:ho:neg-11
  (quote (hopper:mul-from-suc:f hopper:nat:n1 hopper:nat:n3 hopper:nat:n2)))
!(hopper:rec:classify hopper:mulfromsuc:ho:neg-12
  (quote (hopper:mul-from-suc:f hopper:nat:n2 hopper:nat:n5 hopper:nat:n7)))
!(hopper:rec:classify hopper:mulfromsuc:ho:neg-13
  (quote (hopper:mul-from-suc:f hopper:nat:n3 hopper:nat:n1 hopper:nat:n4)))
!(hopper:rec:classify hopper:mulfromsuc:ho:neg-14
  (quote (hopper:mul-from-suc:f hopper:nat:n4 hopper:nat:n4 hopper:nat:n8)))
!(hopper:rec:classify hopper:mulfromsuc:ho:neg-15
  (quote (hopper:mul-from-suc:f hopper:nat:n5 hopper:nat:n10 hopper:nat:n15)))
!(hopper:rec:classify hopper:mulfromsuc:ho:neg-16
  (quote (hopper:mul-from-suc:f hopper:nat:n6 hopper:nat:n12 hopper:nat:n24)))
!(hopper:rec:classify hopper:firsthalf:ho:zero-step-overlap
  (quote (hopper:first-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:nil hopper:atom)))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:nil hopper:atom)))))))
