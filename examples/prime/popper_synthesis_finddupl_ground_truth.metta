; Faithful exact-image qualification for Popper's synthesis-finddupl task.
;
; Source: logic-and-learning-lab/Popper revision
; 5c608d3ec87e4975866042fe326e9a317277e021.
; Source file digests (SHA-256):
;   bias.pl c8190331a770d21bb067a34c52077530afadb5649bef0bb19b1d8ed8093c5704
;   bk.pl   c2c391564743a159cf60483322cd87327bb9f32f041b6e754fb982c1c266fd25
;   exs.pl  5dc11265c74fcd6b18081035b4504d91d4b489e1b95bc49f07a1129e3e3a9101
;
; The operational member relation retains one proof path per list occurrence.
; Consequently equal endpoint judgments may have several derivations; the
; classifier checks and reports the complete occurrence bag rather than
; collapsing it to a Boolean or set.

!(import! &self ../../lib/ilp/prime_native_list_types.metta)
!(import! &self ../../lib/ilp/popper_snapshot_numerals.metta)
!(import! &self ../../lib/ilp/popper_native_list_relations.metta)
!(import! &self ../../lib/ilp/popper_benchmark_classify.metta)
!(import! &self ../../lib/ilp/popper_synthesis_finddupl_types.metta)
!(import! &self ../../lib/ilp/popper_synthesis_finddupl_rules.metta)

(= (popper:finddupl:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (popper:finddupl:package) 128 8000000 512 (quote $goal))
    (popper:classify-occurrences
      $name (quote $goal) $occurrences)))

!(popper:finddupl:classify pos-1
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n91 (list:cons popper:numeral popper:n77 (list:cons popper:numeral popper:n16 (list:cons popper:numeral popper:n22 (list:cons popper:numeral popper:n6 (list:cons popper:numeral popper:n55 (list:cons popper:numeral popper:n9 (list:cons popper:numeral popper:n56 (list:cons popper:numeral popper:n73 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n83 (list:cons popper:numeral popper:n14 (list:cons popper:numeral popper:n51 (list:cons popper:numeral popper:n52 (list:cons popper:numeral popper:n84 (list:cons popper:numeral popper:n33 (list:cons popper:numeral popper:n62 (list:cons popper:numeral popper:n16 (list:nil popper:numeral))))))))))))))))))))
      popper:n16)))

!(popper:finddupl:classify pos-2
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n88 (list:nil popper:numeral))))
      popper:n43)))

!(popper:finddupl:classify pos-3
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n23 (list:cons popper:numeral popper:n49 (list:cons popper:numeral popper:n75 (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n82 (list:cons popper:numeral popper:n31 (list:cons popper:numeral popper:n1 (list:cons popper:numeral popper:n59 (list:cons popper:numeral popper:n46 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n46 (list:cons popper:numeral popper:n98 (list:cons popper:numeral popper:n18 (list:cons popper:numeral popper:n22 (list:cons popper:numeral popper:n27 (list:cons popper:numeral popper:n92 (list:cons popper:numeral popper:n65 (list:cons popper:numeral popper:n57 (list:cons popper:numeral popper:n40 (list:cons popper:numeral popper:n16 (list:cons popper:numeral popper:n50 (list:cons popper:numeral popper:n17 (list:cons popper:numeral popper:n1 (list:cons popper:numeral popper:n12 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n59 (list:cons popper:numeral popper:n44 (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n44 (list:cons popper:numeral popper:n11 (list:cons popper:numeral popper:n40 (list:cons popper:numeral popper:n78 (list:cons popper:numeral popper:n88 (list:cons popper:numeral popper:n22 (list:cons popper:numeral popper:n82 (list:cons popper:numeral popper:n88 (list:cons popper:numeral popper:n82 (list:cons popper:numeral popper:n33 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n67 (list:cons popper:numeral popper:n87 (list:cons popper:numeral popper:n53 (list:cons popper:numeral popper:n80 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n82 (list:cons popper:numeral popper:n18 (list:nil popper:numeral))))))))))))))))))))))))))))))))))))))))))))))))
      popper:n82)))

!(popper:finddupl:classify pos-4
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n71 (list:cons popper:numeral popper:n71 (list:cons popper:numeral popper:n8 (list:cons popper:numeral popper:n12 (list:cons popper:numeral popper:n19 (list:nil popper:numeral))))))
      popper:n71)))

