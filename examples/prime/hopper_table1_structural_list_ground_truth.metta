; Exact proof-relevant qualification for three structural-list tasks.
; Every output occurrence is checked against its indexed target.

!(import! &self ../../lib/ilp/prime_native_list_types.metta)
!(import! &self ../../lib/ilp/prime_relational_combinators_types.metta)
!(import! &self ../../lib/ilp/hopper_table1_first_order_types.metta)
!(import! &self ../../lib/ilp/hopper_table1_structural_list_types.metta)
!(import! &self ../../lib/ilp/hopper_table1_structural_list_rules.metta)

(= (hopper:struct:proof-checks (quote $goal) $occurrences)
  (collapse
    (let (occurrence $proof-data) (superpose $occurrences)
      (type:check (unquote $proof-data) $goal))))

(= (hopper:struct:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (hopper:table1:structural-list:package)
      1024 20000000 8192 (quote $goal))
    (let $count (- (size-atom $occurrences) 1)
      (let $checks
        (hopper:struct:proof-checks (quote $goal) $occurrences)
        (if (== $count 0)
            (hopper:struct:case $name not-derived $count $checks)
            (hopper:struct:case $name derived $count $checks))))))

!(hopper:struct:classify hopper:lasthalf:ho:pos-1
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)) (list:nil hopper:atom))))
!(hopper:struct:classify hopper:lasthalf:ho:pos-2
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:nil hopper:atom))) (list:cons hopper:atom hopper:atom:n2 (list:nil hopper:atom)))))
!(hopper:struct:classify hopper:lasthalf:ho:pos-3
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:nil hopper:atom)))) (list:cons hopper:atom hopper:atom:n3 (list:nil hopper:atom)))))
!(hopper:struct:classify hopper:lasthalf:ho:pos-4
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:nil hopper:atom))))) (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:nil hopper:atom))))))
!(hopper:struct:classify hopper:lasthalf:ho:pos-5
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:nil hopper:atom)))))) (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:nil hopper:atom))))))
!(hopper:struct:classify hopper:lasthalf:ho:pos-6
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:cons hopper:atom hopper:atom:n6 (list:nil hopper:atom))))))) (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:cons hopper:atom hopper:atom:n6 (list:nil hopper:atom)))))))
!(hopper:struct:classify hopper:lasthalf:ho:pos-7
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:cons hopper:atom hopper:atom:n6 (list:cons hopper:atom hopper:atom:n7 (list:nil hopper:atom)))))))) (list:cons hopper:atom hopper:atom:n5 (list:cons hopper:atom hopper:atom:n6 (list:cons hopper:atom hopper:atom:n7 (list:nil hopper:atom)))))))
!(hopper:struct:classify hopper:lasthalf:ho:pos-8
  (quote (hopper:last-half:f (list:nil hopper:atom) (list:nil hopper:atom))))
!(hopper:struct:classify hopper:lasthalf:ho:neg-9
  (quote (hopper:last-half:f (list:nil hopper:atom) (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))
!(hopper:struct:classify hopper:lasthalf:ho:neg-10
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:nil hopper:atom))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:nil hopper:atom))))))
!(hopper:struct:classify hopper:lasthalf:ho:neg-11
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:nil hopper:atom)))))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:nil hopper:atom)))))))
!(hopper:struct:classify hopper:lasthalf:ho:neg-12
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:nil hopper:atom)))))) (list:nil hopper:atom))))
!(hopper:struct:classify hopper:lasthalf:ho:neg-13
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:nil hopper:atom)))))) (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:nil hopper:atom))))))))
!(hopper:struct:classify hopper:lasthalf:ho:neg-14
  (quote (hopper:last-half:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n5 (list:nil hopper:atom)))))) (list:cons hopper:atom hopper:atom:n5 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))))))
!(hopper:struct:classify hopper:of1and2:ho:pos-1
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:of1and2:ho:pos-2
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:of1and2:ho:pos-3
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:of1and2:ho:pos-4
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:of1and2:ho:neg-5
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n0 (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:of1and2:ho:neg-6
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n0 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:of1and2:ho:neg-7
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n0 (list:cons hopper:nat hopper:nat:n2 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:of1and2:ho:neg-8
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n2 (list:cons hopper:nat hopper:nat:n0 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:of1and2:ho:neg-9
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n1 (list:cons hopper:nat hopper:nat:n3 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:of1and2:ho:neg-10
  (quote (hopper:of-one-and-two:f (list:cons hopper:nat hopper:nat:n3 (list:cons hopper:nat hopper:nat:n1 (list:nil hopper:nat))))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-1
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-2
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-3
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom))))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-4
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-5
  (quote (hopper:palindrome:f (list:nil hopper:atom))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-6
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom))))))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-7
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))))))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-8
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))))))))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-9
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))))))))))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-10
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom))))))))))))))))
!(hopper:struct:classify hopper:ispalindrome:ho:pos-11
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom))))))))))))))))))))))))))))
!(hopper:struct:classify hopper:ispalindrome:ho:neg-12
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))))))
!(hopper:struct:classify hopper:ispalindrome:ho:neg-13
  (quote (hopper:palindrome:f (list:cons hopper:atom hopper:atom:n1 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n4 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n3 (list:cons hopper:atom hopper:atom:n2 (list:cons hopper:atom hopper:atom:n1 (list:nil hopper:atom)))))))))))