!(popper:finddupl:classify pos-5
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n89 (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n27 (list:cons popper:numeral popper:n42 (list:cons popper:numeral popper:n50 (list:cons popper:numeral popper:n98 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n76 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n12 (list:cons popper:numeral popper:n9 (list:cons popper:numeral popper:n36 (list:cons popper:numeral popper:n52 (list:cons popper:numeral popper:n97 (list:cons popper:numeral popper:n11 (list:cons popper:numeral popper:n93 (list:cons popper:numeral popper:n3 (list:cons popper:numeral popper:n53 (list:cons popper:numeral popper:n100 (list:cons popper:numeral popper:n78 (list:cons popper:numeral popper:n101 (list:cons popper:numeral popper:n36 (list:cons popper:numeral popper:n92 (list:cons popper:numeral popper:n87 (list:cons popper:numeral popper:n59 (list:cons popper:numeral popper:n31 (list:cons popper:numeral popper:n68 (list:cons popper:numeral popper:n76 (list:cons popper:numeral popper:n83 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n34 (list:nil popper:numeral))))))))))))))))))))))))))))))))
      popper:n76)))

!(popper:finddupl:classify pos-6
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n62 (list:cons popper:numeral popper:n93 (list:cons popper:numeral popper:n50 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n84 (list:cons popper:numeral popper:n58 (list:cons popper:numeral popper:n74 (list:cons popper:numeral popper:n73 (list:cons popper:numeral popper:n85 (list:cons popper:numeral popper:n84 (list:cons popper:numeral popper:n82 (list:cons popper:numeral popper:n28 (list:cons popper:numeral popper:n86 (list:cons popper:numeral popper:n15 (list:nil popper:numeral)))))))))))))))
      popper:n84)))

!(popper:finddupl:classify pos-7
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n55 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n61 (list:cons popper:numeral popper:n87 (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n69 (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n10 (list:cons popper:numeral popper:n68 (list:cons popper:numeral popper:n56 (list:cons popper:numeral popper:n53 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n98 (list:cons popper:numeral popper:n47 (list:cons popper:numeral popper:n75 (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n55 (list:cons popper:numeral popper:n22 (list:cons popper:numeral popper:n25 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n44 (list:cons popper:numeral popper:n19 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n31 (list:cons popper:numeral popper:n75 (list:cons popper:numeral popper:n6 (list:cons popper:numeral popper:n39 (list:cons popper:numeral popper:n75 (list:cons popper:numeral popper:n101 (list:cons popper:numeral popper:n42 (list:cons popper:numeral popper:n100 (list:cons popper:numeral popper:n19 (list:cons popper:numeral popper:n66 (list:cons popper:numeral popper:n31 (list:cons popper:numeral popper:n86 (list:cons popper:numeral popper:n53 (list:cons popper:numeral popper:n40 (list:cons popper:numeral popper:n23 (list:cons popper:numeral popper:n45 (list:cons popper:numeral popper:n32 (list:cons popper:numeral popper:n2 (list:cons popper:numeral popper:n67 (list:cons popper:numeral popper:n98 (list:nil popper:numeral))))))))))))))))))))))))))))))))))))))))))))))
      popper:n75)))

!(popper:finddupl:classify pos-8
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n79 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n5 (list:cons popper:numeral popper:n82 (list:cons popper:numeral popper:n28 (list:cons popper:numeral popper:n7 (list:cons popper:numeral popper:n7 (list:cons popper:numeral popper:n7 (list:cons popper:numeral popper:n98 (list:cons popper:numeral popper:n23 (list:cons popper:numeral popper:n49 (list:cons popper:numeral popper:n41 (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n75 (list:cons popper:numeral popper:n79 (list:cons popper:numeral popper:n5 (list:cons popper:numeral popper:n66 (list:cons popper:numeral popper:n74 (list:cons popper:numeral popper:n7 (list:cons popper:numeral popper:n33 (list:cons popper:numeral popper:n101 (list:cons popper:numeral popper:n94 (list:cons popper:numeral popper:n18 (list:cons popper:numeral popper:n86 (list:cons popper:numeral popper:n10 (list:cons popper:numeral popper:n78 (list:nil popper:numeral)))))))))))))))))))))))))))
      popper:n7)))

!(popper:finddupl:classify pos-9
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n84 (list:cons popper:numeral popper:n44 (list:cons popper:numeral popper:n84 (list:cons popper:numeral popper:n26 (list:cons popper:numeral popper:n44 (list:cons popper:numeral popper:n74 (list:cons popper:numeral popper:n96 (list:cons popper:numeral popper:n24 (list:cons popper:numeral popper:n79 (list:nil popper:numeral))))))))))
      popper:n84)))

!(popper:finddupl:classify pos-10
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n61 (list:cons popper:numeral popper:n19 (list:cons popper:numeral popper:n59 (list:cons popper:numeral popper:n52 (list:cons popper:numeral popper:n84 (list:cons popper:numeral popper:n23 (list:cons popper:numeral popper:n10 (list:cons popper:numeral popper:n46 (list:cons popper:numeral popper:n1 (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n94 (list:cons popper:numeral popper:n60 (list:cons popper:numeral popper:n13 (list:cons popper:numeral popper:n85 (list:cons popper:numeral popper:n52 (list:cons popper:numeral popper:n16 (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n29 (list:cons popper:numeral popper:n33 (list:cons popper:numeral popper:n14 (list:cons popper:numeral popper:n26 (list:cons popper:numeral popper:n20 (list:cons popper:numeral popper:n44 (list:cons popper:numeral popper:n20 (list:cons popper:numeral popper:n2 (list:cons popper:numeral popper:n8 (list:cons popper:numeral popper:n51 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n70 (list:cons popper:numeral popper:n11 (list:cons popper:numeral popper:n56 (list:cons popper:numeral popper:n69 (list:cons popper:numeral popper:n13 (list:cons popper:numeral popper:n56 (list:cons popper:numeral popper:n9 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n47 (list:cons popper:numeral popper:n33 (list:cons popper:numeral popper:n85 (list:cons popper:numeral popper:n19 (list:cons popper:numeral popper:n74 (list:cons popper:numeral popper:n51 (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n93 (list:cons popper:numeral popper:n79 (list:cons popper:numeral popper:n3 (list:cons popper:numeral popper:n82 (list:cons popper:numeral popper:n23 (list:cons popper:numeral popper:n12 (list:cons popper:numeral popper:n51 (list:cons popper:numeral popper:n23 (list:nil popper:numeral)))))))))))))))))))))))))))))))))))))))))))))))))))))
      popper:n56)))

!(popper:finddupl:classify neg-1
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n76 (list:cons popper:numeral popper:n45 (list:cons popper:numeral popper:n73 (list:cons popper:numeral popper:n41 (list:cons popper:numeral popper:n29 (list:cons popper:numeral popper:n52 (list:cons popper:numeral popper:n78 (list:cons popper:numeral popper:n27 (list:cons popper:numeral popper:n99 (list:cons popper:numeral popper:n79 (list:cons popper:numeral popper:n94 (list:cons popper:numeral popper:n47 (list:cons popper:numeral popper:n48 (list:cons popper:numeral popper:n61 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n33 (list:cons popper:numeral popper:n98 (list:cons popper:numeral popper:n77 (list:cons popper:numeral popper:n70 (list:cons popper:numeral popper:n47 (list:cons popper:numeral popper:n83 (list:cons popper:numeral popper:n88 (list:cons popper:numeral popper:n36 (list:cons popper:numeral popper:n57 (list:cons popper:numeral popper:n3 (list:cons popper:numeral popper:n96 (list:cons popper:numeral popper:n73 (list:cons popper:numeral popper:n97 (list:cons popper:numeral popper:n1 (list:nil popper:numeral))))))))))))))))))))))))))))))
      popper:n57)))

!(popper:finddupl:classify neg-2
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n63 (list:cons popper:numeral popper:n67 (list:cons popper:numeral popper:n24 (list:cons popper:numeral popper:n17 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n63 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n72 (list:cons popper:numeral popper:n21 (list:cons popper:numeral popper:n71 (list:cons popper:numeral popper:n99 (list:cons popper:numeral popper:n60 (list:cons popper:numeral popper:n19 (list:cons popper:numeral popper:n68 (list:cons popper:numeral popper:n90 (list:cons popper:numeral popper:n18 (list:cons popper:numeral popper:n19 (list:cons popper:numeral popper:n11 (list:cons popper:numeral popper:n25 (list:cons popper:numeral popper:n97 (list:cons popper:numeral popper:n86 (list:cons popper:numeral popper:n82 (list:nil popper:numeral)))))))))))))))))))))))
      popper:n37)))

!(popper:finddupl:classify neg-3
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n49 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n15 (list:cons popper:numeral popper:n12 (list:cons popper:numeral popper:n57 (list:cons popper:numeral popper:n8 (list:cons popper:numeral popper:n36 (list:cons popper:numeral popper:n93 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n38 (list:cons popper:numeral popper:n17 (list:cons popper:numeral popper:n1 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n2 (list:cons popper:numeral popper:n80 (list:cons popper:numeral popper:n8 (list:cons popper:numeral popper:n24 (list:cons popper:numeral popper:n87 (list:cons popper:numeral popper:n45 (list:cons popper:numeral popper:n86 (list:cons popper:numeral popper:n46 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n97 (list:cons popper:numeral popper:n70 (list:cons popper:numeral popper:n73 (list:cons popper:numeral popper:n30 (list:cons popper:numeral popper:n62 (list:cons popper:numeral popper:n95 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n33 (list:cons popper:numeral popper:n88 (list:cons popper:numeral popper:n74 (list:cons popper:numeral popper:n18 (list:cons popper:numeral popper:n49 (list:cons popper:numeral popper:n92 (list:cons popper:numeral popper:n38 (list:cons popper:numeral popper:n87 (list:cons popper:numeral popper:n59 (list:cons popper:numeral popper:n5 (list:cons popper:numeral popper:n31 (list:cons popper:numeral popper:n50 (list:nil popper:numeral))))))))))))))))))))))))))))))))))))))))))
      popper:n4)))

!(popper:finddupl:classify neg-4
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n18 (list:cons popper:numeral popper:n18 (list:cons popper:numeral popper:n32 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n27 (list:cons popper:numeral popper:n15 (list:cons popper:numeral popper:n97 (list:cons popper:numeral popper:n15 (list:cons popper:numeral popper:n50 (list:cons popper:numeral popper:n23 (list:cons popper:numeral popper:n79 (list:cons popper:numeral popper:n22 (list:cons popper:numeral popper:n93 (list:cons popper:numeral popper:n48 (list:cons popper:numeral popper:n77 (list:cons popper:numeral popper:n40 (list:cons popper:numeral popper:n87 (list:nil popper:numeral))))))))))))))))))
      popper:n79)))

!(popper:finddupl:classify neg-5
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n8 (list:cons popper:numeral popper:n62 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n92 (list:cons popper:numeral popper:n20 (list:cons popper:numeral popper:n14 (list:cons popper:numeral popper:n28 (list:cons popper:numeral popper:n32 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n89 (list:cons popper:numeral popper:n87 (list:cons popper:numeral popper:n6 (list:cons popper:numeral popper:n2 (list:cons popper:numeral popper:n85 (list:cons popper:numeral popper:n60 (list:cons popper:numeral popper:n96 (list:cons popper:numeral popper:n55 (list:cons popper:numeral popper:n61 (list:cons popper:numeral popper:n12 (list:cons popper:numeral popper:n90 (list:cons popper:numeral popper:n32 (list:cons popper:numeral popper:n90 (list:cons popper:numeral popper:n88 (list:cons popper:numeral popper:n94 (list:cons popper:numeral popper:n74 (list:cons popper:numeral popper:n14 (list:cons popper:numeral popper:n14 (list:cons popper:numeral popper:n12 (list:cons popper:numeral popper:n93 (list:cons popper:numeral popper:n51 (list:cons popper:numeral popper:n99 (list:cons popper:numeral popper:n33 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n21 (list:cons popper:numeral popper:n29 (list:cons popper:numeral popper:n90 (list:cons popper:numeral popper:n99 (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n40 (list:cons popper:numeral popper:n75 (list:nil popper:numeral)))))))))))))))))))))))))))))))))))))))))
      popper:n60)))

!(popper:finddupl:classify neg-6
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n97 (list:cons popper:numeral popper:n68 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n63 (list:cons popper:numeral popper:n99 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n26 (list:cons popper:numeral popper:n36 (list:cons popper:numeral popper:n83 (list:cons popper:numeral popper:n56 (list:cons popper:numeral popper:n76 (list:cons popper:numeral popper:n23 (list:cons popper:numeral popper:n76 (list:cons popper:numeral popper:n31 (list:cons popper:numeral popper:n70 (list:cons popper:numeral popper:n76 (list:nil popper:numeral)))))))))))))))))
      popper:n31)))

!(popper:finddupl:classify neg-7
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n81 (list:cons popper:numeral popper:n76 (list:cons popper:numeral popper:n91 (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n100 (list:cons popper:numeral popper:n8 (list:cons popper:numeral popper:n68 (list:cons popper:numeral popper:n80 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n35 (list:cons popper:numeral popper:n83 (list:cons popper:numeral popper:n39 (list:cons popper:numeral popper:n85 (list:cons popper:numeral popper:n95 (list:cons popper:numeral popper:n26 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n12 (list:cons popper:numeral popper:n78 (list:cons popper:numeral popper:n45 (list:cons popper:numeral popper:n8 (list:cons popper:numeral popper:n59 (list:cons popper:numeral popper:n20 (list:nil popper:numeral)))))))))))))))))))))))
      popper:n80)))

!(popper:finddupl:classify neg-8
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n78 (list:cons popper:numeral popper:n83 (list:cons popper:numeral popper:n46 (list:cons popper:numeral popper:n99 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n84 (list:cons popper:numeral popper:n14 (list:cons popper:numeral popper:n68 (list:cons popper:numeral popper:n27 (list:cons popper:numeral popper:n69 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n1 (list:cons popper:numeral popper:n37 (list:cons popper:numeral popper:n23 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n83 (list:cons popper:numeral popper:n45 (list:cons popper:numeral popper:n92 (list:cons popper:numeral popper:n56 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n78 (list:cons popper:numeral popper:n10 (list:cons popper:numeral popper:n21 (list:cons popper:numeral popper:n70 (list:cons popper:numeral popper:n1 (list:nil popper:numeral))))))))))))))))))))))))))
      popper:n21)))

!(popper:finddupl:classify neg-9
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n12 (list:cons popper:numeral popper:n32 (list:cons popper:numeral popper:n79 (list:cons popper:numeral popper:n36 (list:cons popper:numeral popper:n55 (list:cons popper:numeral popper:n34 (list:cons popper:numeral popper:n94 (list:cons popper:numeral popper:n70 (list:cons popper:numeral popper:n29 (list:cons popper:numeral popper:n31 (list:cons popper:numeral popper:n43 (list:cons popper:numeral popper:n20 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n29 (list:cons popper:numeral popper:n70 (list:cons popper:numeral popper:n97 (list:cons popper:numeral popper:n54 (list:cons popper:numeral popper:n59 (list:cons popper:numeral popper:n93 (list:cons popper:numeral popper:n16 (list:cons popper:numeral popper:n20 (list:cons popper:numeral popper:n79 (list:cons popper:numeral popper:n9 (list:nil popper:numeral))))))))))))))))))))))))
      popper:n9)))

!(popper:finddupl:classify neg-10
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n57 (list:cons popper:numeral popper:n53 (list:cons popper:numeral popper:n27 (list:cons popper:numeral popper:n28 (list:cons popper:numeral popper:n65 (list:cons popper:numeral popper:n65 (list:cons popper:numeral popper:n46 (list:cons popper:numeral popper:n74 (list:cons popper:numeral popper:n9 (list:cons popper:numeral popper:n70 (list:cons popper:numeral popper:n58 (list:cons popper:numeral popper:n98 (list:cons popper:numeral popper:n64 (list:cons popper:numeral popper:n86 (list:cons popper:numeral popper:n88 (list:cons popper:numeral popper:n2 (list:cons popper:numeral popper:n1 (list:cons popper:numeral popper:n21 (list:cons popper:numeral popper:n56 (list:cons popper:numeral popper:n95 (list:cons popper:numeral popper:n95 (list:cons popper:numeral popper:n4 (list:cons popper:numeral popper:n66 (list:cons popper:numeral popper:n65 (list:cons popper:numeral popper:n90 (list:cons popper:numeral popper:n22 (list:cons popper:numeral popper:n66 (list:cons popper:numeral popper:n100 (list:cons popper:numeral popper:n96 (list:cons popper:numeral popper:n49 (list:cons popper:numeral popper:n15 (list:cons popper:numeral popper:n86 (list:nil popper:numeral)))))))))))))))))))))))))))))))))
      popper:n49)))

!(popper:finddupl:classify neg-11
  (quote
    (finddupl:target
      (list:cons popper:numeral popper:n84 (list:cons popper:numeral popper:n44 (list:cons popper:numeral popper:n44 (list:cons popper:numeral popper:n26 (list:cons popper:numeral popper:n44 (list:cons popper:numeral popper:n74 (list:cons popper:numeral popper:n96 (list:cons popper:numeral popper:n24 (list:cons popper:numeral popper:n79 (list:nil popper:numeral))))))))))
      popper:n84)))
