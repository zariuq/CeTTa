; Exact proof-relevant qualification for IGGP scissors-paper-stone.
;
; Canonical GDL: games/scissors_paper_stone.txt
; SHA-256: fe401ac80704e5b138a48b80d6d2bd171427456245e72e555614efb96351710a
; The excluded Prolog projection changes the canonical scoring rules.
; Every train, validate, and test atom occurrence is classified.
; Intermediate opponents and actions remain visible in proof data.

!(import! &self ../../lib/ilp/iggp_scissors_paper_stone_types.metta)
!(import! &self ../../lib/ilp/iggp_scissors_paper_stone_rules.metta)
!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)

(= (iggp:sps:classify $name $episode (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (sps:package $episode) 32 2000000 256 (quote $goal))
    (iggp:classify-occurrences
      $name (quote $goal) $occurrences)))

(= (sps:outcome-proof-shape (unquote (quote $proof)))
   (sps:outcome-proof-shape $proof))
(= (sps:outcome-proof-shape
     (sps:proof:draws $state $player $other $action
       $player-does $other-does $apart))
   (shared-action $other $action))
(= (sps:outcome-proof-shape
     (sps:proof:wins $state $player $other $own $other-action
       $player-does $other-does $apart $beats-proof))
   (versus $other $own $other-action))
(= (sps:outcome-proof-shape
     (sps:proof:loses $state $player $other $own $other-action
       $player-does $other-does $apart $beats-proof))
   (versus $other $own $other-action))
(= (sps:proof-shape
     (sps:proof:next-score-draw $state $player $score
       $score-proof $outcome-proof))
   (score-via-draw $player $score
     (sps:outcome-proof-shape $outcome-proof)))
(= (sps:proof-shape
     (sps:proof:next-score-loss $state $player $score
       $score-proof $outcome-proof))
   (score-via-loss $player $score
     (sps:outcome-proof-shape $outcome-proof)))
(= (sps:proof-shape
     (sps:proof:next-score-win $state $player $previous $score
       $score-proof $successor-proof $outcome-proof))
   (score-via-win $player $previous $score
     (sps:outcome-proof-shape $outcome-proof)))
(= (iggp:sps:proof-shapes $episode (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (sps:package $episode) 32 2000000 256 (quote $goal))
    (collapse
      (let (occurrence $proof-data) (superpose $occurrences)
        (let (quote $proof) $proof-data
          (sps:proof-shape $proof))))))

!(iggp:sps:classify sps:goal:train:state-1:atom-1 sps:goal:train:state-1
  (quote (sps:goal sps:goal:train:state-1 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-1:atom-2 sps:goal:train:state-1
  (quote (sps:goal sps:goal:train:state-1 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-1:atom-3 sps:goal:train:state-1
  (quote (sps:goal sps:goal:train:state-1 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-1:atom-4 sps:goal:train:state-1
  (quote (sps:goal sps:goal:train:state-1 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-1:atom-5 sps:goal:train:state-1
  (quote (sps:goal sps:goal:train:state-1 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-1:atom-6 sps:goal:train:state-1
  (quote (sps:goal sps:goal:train:state-1 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-1:atom-7 sps:goal:train:state-1
  (quote (sps:goal sps:goal:train:state-1 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-1:atom-8 sps:goal:train:state-1
  (quote (sps:goal sps:goal:train:state-1 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-2:atom-1 sps:goal:train:state-2
  (quote (sps:goal sps:goal:train:state-2 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-2:atom-2 sps:goal:train:state-2
  (quote (sps:goal sps:goal:train:state-2 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-2:atom-3 sps:goal:train:state-2
  (quote (sps:goal sps:goal:train:state-2 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-2:atom-4 sps:goal:train:state-2
  (quote (sps:goal sps:goal:train:state-2 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-2:atom-5 sps:goal:train:state-2
  (quote (sps:goal sps:goal:train:state-2 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-2:atom-6 sps:goal:train:state-2
  (quote (sps:goal sps:goal:train:state-2 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-2:atom-7 sps:goal:train:state-2
  (quote (sps:goal sps:goal:train:state-2 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-2:atom-8 sps:goal:train:state-2
  (quote (sps:goal sps:goal:train:state-2 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-3:atom-1 sps:goal:train:state-3
  (quote (sps:goal sps:goal:train:state-3 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-3:atom-2 sps:goal:train:state-3
  (quote (sps:goal sps:goal:train:state-3 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-3:atom-3 sps:goal:train:state-3
  (quote (sps:goal sps:goal:train:state-3 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-3:atom-4 sps:goal:train:state-3
  (quote (sps:goal sps:goal:train:state-3 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-3:atom-5 sps:goal:train:state-3
  (quote (sps:goal sps:goal:train:state-3 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-3:atom-6 sps:goal:train:state-3
  (quote (sps:goal sps:goal:train:state-3 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-3:atom-7 sps:goal:train:state-3
  (quote (sps:goal sps:goal:train:state-3 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-3:atom-8 sps:goal:train:state-3
  (quote (sps:goal sps:goal:train:state-3 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-4:atom-1 sps:goal:train:state-4
  (quote (sps:goal sps:goal:train:state-4 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-4:atom-2 sps:goal:train:state-4
  (quote (sps:goal sps:goal:train:state-4 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-4:atom-3 sps:goal:train:state-4
  (quote (sps:goal sps:goal:train:state-4 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-4:atom-4 sps:goal:train:state-4
  (quote (sps:goal sps:goal:train:state-4 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-4:atom-5 sps:goal:train:state-4
  (quote (sps:goal sps:goal:train:state-4 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-4:atom-6 sps:goal:train:state-4
  (quote (sps:goal sps:goal:train:state-4 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-4:atom-7 sps:goal:train:state-4
  (quote (sps:goal sps:goal:train:state-4 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-4:atom-8 sps:goal:train:state-4
  (quote (sps:goal sps:goal:train:state-4 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-5:atom-1 sps:goal:train:state-5
  (quote (sps:goal sps:goal:train:state-5 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-5:atom-2 sps:goal:train:state-5
  (quote (sps:goal sps:goal:train:state-5 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-5:atom-3 sps:goal:train:state-5
  (quote (sps:goal sps:goal:train:state-5 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-5:atom-4 sps:goal:train:state-5
  (quote (sps:goal sps:goal:train:state-5 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-5:atom-5 sps:goal:train:state-5
  (quote (sps:goal sps:goal:train:state-5 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-5:atom-6 sps:goal:train:state-5
  (quote (sps:goal sps:goal:train:state-5 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-5:atom-7 sps:goal:train:state-5
  (quote (sps:goal sps:goal:train:state-5 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-5:atom-8 sps:goal:train:state-5
  (quote (sps:goal sps:goal:train:state-5 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-6:atom-1 sps:goal:train:state-6
  (quote (sps:goal sps:goal:train:state-6 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-6:atom-2 sps:goal:train:state-6
  (quote (sps:goal sps:goal:train:state-6 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-6:atom-3 sps:goal:train:state-6
  (quote (sps:goal sps:goal:train:state-6 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-6:atom-4 sps:goal:train:state-6
  (quote (sps:goal sps:goal:train:state-6 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-6:atom-5 sps:goal:train:state-6
  (quote (sps:goal sps:goal:train:state-6 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-6:atom-6 sps:goal:train:state-6
  (quote (sps:goal sps:goal:train:state-6 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-6:atom-7 sps:goal:train:state-6
  (quote (sps:goal sps:goal:train:state-6 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-6:atom-8 sps:goal:train:state-6
  (quote (sps:goal sps:goal:train:state-6 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-7:atom-1 sps:goal:train:state-7
  (quote (sps:goal sps:goal:train:state-7 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-7:atom-2 sps:goal:train:state-7
  (quote (sps:goal sps:goal:train:state-7 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-7:atom-3 sps:goal:train:state-7
  (quote (sps:goal sps:goal:train:state-7 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-7:atom-4 sps:goal:train:state-7
  (quote (sps:goal sps:goal:train:state-7 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-7:atom-5 sps:goal:train:state-7
  (quote (sps:goal sps:goal:train:state-7 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-7:atom-6 sps:goal:train:state-7
  (quote (sps:goal sps:goal:train:state-7 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-7:atom-7 sps:goal:train:state-7
  (quote (sps:goal sps:goal:train:state-7 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-7:atom-8 sps:goal:train:state-7
  (quote (sps:goal sps:goal:train:state-7 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-8:atom-1 sps:goal:train:state-8
  (quote (sps:goal sps:goal:train:state-8 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-8:atom-2 sps:goal:train:state-8
  (quote (sps:goal sps:goal:train:state-8 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-8:atom-3 sps:goal:train:state-8
  (quote (sps:goal sps:goal:train:state-8 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-8:atom-4 sps:goal:train:state-8
  (quote (sps:goal sps:goal:train:state-8 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-8:atom-5 sps:goal:train:state-8
  (quote (sps:goal sps:goal:train:state-8 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-8:atom-6 sps:goal:train:state-8
  (quote (sps:goal sps:goal:train:state-8 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-8:atom-7 sps:goal:train:state-8
  (quote (sps:goal sps:goal:train:state-8 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-8:atom-8 sps:goal:train:state-8
  (quote (sps:goal sps:goal:train:state-8 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-9:atom-1 sps:goal:train:state-9
  (quote (sps:goal sps:goal:train:state-9 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-9:atom-2 sps:goal:train:state-9
  (quote (sps:goal sps:goal:train:state-9 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-9:atom-3 sps:goal:train:state-9
  (quote (sps:goal sps:goal:train:state-9 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-9:atom-4 sps:goal:train:state-9
  (quote (sps:goal sps:goal:train:state-9 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-9:atom-5 sps:goal:train:state-9
  (quote (sps:goal sps:goal:train:state-9 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-9:atom-6 sps:goal:train:state-9
  (quote (sps:goal sps:goal:train:state-9 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-9:atom-7 sps:goal:train:state-9
  (quote (sps:goal sps:goal:train:state-9 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-9:atom-8 sps:goal:train:state-9
  (quote (sps:goal sps:goal:train:state-9 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-10:atom-1 sps:goal:train:state-10
  (quote (sps:goal sps:goal:train:state-10 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-10:atom-2 sps:goal:train:state-10
  (quote (sps:goal sps:goal:train:state-10 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-10:atom-3 sps:goal:train:state-10
  (quote (sps:goal sps:goal:train:state-10 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-10:atom-4 sps:goal:train:state-10
  (quote (sps:goal sps:goal:train:state-10 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-10:atom-5 sps:goal:train:state-10
  (quote (sps:goal sps:goal:train:state-10 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-10:atom-6 sps:goal:train:state-10
  (quote (sps:goal sps:goal:train:state-10 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-10:atom-7 sps:goal:train:state-10
  (quote (sps:goal sps:goal:train:state-10 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-10:atom-8 sps:goal:train:state-10
  (quote (sps:goal sps:goal:train:state-10 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-11:atom-1 sps:goal:train:state-11
  (quote (sps:goal sps:goal:train:state-11 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-11:atom-2 sps:goal:train:state-11
  (quote (sps:goal sps:goal:train:state-11 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-11:atom-3 sps:goal:train:state-11
  (quote (sps:goal sps:goal:train:state-11 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-11:atom-4 sps:goal:train:state-11
  (quote (sps:goal sps:goal:train:state-11 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-11:atom-5 sps:goal:train:state-11
  (quote (sps:goal sps:goal:train:state-11 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-11:atom-6 sps:goal:train:state-11
  (quote (sps:goal sps:goal:train:state-11 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-11:atom-7 sps:goal:train:state-11
  (quote (sps:goal sps:goal:train:state-11 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-11:atom-8 sps:goal:train:state-11
  (quote (sps:goal sps:goal:train:state-11 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-12:atom-1 sps:goal:train:state-12
  (quote (sps:goal sps:goal:train:state-12 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-12:atom-2 sps:goal:train:state-12
  (quote (sps:goal sps:goal:train:state-12 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-12:atom-3 sps:goal:train:state-12
  (quote (sps:goal sps:goal:train:state-12 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-12:atom-4 sps:goal:train:state-12
  (quote (sps:goal sps:goal:train:state-12 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-12:atom-5 sps:goal:train:state-12
  (quote (sps:goal sps:goal:train:state-12 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-12:atom-6 sps:goal:train:state-12
  (quote (sps:goal sps:goal:train:state-12 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-12:atom-7 sps:goal:train:state-12
  (quote (sps:goal sps:goal:train:state-12 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-12:atom-8 sps:goal:train:state-12
  (quote (sps:goal sps:goal:train:state-12 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-13:atom-1 sps:goal:train:state-13
  (quote (sps:goal sps:goal:train:state-13 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-13:atom-2 sps:goal:train:state-13
  (quote (sps:goal sps:goal:train:state-13 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:train:state-13:atom-3 sps:goal:train:state-13
  (quote (sps:goal sps:goal:train:state-13 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-13:atom-4 sps:goal:train:state-13
  (quote (sps:goal sps:goal:train:state-13 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:train:state-13:atom-5 sps:goal:train:state-13
  (quote (sps:goal sps:goal:train:state-13 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-13:atom-6 sps:goal:train:state-13
  (quote (sps:goal sps:goal:train:state-13 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:train:state-13:atom-7 sps:goal:train:state-13
  (quote (sps:goal sps:goal:train:state-13 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:train:state-13:atom-8 sps:goal:train:state-13
  (quote (sps:goal sps:goal:train:state-13 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:validate:state-1:atom-1 sps:goal:validate:state-1
  (quote (sps:goal sps:goal:validate:state-1 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:validate:state-1:atom-2 sps:goal:validate:state-1
  (quote (sps:goal sps:goal:validate:state-1 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:validate:state-1:atom-3 sps:goal:validate:state-1
  (quote (sps:goal sps:goal:validate:state-1 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:validate:state-1:atom-4 sps:goal:validate:state-1
  (quote (sps:goal sps:goal:validate:state-1 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:validate:state-1:atom-5 sps:goal:validate:state-1
  (quote (sps:goal sps:goal:validate:state-1 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:validate:state-1:atom-6 sps:goal:validate:state-1
  (quote (sps:goal sps:goal:validate:state-1 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:validate:state-1:atom-7 sps:goal:validate:state-1
  (quote (sps:goal sps:goal:validate:state-1 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:validate:state-1:atom-8 sps:goal:validate:state-1
  (quote (sps:goal sps:goal:validate:state-1 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:validate:state-2:atom-1 sps:goal:validate:state-2
  (quote (sps:goal sps:goal:validate:state-2 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:validate:state-2:atom-2 sps:goal:validate:state-2
  (quote (sps:goal sps:goal:validate:state-2 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:validate:state-2:atom-3 sps:goal:validate:state-2
  (quote (sps:goal sps:goal:validate:state-2 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:validate:state-2:atom-4 sps:goal:validate:state-2
  (quote (sps:goal sps:goal:validate:state-2 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:validate:state-2:atom-5 sps:goal:validate:state-2
  (quote (sps:goal sps:goal:validate:state-2 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:validate:state-2:atom-6 sps:goal:validate:state-2
  (quote (sps:goal sps:goal:validate:state-2 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:validate:state-2:atom-7 sps:goal:validate:state-2
  (quote (sps:goal sps:goal:validate:state-2 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:validate:state-2:atom-8 sps:goal:validate:state-2
  (quote (sps:goal sps:goal:validate:state-2 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:validate:state-3:atom-1 sps:goal:validate:state-3
  (quote (sps:goal sps:goal:validate:state-3 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:validate:state-3:atom-2 sps:goal:validate:state-3
  (quote (sps:goal sps:goal:validate:state-3 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:validate:state-3:atom-3 sps:goal:validate:state-3
  (quote (sps:goal sps:goal:validate:state-3 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:validate:state-3:atom-4 sps:goal:validate:state-3
  (quote (sps:goal sps:goal:validate:state-3 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:validate:state-3:atom-5 sps:goal:validate:state-3
  (quote (sps:goal sps:goal:validate:state-3 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:validate:state-3:atom-6 sps:goal:validate:state-3
  (quote (sps:goal sps:goal:validate:state-3 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:validate:state-3:atom-7 sps:goal:validate:state-3
  (quote (sps:goal sps:goal:validate:state-3 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:validate:state-3:atom-8 sps:goal:validate:state-3
  (quote (sps:goal sps:goal:validate:state-3 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:test:state-1:atom-1 sps:goal:test:state-1
  (quote (sps:goal sps:goal:test:state-1 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:test:state-1:atom-2 sps:goal:test:state-1
  (quote (sps:goal sps:goal:test:state-1 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:test:state-1:atom-3 sps:goal:test:state-1
  (quote (sps:goal sps:goal:test:state-1 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:test:state-1:atom-4 sps:goal:test:state-1
  (quote (sps:goal sps:goal:test:state-1 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:test:state-1:atom-5 sps:goal:test:state-1
  (quote (sps:goal sps:goal:test:state-1 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:test:state-1:atom-6 sps:goal:test:state-1
  (quote (sps:goal sps:goal:test:state-1 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:test:state-1:atom-7 sps:goal:test:state-1
  (quote (sps:goal sps:goal:test:state-1 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:test:state-1:atom-8 sps:goal:test:state-1
  (quote (sps:goal sps:goal:test:state-1 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:test:state-2:atom-1 sps:goal:test:state-2
  (quote (sps:goal sps:goal:test:state-2 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:test:state-2:atom-2 sps:goal:test:state-2
  (quote (sps:goal sps:goal:test:state-2 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:test:state-2:atom-3 sps:goal:test:state-2
  (quote (sps:goal sps:goal:test:state-2 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:test:state-2:atom-4 sps:goal:test:state-2
  (quote (sps:goal sps:goal:test:state-2 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:test:state-2:atom-5 sps:goal:test:state-2
  (quote (sps:goal sps:goal:test:state-2 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:test:state-2:atom-6 sps:goal:test:state-2
  (quote (sps:goal sps:goal:test:state-2 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:test:state-2:atom-7 sps:goal:test:state-2
  (quote (sps:goal sps:goal:test:state-2 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:test:state-2:atom-8 sps:goal:test:state-2
  (quote (sps:goal sps:goal:test:state-2 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:test:state-3:atom-1 sps:goal:test:state-3
  (quote (sps:goal sps:goal:test:state-3 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:test:state-3:atom-2 sps:goal:test:state-3
  (quote (sps:goal sps:goal:test:state-3 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:test:state-3:atom-3 sps:goal:test:state-3
  (quote (sps:goal sps:goal:test:state-3 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:test:state-3:atom-4 sps:goal:test:state-3
  (quote (sps:goal sps:goal:test:state-3 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:test:state-3:atom-5 sps:goal:test:state-3
  (quote (sps:goal sps:goal:test:state-3 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:test:state-3:atom-6 sps:goal:test:state-3
  (quote (sps:goal sps:goal:test:state-3 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:test:state-3:atom-7 sps:goal:test:state-3
  (quote (sps:goal sps:goal:test:state-3 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:test:state-3:atom-8 sps:goal:test:state-3
  (quote (sps:goal sps:goal:test:state-3 sps:p2 sps:n3)))
!(iggp:sps:classify sps:goal:test:state-4:atom-1 sps:goal:test:state-4
  (quote (sps:goal sps:goal:test:state-4 sps:p1 sps:n0)))
!(iggp:sps:classify sps:goal:test:state-4:atom-2 sps:goal:test:state-4
  (quote (sps:goal sps:goal:test:state-4 sps:p2 sps:n0)))
!(iggp:sps:classify sps:goal:test:state-4:atom-3 sps:goal:test:state-4
  (quote (sps:goal sps:goal:test:state-4 sps:p1 sps:n1)))
!(iggp:sps:classify sps:goal:test:state-4:atom-4 sps:goal:test:state-4
  (quote (sps:goal sps:goal:test:state-4 sps:p2 sps:n1)))
!(iggp:sps:classify sps:goal:test:state-4:atom-5 sps:goal:test:state-4
  (quote (sps:goal sps:goal:test:state-4 sps:p1 sps:n2)))
!(iggp:sps:classify sps:goal:test:state-4:atom-6 sps:goal:test:state-4
  (quote (sps:goal sps:goal:test:state-4 sps:p2 sps:n2)))
!(iggp:sps:classify sps:goal:test:state-4:atom-7 sps:goal:test:state-4
  (quote (sps:goal sps:goal:test:state-4 sps:p1 sps:n3)))
!(iggp:sps:classify sps:goal:test:state-4:atom-8 sps:goal:test:state-4
  (quote (sps:goal sps:goal:test:state-4 sps:p2 sps:n3)))
!(iggp:sps:classify sps:legal:train:state-1:atom-1 sps:legal:train:state-1
  (quote (sps:legal sps:legal:train:state-1 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-1:atom-2 sps:legal:train:state-1
  (quote (sps:legal sps:legal:train:state-1 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-1:atom-3 sps:legal:train:state-1
  (quote (sps:legal sps:legal:train:state-1 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-1:atom-4 sps:legal:train:state-1
  (quote (sps:legal sps:legal:train:state-1 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-1:atom-5 sps:legal:train:state-1
  (quote (sps:legal sps:legal:train:state-1 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-1:atom-6 sps:legal:train:state-1
  (quote (sps:legal sps:legal:train:state-1 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-2:atom-1 sps:legal:train:state-2
  (quote (sps:legal sps:legal:train:state-2 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-2:atom-2 sps:legal:train:state-2
  (quote (sps:legal sps:legal:train:state-2 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-2:atom-3 sps:legal:train:state-2
  (quote (sps:legal sps:legal:train:state-2 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-2:atom-4 sps:legal:train:state-2
  (quote (sps:legal sps:legal:train:state-2 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-2:atom-5 sps:legal:train:state-2
  (quote (sps:legal sps:legal:train:state-2 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-2:atom-6 sps:legal:train:state-2
  (quote (sps:legal sps:legal:train:state-2 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-3:atom-1 sps:legal:train:state-3
  (quote (sps:legal sps:legal:train:state-3 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-3:atom-2 sps:legal:train:state-3
  (quote (sps:legal sps:legal:train:state-3 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-3:atom-3 sps:legal:train:state-3
  (quote (sps:legal sps:legal:train:state-3 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-3:atom-4 sps:legal:train:state-3
  (quote (sps:legal sps:legal:train:state-3 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-3:atom-5 sps:legal:train:state-3
  (quote (sps:legal sps:legal:train:state-3 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-3:atom-6 sps:legal:train:state-3
  (quote (sps:legal sps:legal:train:state-3 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-4:atom-1 sps:legal:train:state-4
  (quote (sps:legal sps:legal:train:state-4 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-4:atom-2 sps:legal:train:state-4
  (quote (sps:legal sps:legal:train:state-4 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-4:atom-3 sps:legal:train:state-4
  (quote (sps:legal sps:legal:train:state-4 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-4:atom-4 sps:legal:train:state-4
  (quote (sps:legal sps:legal:train:state-4 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-4:atom-5 sps:legal:train:state-4
  (quote (sps:legal sps:legal:train:state-4 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-4:atom-6 sps:legal:train:state-4
  (quote (sps:legal sps:legal:train:state-4 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-5:atom-1 sps:legal:train:state-5
  (quote (sps:legal sps:legal:train:state-5 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-5:atom-2 sps:legal:train:state-5
  (quote (sps:legal sps:legal:train:state-5 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-5:atom-3 sps:legal:train:state-5
  (quote (sps:legal sps:legal:train:state-5 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-5:atom-4 sps:legal:train:state-5
  (quote (sps:legal sps:legal:train:state-5 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-5:atom-5 sps:legal:train:state-5
  (quote (sps:legal sps:legal:train:state-5 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-5:atom-6 sps:legal:train:state-5
  (quote (sps:legal sps:legal:train:state-5 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-6:atom-1 sps:legal:train:state-6
  (quote (sps:legal sps:legal:train:state-6 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-6:atom-2 sps:legal:train:state-6
  (quote (sps:legal sps:legal:train:state-6 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-6:atom-3 sps:legal:train:state-6
  (quote (sps:legal sps:legal:train:state-6 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-6:atom-4 sps:legal:train:state-6
  (quote (sps:legal sps:legal:train:state-6 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-6:atom-5 sps:legal:train:state-6
  (quote (sps:legal sps:legal:train:state-6 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-6:atom-6 sps:legal:train:state-6
  (quote (sps:legal sps:legal:train:state-6 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-7:atom-1 sps:legal:train:state-7
  (quote (sps:legal sps:legal:train:state-7 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-7:atom-2 sps:legal:train:state-7
  (quote (sps:legal sps:legal:train:state-7 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-7:atom-3 sps:legal:train:state-7
  (quote (sps:legal sps:legal:train:state-7 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-7:atom-4 sps:legal:train:state-7
  (quote (sps:legal sps:legal:train:state-7 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-7:atom-5 sps:legal:train:state-7
  (quote (sps:legal sps:legal:train:state-7 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-7:atom-6 sps:legal:train:state-7
  (quote (sps:legal sps:legal:train:state-7 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-8:atom-1 sps:legal:train:state-8
  (quote (sps:legal sps:legal:train:state-8 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-8:atom-2 sps:legal:train:state-8
  (quote (sps:legal sps:legal:train:state-8 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-8:atom-3 sps:legal:train:state-8
  (quote (sps:legal sps:legal:train:state-8 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-8:atom-4 sps:legal:train:state-8
  (quote (sps:legal sps:legal:train:state-8 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-8:atom-5 sps:legal:train:state-8
  (quote (sps:legal sps:legal:train:state-8 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-8:atom-6 sps:legal:train:state-8
  (quote (sps:legal sps:legal:train:state-8 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-9:atom-1 sps:legal:train:state-9
  (quote (sps:legal sps:legal:train:state-9 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-9:atom-2 sps:legal:train:state-9
  (quote (sps:legal sps:legal:train:state-9 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-9:atom-3 sps:legal:train:state-9
  (quote (sps:legal sps:legal:train:state-9 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-9:atom-4 sps:legal:train:state-9
  (quote (sps:legal sps:legal:train:state-9 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-9:atom-5 sps:legal:train:state-9
  (quote (sps:legal sps:legal:train:state-9 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-9:atom-6 sps:legal:train:state-9
  (quote (sps:legal sps:legal:train:state-9 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-10:atom-1 sps:legal:train:state-10
  (quote (sps:legal sps:legal:train:state-10 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-10:atom-2 sps:legal:train:state-10
  (quote (sps:legal sps:legal:train:state-10 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-10:atom-3 sps:legal:train:state-10
  (quote (sps:legal sps:legal:train:state-10 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-10:atom-4 sps:legal:train:state-10
  (quote (sps:legal sps:legal:train:state-10 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-10:atom-5 sps:legal:train:state-10
  (quote (sps:legal sps:legal:train:state-10 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-10:atom-6 sps:legal:train:state-10
  (quote (sps:legal sps:legal:train:state-10 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-11:atom-1 sps:legal:train:state-11
  (quote (sps:legal sps:legal:train:state-11 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-11:atom-2 sps:legal:train:state-11
  (quote (sps:legal sps:legal:train:state-11 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-11:atom-3 sps:legal:train:state-11
  (quote (sps:legal sps:legal:train:state-11 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-11:atom-4 sps:legal:train:state-11
  (quote (sps:legal sps:legal:train:state-11 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-11:atom-5 sps:legal:train:state-11
  (quote (sps:legal sps:legal:train:state-11 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-11:atom-6 sps:legal:train:state-11
  (quote (sps:legal sps:legal:train:state-11 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-12:atom-1 sps:legal:train:state-12
  (quote (sps:legal sps:legal:train:state-12 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-12:atom-2 sps:legal:train:state-12
  (quote (sps:legal sps:legal:train:state-12 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-12:atom-3 sps:legal:train:state-12
  (quote (sps:legal sps:legal:train:state-12 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-12:atom-4 sps:legal:train:state-12
  (quote (sps:legal sps:legal:train:state-12 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-12:atom-5 sps:legal:train:state-12
  (quote (sps:legal sps:legal:train:state-12 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-12:atom-6 sps:legal:train:state-12
  (quote (sps:legal sps:legal:train:state-12 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-13:atom-1 sps:legal:train:state-13
  (quote (sps:legal sps:legal:train:state-13 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-13:atom-2 sps:legal:train:state-13
  (quote (sps:legal sps:legal:train:state-13 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:train:state-13:atom-3 sps:legal:train:state-13
  (quote (sps:legal sps:legal:train:state-13 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-13:atom-4 sps:legal:train:state-13
  (quote (sps:legal sps:legal:train:state-13 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:train:state-13:atom-5 sps:legal:train:state-13
  (quote (sps:legal sps:legal:train:state-13 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:train:state-13:atom-6 sps:legal:train:state-13
  (quote (sps:legal sps:legal:train:state-13 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:validate:state-1:atom-1 sps:legal:validate:state-1
  (quote (sps:legal sps:legal:validate:state-1 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:validate:state-1:atom-2 sps:legal:validate:state-1
  (quote (sps:legal sps:legal:validate:state-1 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:validate:state-1:atom-3 sps:legal:validate:state-1
  (quote (sps:legal sps:legal:validate:state-1 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:validate:state-1:atom-4 sps:legal:validate:state-1
  (quote (sps:legal sps:legal:validate:state-1 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:validate:state-1:atom-5 sps:legal:validate:state-1
  (quote (sps:legal sps:legal:validate:state-1 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:validate:state-1:atom-6 sps:legal:validate:state-1
  (quote (sps:legal sps:legal:validate:state-1 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:validate:state-2:atom-1 sps:legal:validate:state-2
  (quote (sps:legal sps:legal:validate:state-2 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:validate:state-2:atom-2 sps:legal:validate:state-2
  (quote (sps:legal sps:legal:validate:state-2 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:validate:state-2:atom-3 sps:legal:validate:state-2
  (quote (sps:legal sps:legal:validate:state-2 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:validate:state-2:atom-4 sps:legal:validate:state-2
  (quote (sps:legal sps:legal:validate:state-2 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:validate:state-2:atom-5 sps:legal:validate:state-2
  (quote (sps:legal sps:legal:validate:state-2 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:validate:state-2:atom-6 sps:legal:validate:state-2
  (quote (sps:legal sps:legal:validate:state-2 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:validate:state-3:atom-1 sps:legal:validate:state-3
  (quote (sps:legal sps:legal:validate:state-3 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:validate:state-3:atom-2 sps:legal:validate:state-3
  (quote (sps:legal sps:legal:validate:state-3 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:validate:state-3:atom-3 sps:legal:validate:state-3
  (quote (sps:legal sps:legal:validate:state-3 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:validate:state-3:atom-4 sps:legal:validate:state-3
  (quote (sps:legal sps:legal:validate:state-3 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:validate:state-3:atom-5 sps:legal:validate:state-3
  (quote (sps:legal sps:legal:validate:state-3 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:validate:state-3:atom-6 sps:legal:validate:state-3
  (quote (sps:legal sps:legal:validate:state-3 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:test:state-1:atom-1 sps:legal:test:state-1
  (quote (sps:legal sps:legal:test:state-1 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:test:state-1:atom-2 sps:legal:test:state-1
  (quote (sps:legal sps:legal:test:state-1 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:test:state-1:atom-3 sps:legal:test:state-1
  (quote (sps:legal sps:legal:test:state-1 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:test:state-1:atom-4 sps:legal:test:state-1
  (quote (sps:legal sps:legal:test:state-1 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:test:state-1:atom-5 sps:legal:test:state-1
  (quote (sps:legal sps:legal:test:state-1 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:test:state-1:atom-6 sps:legal:test:state-1
  (quote (sps:legal sps:legal:test:state-1 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:test:state-2:atom-1 sps:legal:test:state-2
  (quote (sps:legal sps:legal:test:state-2 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:test:state-2:atom-2 sps:legal:test:state-2
  (quote (sps:legal sps:legal:test:state-2 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:test:state-2:atom-3 sps:legal:test:state-2
  (quote (sps:legal sps:legal:test:state-2 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:test:state-2:atom-4 sps:legal:test:state-2
  (quote (sps:legal sps:legal:test:state-2 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:test:state-2:atom-5 sps:legal:test:state-2
  (quote (sps:legal sps:legal:test:state-2 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:test:state-2:atom-6 sps:legal:test:state-2
  (quote (sps:legal sps:legal:test:state-2 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:test:state-3:atom-1 sps:legal:test:state-3
  (quote (sps:legal sps:legal:test:state-3 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:test:state-3:atom-2 sps:legal:test:state-3
  (quote (sps:legal sps:legal:test:state-3 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:test:state-3:atom-3 sps:legal:test:state-3
  (quote (sps:legal sps:legal:test:state-3 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:test:state-3:atom-4 sps:legal:test:state-3
  (quote (sps:legal sps:legal:test:state-3 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:test:state-3:atom-5 sps:legal:test:state-3
  (quote (sps:legal sps:legal:test:state-3 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:test:state-3:atom-6 sps:legal:test:state-3
  (quote (sps:legal sps:legal:test:state-3 sps:p2 sps:stone)))
!(iggp:sps:classify sps:legal:test:state-4:atom-1 sps:legal:test:state-4
  (quote (sps:legal sps:legal:test:state-4 sps:p1 sps:paper)))
!(iggp:sps:classify sps:legal:test:state-4:atom-2 sps:legal:test:state-4
  (quote (sps:legal sps:legal:test:state-4 sps:p2 sps:paper)))
!(iggp:sps:classify sps:legal:test:state-4:atom-3 sps:legal:test:state-4
  (quote (sps:legal sps:legal:test:state-4 sps:p1 sps:scissors)))
!(iggp:sps:classify sps:legal:test:state-4:atom-4 sps:legal:test:state-4
  (quote (sps:legal sps:legal:test:state-4 sps:p2 sps:scissors)))
!(iggp:sps:classify sps:legal:test:state-4:atom-5 sps:legal:test:state-4
  (quote (sps:legal sps:legal:test:state-4 sps:p1 sps:stone)))
!(iggp:sps:classify sps:legal:test:state-4:atom-6 sps:legal:test:state-4
  (quote (sps:legal sps:legal:test:state-4 sps:p2 sps:stone)))
!(iggp:sps:classify sps:next:train:state-1:atom-1 sps:next:train:state-1
  (quote (sps:next-score sps:next:train:state-1 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-1:atom-2 sps:next:train:state-1
  (quote (sps:next-score sps:next:train:state-1 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-1:atom-3 sps:next:train:state-1
  (quote (sps:next-score sps:next:train:state-1 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-1:atom-4 sps:next:train:state-1
  (quote (sps:next-score sps:next:train:state-1 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-1:atom-5 sps:next:train:state-1
  (quote (sps:next-score sps:next:train:state-1 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-1:atom-6 sps:next:train:state-1
  (quote (sps:next-score sps:next:train:state-1 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-1:atom-7 sps:next:train:state-1
  (quote (sps:next-score sps:next:train:state-1 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-1:atom-8 sps:next:train:state-1
  (quote (sps:next-score sps:next:train:state-1 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-1:atom-9 sps:next:train:state-1
  (quote (sps:next-step sps:next:train:state-1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-1:atom-10 sps:next:train:state-1
  (quote (sps:next-step sps:next:train:state-1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-1:atom-11 sps:next:train:state-1
  (quote (sps:next-step sps:next:train:state-1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-1:atom-12 sps:next:train:state-1
  (quote (sps:next-step sps:next:train:state-1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-2:atom-1 sps:next:train:state-2
  (quote (sps:next-score sps:next:train:state-2 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-2:atom-2 sps:next:train:state-2
  (quote (sps:next-score sps:next:train:state-2 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-2:atom-3 sps:next:train:state-2
  (quote (sps:next-score sps:next:train:state-2 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-2:atom-4 sps:next:train:state-2
  (quote (sps:next-score sps:next:train:state-2 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-2:atom-5 sps:next:train:state-2
  (quote (sps:next-score sps:next:train:state-2 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-2:atom-6 sps:next:train:state-2
  (quote (sps:next-score sps:next:train:state-2 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-2:atom-7 sps:next:train:state-2
  (quote (sps:next-score sps:next:train:state-2 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-2:atom-8 sps:next:train:state-2
  (quote (sps:next-score sps:next:train:state-2 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-2:atom-9 sps:next:train:state-2
  (quote (sps:next-step sps:next:train:state-2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-2:atom-10 sps:next:train:state-2
  (quote (sps:next-step sps:next:train:state-2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-2:atom-11 sps:next:train:state-2
  (quote (sps:next-step sps:next:train:state-2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-2:atom-12 sps:next:train:state-2
  (quote (sps:next-step sps:next:train:state-2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-3:atom-1 sps:next:train:state-3
  (quote (sps:next-score sps:next:train:state-3 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-3:atom-2 sps:next:train:state-3
  (quote (sps:next-score sps:next:train:state-3 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-3:atom-3 sps:next:train:state-3
  (quote (sps:next-score sps:next:train:state-3 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-3:atom-4 sps:next:train:state-3
  (quote (sps:next-score sps:next:train:state-3 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-3:atom-5 sps:next:train:state-3
  (quote (sps:next-score sps:next:train:state-3 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-3:atom-6 sps:next:train:state-3
  (quote (sps:next-score sps:next:train:state-3 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-3:atom-7 sps:next:train:state-3
  (quote (sps:next-score sps:next:train:state-3 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-3:atom-8 sps:next:train:state-3
  (quote (sps:next-score sps:next:train:state-3 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-3:atom-9 sps:next:train:state-3
  (quote (sps:next-step sps:next:train:state-3 sps:n0)))
!(iggp:sps:classify sps:next:train:state-3:atom-10 sps:next:train:state-3
  (quote (sps:next-step sps:next:train:state-3 sps:n1)))
!(iggp:sps:classify sps:next:train:state-3:atom-11 sps:next:train:state-3
  (quote (sps:next-step sps:next:train:state-3 sps:n2)))
!(iggp:sps:classify sps:next:train:state-3:atom-12 sps:next:train:state-3
  (quote (sps:next-step sps:next:train:state-3 sps:n3)))
!(iggp:sps:classify sps:next:train:state-4:atom-1 sps:next:train:state-4
  (quote (sps:next-score sps:next:train:state-4 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-4:atom-2 sps:next:train:state-4
  (quote (sps:next-score sps:next:train:state-4 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-4:atom-3 sps:next:train:state-4
  (quote (sps:next-score sps:next:train:state-4 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-4:atom-4 sps:next:train:state-4
  (quote (sps:next-score sps:next:train:state-4 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-4:atom-5 sps:next:train:state-4
  (quote (sps:next-score sps:next:train:state-4 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-4:atom-6 sps:next:train:state-4
  (quote (sps:next-score sps:next:train:state-4 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-4:atom-7 sps:next:train:state-4
  (quote (sps:next-score sps:next:train:state-4 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-4:atom-8 sps:next:train:state-4
  (quote (sps:next-score sps:next:train:state-4 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-4:atom-9 sps:next:train:state-4
  (quote (sps:next-step sps:next:train:state-4 sps:n0)))
!(iggp:sps:classify sps:next:train:state-4:atom-10 sps:next:train:state-4
  (quote (sps:next-step sps:next:train:state-4 sps:n1)))
!(iggp:sps:classify sps:next:train:state-4:atom-11 sps:next:train:state-4
  (quote (sps:next-step sps:next:train:state-4 sps:n2)))
!(iggp:sps:classify sps:next:train:state-4:atom-12 sps:next:train:state-4
  (quote (sps:next-step sps:next:train:state-4 sps:n3)))
!(iggp:sps:classify sps:next:train:state-5:atom-1 sps:next:train:state-5
  (quote (sps:next-score sps:next:train:state-5 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-5:atom-2 sps:next:train:state-5
  (quote (sps:next-score sps:next:train:state-5 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-5:atom-3 sps:next:train:state-5
  (quote (sps:next-score sps:next:train:state-5 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-5:atom-4 sps:next:train:state-5
  (quote (sps:next-score sps:next:train:state-5 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-5:atom-5 sps:next:train:state-5
  (quote (sps:next-score sps:next:train:state-5 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-5:atom-6 sps:next:train:state-5
  (quote (sps:next-score sps:next:train:state-5 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-5:atom-7 sps:next:train:state-5
  (quote (sps:next-score sps:next:train:state-5 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-5:atom-8 sps:next:train:state-5
  (quote (sps:next-score sps:next:train:state-5 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-5:atom-9 sps:next:train:state-5
  (quote (sps:next-step sps:next:train:state-5 sps:n0)))
!(iggp:sps:classify sps:next:train:state-5:atom-10 sps:next:train:state-5
  (quote (sps:next-step sps:next:train:state-5 sps:n1)))
!(iggp:sps:classify sps:next:train:state-5:atom-11 sps:next:train:state-5
  (quote (sps:next-step sps:next:train:state-5 sps:n2)))
!(iggp:sps:classify sps:next:train:state-5:atom-12 sps:next:train:state-5
  (quote (sps:next-step sps:next:train:state-5 sps:n3)))
!(iggp:sps:classify sps:next:train:state-6:atom-1 sps:next:train:state-6
  (quote (sps:next-score sps:next:train:state-6 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-6:atom-2 sps:next:train:state-6
  (quote (sps:next-score sps:next:train:state-6 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-6:atom-3 sps:next:train:state-6
  (quote (sps:next-score sps:next:train:state-6 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-6:atom-4 sps:next:train:state-6
  (quote (sps:next-score sps:next:train:state-6 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-6:atom-5 sps:next:train:state-6
  (quote (sps:next-score sps:next:train:state-6 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-6:atom-6 sps:next:train:state-6
  (quote (sps:next-score sps:next:train:state-6 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-6:atom-7 sps:next:train:state-6
  (quote (sps:next-score sps:next:train:state-6 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-6:atom-8 sps:next:train:state-6
  (quote (sps:next-score sps:next:train:state-6 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-6:atom-9 sps:next:train:state-6
  (quote (sps:next-step sps:next:train:state-6 sps:n0)))
!(iggp:sps:classify sps:next:train:state-6:atom-10 sps:next:train:state-6
  (quote (sps:next-step sps:next:train:state-6 sps:n1)))
!(iggp:sps:classify sps:next:train:state-6:atom-11 sps:next:train:state-6
  (quote (sps:next-step sps:next:train:state-6 sps:n2)))
!(iggp:sps:classify sps:next:train:state-6:atom-12 sps:next:train:state-6
  (quote (sps:next-step sps:next:train:state-6 sps:n3)))
!(iggp:sps:classify sps:next:train:state-7:atom-1 sps:next:train:state-7
  (quote (sps:next-score sps:next:train:state-7 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-7:atom-2 sps:next:train:state-7
  (quote (sps:next-score sps:next:train:state-7 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-7:atom-3 sps:next:train:state-7
  (quote (sps:next-score sps:next:train:state-7 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-7:atom-4 sps:next:train:state-7
  (quote (sps:next-score sps:next:train:state-7 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-7:atom-5 sps:next:train:state-7
  (quote (sps:next-score sps:next:train:state-7 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-7:atom-6 sps:next:train:state-7
  (quote (sps:next-score sps:next:train:state-7 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-7:atom-7 sps:next:train:state-7
  (quote (sps:next-score sps:next:train:state-7 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-7:atom-8 sps:next:train:state-7
  (quote (sps:next-score sps:next:train:state-7 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-7:atom-9 sps:next:train:state-7
  (quote (sps:next-step sps:next:train:state-7 sps:n0)))
!(iggp:sps:classify sps:next:train:state-7:atom-10 sps:next:train:state-7
  (quote (sps:next-step sps:next:train:state-7 sps:n1)))
!(iggp:sps:classify sps:next:train:state-7:atom-11 sps:next:train:state-7
  (quote (sps:next-step sps:next:train:state-7 sps:n2)))
!(iggp:sps:classify sps:next:train:state-7:atom-12 sps:next:train:state-7
  (quote (sps:next-step sps:next:train:state-7 sps:n3)))
!(iggp:sps:classify sps:next:train:state-8:atom-1 sps:next:train:state-8
  (quote (sps:next-score sps:next:train:state-8 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-8:atom-2 sps:next:train:state-8
  (quote (sps:next-score sps:next:train:state-8 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-8:atom-3 sps:next:train:state-8
  (quote (sps:next-score sps:next:train:state-8 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-8:atom-4 sps:next:train:state-8
  (quote (sps:next-score sps:next:train:state-8 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-8:atom-5 sps:next:train:state-8
  (quote (sps:next-score sps:next:train:state-8 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-8:atom-6 sps:next:train:state-8
  (quote (sps:next-score sps:next:train:state-8 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-8:atom-7 sps:next:train:state-8
  (quote (sps:next-score sps:next:train:state-8 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-8:atom-8 sps:next:train:state-8
  (quote (sps:next-score sps:next:train:state-8 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-8:atom-9 sps:next:train:state-8
  (quote (sps:next-step sps:next:train:state-8 sps:n0)))
!(iggp:sps:classify sps:next:train:state-8:atom-10 sps:next:train:state-8
  (quote (sps:next-step sps:next:train:state-8 sps:n1)))
!(iggp:sps:classify sps:next:train:state-8:atom-11 sps:next:train:state-8
  (quote (sps:next-step sps:next:train:state-8 sps:n2)))
!(iggp:sps:classify sps:next:train:state-8:atom-12 sps:next:train:state-8
  (quote (sps:next-step sps:next:train:state-8 sps:n3)))
!(iggp:sps:classify sps:next:train:state-9:atom-1 sps:next:train:state-9
  (quote (sps:next-score sps:next:train:state-9 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-9:atom-2 sps:next:train:state-9
  (quote (sps:next-score sps:next:train:state-9 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-9:atom-3 sps:next:train:state-9
  (quote (sps:next-score sps:next:train:state-9 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-9:atom-4 sps:next:train:state-9
  (quote (sps:next-score sps:next:train:state-9 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-9:atom-5 sps:next:train:state-9
  (quote (sps:next-score sps:next:train:state-9 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-9:atom-6 sps:next:train:state-9
  (quote (sps:next-score sps:next:train:state-9 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-9:atom-7 sps:next:train:state-9
  (quote (sps:next-score sps:next:train:state-9 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-9:atom-8 sps:next:train:state-9
  (quote (sps:next-score sps:next:train:state-9 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-9:atom-9 sps:next:train:state-9
  (quote (sps:next-step sps:next:train:state-9 sps:n0)))
!(iggp:sps:classify sps:next:train:state-9:atom-10 sps:next:train:state-9
  (quote (sps:next-step sps:next:train:state-9 sps:n1)))
!(iggp:sps:classify sps:next:train:state-9:atom-11 sps:next:train:state-9
  (quote (sps:next-step sps:next:train:state-9 sps:n2)))
!(iggp:sps:classify sps:next:train:state-9:atom-12 sps:next:train:state-9
  (quote (sps:next-step sps:next:train:state-9 sps:n3)))
!(iggp:sps:classify sps:next:train:state-10:atom-1 sps:next:train:state-10
  (quote (sps:next-score sps:next:train:state-10 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-10:atom-2 sps:next:train:state-10
  (quote (sps:next-score sps:next:train:state-10 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-10:atom-3 sps:next:train:state-10
  (quote (sps:next-score sps:next:train:state-10 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-10:atom-4 sps:next:train:state-10
  (quote (sps:next-score sps:next:train:state-10 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-10:atom-5 sps:next:train:state-10
  (quote (sps:next-score sps:next:train:state-10 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-10:atom-6 sps:next:train:state-10
  (quote (sps:next-score sps:next:train:state-10 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-10:atom-7 sps:next:train:state-10
  (quote (sps:next-score sps:next:train:state-10 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-10:atom-8 sps:next:train:state-10
  (quote (sps:next-score sps:next:train:state-10 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-10:atom-9 sps:next:train:state-10
  (quote (sps:next-step sps:next:train:state-10 sps:n0)))
!(iggp:sps:classify sps:next:train:state-10:atom-10 sps:next:train:state-10
  (quote (sps:next-step sps:next:train:state-10 sps:n1)))
!(iggp:sps:classify sps:next:train:state-10:atom-11 sps:next:train:state-10
  (quote (sps:next-step sps:next:train:state-10 sps:n2)))
!(iggp:sps:classify sps:next:train:state-10:atom-12 sps:next:train:state-10
  (quote (sps:next-step sps:next:train:state-10 sps:n3)))
!(iggp:sps:classify sps:next:train:state-11:atom-1 sps:next:train:state-11
  (quote (sps:next-score sps:next:train:state-11 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-11:atom-2 sps:next:train:state-11
  (quote (sps:next-score sps:next:train:state-11 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-11:atom-3 sps:next:train:state-11
  (quote (sps:next-score sps:next:train:state-11 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-11:atom-4 sps:next:train:state-11
  (quote (sps:next-score sps:next:train:state-11 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-11:atom-5 sps:next:train:state-11
  (quote (sps:next-score sps:next:train:state-11 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-11:atom-6 sps:next:train:state-11
  (quote (sps:next-score sps:next:train:state-11 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-11:atom-7 sps:next:train:state-11
  (quote (sps:next-score sps:next:train:state-11 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-11:atom-8 sps:next:train:state-11
  (quote (sps:next-score sps:next:train:state-11 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-11:atom-9 sps:next:train:state-11
  (quote (sps:next-step sps:next:train:state-11 sps:n0)))
!(iggp:sps:classify sps:next:train:state-11:atom-10 sps:next:train:state-11
  (quote (sps:next-step sps:next:train:state-11 sps:n1)))
!(iggp:sps:classify sps:next:train:state-11:atom-11 sps:next:train:state-11
  (quote (sps:next-step sps:next:train:state-11 sps:n2)))
!(iggp:sps:classify sps:next:train:state-11:atom-12 sps:next:train:state-11
  (quote (sps:next-step sps:next:train:state-11 sps:n3)))
!(iggp:sps:classify sps:next:train:state-12:atom-1 sps:next:train:state-12
  (quote (sps:next-score sps:next:train:state-12 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-12:atom-2 sps:next:train:state-12
  (quote (sps:next-score sps:next:train:state-12 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-12:atom-3 sps:next:train:state-12
  (quote (sps:next-score sps:next:train:state-12 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-12:atom-4 sps:next:train:state-12
  (quote (sps:next-score sps:next:train:state-12 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-12:atom-5 sps:next:train:state-12
  (quote (sps:next-score sps:next:train:state-12 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-12:atom-6 sps:next:train:state-12
  (quote (sps:next-score sps:next:train:state-12 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-12:atom-7 sps:next:train:state-12
  (quote (sps:next-score sps:next:train:state-12 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-12:atom-8 sps:next:train:state-12
  (quote (sps:next-score sps:next:train:state-12 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-12:atom-9 sps:next:train:state-12
  (quote (sps:next-step sps:next:train:state-12 sps:n0)))
!(iggp:sps:classify sps:next:train:state-12:atom-10 sps:next:train:state-12
  (quote (sps:next-step sps:next:train:state-12 sps:n1)))
!(iggp:sps:classify sps:next:train:state-12:atom-11 sps:next:train:state-12
  (quote (sps:next-step sps:next:train:state-12 sps:n2)))
!(iggp:sps:classify sps:next:train:state-12:atom-12 sps:next:train:state-12
  (quote (sps:next-step sps:next:train:state-12 sps:n3)))
!(iggp:sps:classify sps:next:train:state-13:atom-1 sps:next:train:state-13
  (quote (sps:next-score sps:next:train:state-13 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-13:atom-2 sps:next:train:state-13
  (quote (sps:next-score sps:next:train:state-13 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-13:atom-3 sps:next:train:state-13
  (quote (sps:next-score sps:next:train:state-13 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-13:atom-4 sps:next:train:state-13
  (quote (sps:next-score sps:next:train:state-13 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-13:atom-5 sps:next:train:state-13
  (quote (sps:next-score sps:next:train:state-13 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-13:atom-6 sps:next:train:state-13
  (quote (sps:next-score sps:next:train:state-13 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-13:atom-7 sps:next:train:state-13
  (quote (sps:next-score sps:next:train:state-13 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-13:atom-8 sps:next:train:state-13
  (quote (sps:next-score sps:next:train:state-13 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-13:atom-9 sps:next:train:state-13
  (quote (sps:next-step sps:next:train:state-13 sps:n0)))
!(iggp:sps:classify sps:next:train:state-13:atom-10 sps:next:train:state-13
  (quote (sps:next-step sps:next:train:state-13 sps:n1)))
!(iggp:sps:classify sps:next:train:state-13:atom-11 sps:next:train:state-13
  (quote (sps:next-step sps:next:train:state-13 sps:n2)))
!(iggp:sps:classify sps:next:train:state-13:atom-12 sps:next:train:state-13
  (quote (sps:next-step sps:next:train:state-13 sps:n3)))
!(iggp:sps:classify sps:next:train:state-14:atom-1 sps:next:train:state-14
  (quote (sps:next-score sps:next:train:state-14 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-14:atom-2 sps:next:train:state-14
  (quote (sps:next-score sps:next:train:state-14 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-14:atom-3 sps:next:train:state-14
  (quote (sps:next-score sps:next:train:state-14 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-14:atom-4 sps:next:train:state-14
  (quote (sps:next-score sps:next:train:state-14 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-14:atom-5 sps:next:train:state-14
  (quote (sps:next-score sps:next:train:state-14 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-14:atom-6 sps:next:train:state-14
  (quote (sps:next-score sps:next:train:state-14 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-14:atom-7 sps:next:train:state-14
  (quote (sps:next-score sps:next:train:state-14 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-14:atom-8 sps:next:train:state-14
  (quote (sps:next-score sps:next:train:state-14 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-14:atom-9 sps:next:train:state-14
  (quote (sps:next-step sps:next:train:state-14 sps:n0)))
!(iggp:sps:classify sps:next:train:state-14:atom-10 sps:next:train:state-14
  (quote (sps:next-step sps:next:train:state-14 sps:n1)))
!(iggp:sps:classify sps:next:train:state-14:atom-11 sps:next:train:state-14
  (quote (sps:next-step sps:next:train:state-14 sps:n2)))
!(iggp:sps:classify sps:next:train:state-14:atom-12 sps:next:train:state-14
  (quote (sps:next-step sps:next:train:state-14 sps:n3)))
!(iggp:sps:classify sps:next:train:state-15:atom-1 sps:next:train:state-15
  (quote (sps:next-score sps:next:train:state-15 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-15:atom-2 sps:next:train:state-15
  (quote (sps:next-score sps:next:train:state-15 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-15:atom-3 sps:next:train:state-15
  (quote (sps:next-score sps:next:train:state-15 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-15:atom-4 sps:next:train:state-15
  (quote (sps:next-score sps:next:train:state-15 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-15:atom-5 sps:next:train:state-15
  (quote (sps:next-score sps:next:train:state-15 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-15:atom-6 sps:next:train:state-15
  (quote (sps:next-score sps:next:train:state-15 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-15:atom-7 sps:next:train:state-15
  (quote (sps:next-score sps:next:train:state-15 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-15:atom-8 sps:next:train:state-15
  (quote (sps:next-score sps:next:train:state-15 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-15:atom-9 sps:next:train:state-15
  (quote (sps:next-step sps:next:train:state-15 sps:n0)))
!(iggp:sps:classify sps:next:train:state-15:atom-10 sps:next:train:state-15
  (quote (sps:next-step sps:next:train:state-15 sps:n1)))
!(iggp:sps:classify sps:next:train:state-15:atom-11 sps:next:train:state-15
  (quote (sps:next-step sps:next:train:state-15 sps:n2)))
!(iggp:sps:classify sps:next:train:state-15:atom-12 sps:next:train:state-15
  (quote (sps:next-step sps:next:train:state-15 sps:n3)))
!(iggp:sps:classify sps:next:train:state-16:atom-1 sps:next:train:state-16
  (quote (sps:next-score sps:next:train:state-16 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-16:atom-2 sps:next:train:state-16
  (quote (sps:next-score sps:next:train:state-16 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-16:atom-3 sps:next:train:state-16
  (quote (sps:next-score sps:next:train:state-16 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-16:atom-4 sps:next:train:state-16
  (quote (sps:next-score sps:next:train:state-16 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-16:atom-5 sps:next:train:state-16
  (quote (sps:next-score sps:next:train:state-16 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-16:atom-6 sps:next:train:state-16
  (quote (sps:next-score sps:next:train:state-16 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-16:atom-7 sps:next:train:state-16
  (quote (sps:next-score sps:next:train:state-16 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-16:atom-8 sps:next:train:state-16
  (quote (sps:next-score sps:next:train:state-16 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-16:atom-9 sps:next:train:state-16
  (quote (sps:next-step sps:next:train:state-16 sps:n0)))
!(iggp:sps:classify sps:next:train:state-16:atom-10 sps:next:train:state-16
  (quote (sps:next-step sps:next:train:state-16 sps:n1)))
!(iggp:sps:classify sps:next:train:state-16:atom-11 sps:next:train:state-16
  (quote (sps:next-step sps:next:train:state-16 sps:n2)))
!(iggp:sps:classify sps:next:train:state-16:atom-12 sps:next:train:state-16
  (quote (sps:next-step sps:next:train:state-16 sps:n3)))
!(iggp:sps:classify sps:next:train:state-17:atom-1 sps:next:train:state-17
  (quote (sps:next-score sps:next:train:state-17 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-17:atom-2 sps:next:train:state-17
  (quote (sps:next-score sps:next:train:state-17 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-17:atom-3 sps:next:train:state-17
  (quote (sps:next-score sps:next:train:state-17 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-17:atom-4 sps:next:train:state-17
  (quote (sps:next-score sps:next:train:state-17 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-17:atom-5 sps:next:train:state-17
  (quote (sps:next-score sps:next:train:state-17 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-17:atom-6 sps:next:train:state-17
  (quote (sps:next-score sps:next:train:state-17 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-17:atom-7 sps:next:train:state-17
  (quote (sps:next-score sps:next:train:state-17 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-17:atom-8 sps:next:train:state-17
  (quote (sps:next-score sps:next:train:state-17 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-17:atom-9 sps:next:train:state-17
  (quote (sps:next-step sps:next:train:state-17 sps:n0)))
!(iggp:sps:classify sps:next:train:state-17:atom-10 sps:next:train:state-17
  (quote (sps:next-step sps:next:train:state-17 sps:n1)))
!(iggp:sps:classify sps:next:train:state-17:atom-11 sps:next:train:state-17
  (quote (sps:next-step sps:next:train:state-17 sps:n2)))
!(iggp:sps:classify sps:next:train:state-17:atom-12 sps:next:train:state-17
  (quote (sps:next-step sps:next:train:state-17 sps:n3)))
!(iggp:sps:classify sps:next:train:state-18:atom-1 sps:next:train:state-18
  (quote (sps:next-score sps:next:train:state-18 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-18:atom-2 sps:next:train:state-18
  (quote (sps:next-score sps:next:train:state-18 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-18:atom-3 sps:next:train:state-18
  (quote (sps:next-score sps:next:train:state-18 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-18:atom-4 sps:next:train:state-18
  (quote (sps:next-score sps:next:train:state-18 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-18:atom-5 sps:next:train:state-18
  (quote (sps:next-score sps:next:train:state-18 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-18:atom-6 sps:next:train:state-18
  (quote (sps:next-score sps:next:train:state-18 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-18:atom-7 sps:next:train:state-18
  (quote (sps:next-score sps:next:train:state-18 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-18:atom-8 sps:next:train:state-18
  (quote (sps:next-score sps:next:train:state-18 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-18:atom-9 sps:next:train:state-18
  (quote (sps:next-step sps:next:train:state-18 sps:n0)))
!(iggp:sps:classify sps:next:train:state-18:atom-10 sps:next:train:state-18
  (quote (sps:next-step sps:next:train:state-18 sps:n1)))
!(iggp:sps:classify sps:next:train:state-18:atom-11 sps:next:train:state-18
  (quote (sps:next-step sps:next:train:state-18 sps:n2)))
!(iggp:sps:classify sps:next:train:state-18:atom-12 sps:next:train:state-18
  (quote (sps:next-step sps:next:train:state-18 sps:n3)))
!(iggp:sps:classify sps:next:train:state-19:atom-1 sps:next:train:state-19
  (quote (sps:next-score sps:next:train:state-19 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-19:atom-2 sps:next:train:state-19
  (quote (sps:next-score sps:next:train:state-19 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-19:atom-3 sps:next:train:state-19
  (quote (sps:next-score sps:next:train:state-19 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-19:atom-4 sps:next:train:state-19
  (quote (sps:next-score sps:next:train:state-19 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-19:atom-5 sps:next:train:state-19
  (quote (sps:next-score sps:next:train:state-19 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-19:atom-6 sps:next:train:state-19
  (quote (sps:next-score sps:next:train:state-19 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-19:atom-7 sps:next:train:state-19
  (quote (sps:next-score sps:next:train:state-19 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-19:atom-8 sps:next:train:state-19
  (quote (sps:next-score sps:next:train:state-19 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-19:atom-9 sps:next:train:state-19
  (quote (sps:next-step sps:next:train:state-19 sps:n0)))
!(iggp:sps:classify sps:next:train:state-19:atom-10 sps:next:train:state-19
  (quote (sps:next-step sps:next:train:state-19 sps:n1)))
!(iggp:sps:classify sps:next:train:state-19:atom-11 sps:next:train:state-19
  (quote (sps:next-step sps:next:train:state-19 sps:n2)))
!(iggp:sps:classify sps:next:train:state-19:atom-12 sps:next:train:state-19
  (quote (sps:next-step sps:next:train:state-19 sps:n3)))
!(iggp:sps:classify sps:next:train:state-20:atom-1 sps:next:train:state-20
  (quote (sps:next-score sps:next:train:state-20 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-20:atom-2 sps:next:train:state-20
  (quote (sps:next-score sps:next:train:state-20 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-20:atom-3 sps:next:train:state-20
  (quote (sps:next-score sps:next:train:state-20 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-20:atom-4 sps:next:train:state-20
  (quote (sps:next-score sps:next:train:state-20 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-20:atom-5 sps:next:train:state-20
  (quote (sps:next-score sps:next:train:state-20 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-20:atom-6 sps:next:train:state-20
  (quote (sps:next-score sps:next:train:state-20 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-20:atom-7 sps:next:train:state-20
  (quote (sps:next-score sps:next:train:state-20 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-20:atom-8 sps:next:train:state-20
  (quote (sps:next-score sps:next:train:state-20 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-20:atom-9 sps:next:train:state-20
  (quote (sps:next-step sps:next:train:state-20 sps:n0)))
!(iggp:sps:classify sps:next:train:state-20:atom-10 sps:next:train:state-20
  (quote (sps:next-step sps:next:train:state-20 sps:n1)))
!(iggp:sps:classify sps:next:train:state-20:atom-11 sps:next:train:state-20
  (quote (sps:next-step sps:next:train:state-20 sps:n2)))
!(iggp:sps:classify sps:next:train:state-20:atom-12 sps:next:train:state-20
  (quote (sps:next-step sps:next:train:state-20 sps:n3)))
!(iggp:sps:classify sps:next:train:state-21:atom-1 sps:next:train:state-21
  (quote (sps:next-score sps:next:train:state-21 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-21:atom-2 sps:next:train:state-21
  (quote (sps:next-score sps:next:train:state-21 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-21:atom-3 sps:next:train:state-21
  (quote (sps:next-score sps:next:train:state-21 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-21:atom-4 sps:next:train:state-21
  (quote (sps:next-score sps:next:train:state-21 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-21:atom-5 sps:next:train:state-21
  (quote (sps:next-score sps:next:train:state-21 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-21:atom-6 sps:next:train:state-21
  (quote (sps:next-score sps:next:train:state-21 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-21:atom-7 sps:next:train:state-21
  (quote (sps:next-score sps:next:train:state-21 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-21:atom-8 sps:next:train:state-21
  (quote (sps:next-score sps:next:train:state-21 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-21:atom-9 sps:next:train:state-21
  (quote (sps:next-step sps:next:train:state-21 sps:n0)))
!(iggp:sps:classify sps:next:train:state-21:atom-10 sps:next:train:state-21
  (quote (sps:next-step sps:next:train:state-21 sps:n1)))
!(iggp:sps:classify sps:next:train:state-21:atom-11 sps:next:train:state-21
  (quote (sps:next-step sps:next:train:state-21 sps:n2)))
!(iggp:sps:classify sps:next:train:state-21:atom-12 sps:next:train:state-21
  (quote (sps:next-step sps:next:train:state-21 sps:n3)))
!(iggp:sps:classify sps:next:train:state-22:atom-1 sps:next:train:state-22
  (quote (sps:next-score sps:next:train:state-22 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-22:atom-2 sps:next:train:state-22
  (quote (sps:next-score sps:next:train:state-22 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-22:atom-3 sps:next:train:state-22
  (quote (sps:next-score sps:next:train:state-22 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-22:atom-4 sps:next:train:state-22
  (quote (sps:next-score sps:next:train:state-22 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-22:atom-5 sps:next:train:state-22
  (quote (sps:next-score sps:next:train:state-22 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-22:atom-6 sps:next:train:state-22
  (quote (sps:next-score sps:next:train:state-22 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-22:atom-7 sps:next:train:state-22
  (quote (sps:next-score sps:next:train:state-22 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-22:atom-8 sps:next:train:state-22
  (quote (sps:next-score sps:next:train:state-22 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-22:atom-9 sps:next:train:state-22
  (quote (sps:next-step sps:next:train:state-22 sps:n0)))
!(iggp:sps:classify sps:next:train:state-22:atom-10 sps:next:train:state-22
  (quote (sps:next-step sps:next:train:state-22 sps:n1)))
!(iggp:sps:classify sps:next:train:state-22:atom-11 sps:next:train:state-22
  (quote (sps:next-step sps:next:train:state-22 sps:n2)))
!(iggp:sps:classify sps:next:train:state-22:atom-12 sps:next:train:state-22
  (quote (sps:next-step sps:next:train:state-22 sps:n3)))
!(iggp:sps:classify sps:next:train:state-23:atom-1 sps:next:train:state-23
  (quote (sps:next-score sps:next:train:state-23 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-23:atom-2 sps:next:train:state-23
  (quote (sps:next-score sps:next:train:state-23 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-23:atom-3 sps:next:train:state-23
  (quote (sps:next-score sps:next:train:state-23 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-23:atom-4 sps:next:train:state-23
  (quote (sps:next-score sps:next:train:state-23 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-23:atom-5 sps:next:train:state-23
  (quote (sps:next-score sps:next:train:state-23 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-23:atom-6 sps:next:train:state-23
  (quote (sps:next-score sps:next:train:state-23 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-23:atom-7 sps:next:train:state-23
  (quote (sps:next-score sps:next:train:state-23 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-23:atom-8 sps:next:train:state-23
  (quote (sps:next-score sps:next:train:state-23 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-23:atom-9 sps:next:train:state-23
  (quote (sps:next-step sps:next:train:state-23 sps:n0)))
!(iggp:sps:classify sps:next:train:state-23:atom-10 sps:next:train:state-23
  (quote (sps:next-step sps:next:train:state-23 sps:n1)))
!(iggp:sps:classify sps:next:train:state-23:atom-11 sps:next:train:state-23
  (quote (sps:next-step sps:next:train:state-23 sps:n2)))
!(iggp:sps:classify sps:next:train:state-23:atom-12 sps:next:train:state-23
  (quote (sps:next-step sps:next:train:state-23 sps:n3)))
!(iggp:sps:classify sps:next:train:state-24:atom-1 sps:next:train:state-24
  (quote (sps:next-score sps:next:train:state-24 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-24:atom-2 sps:next:train:state-24
  (quote (sps:next-score sps:next:train:state-24 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-24:atom-3 sps:next:train:state-24
  (quote (sps:next-score sps:next:train:state-24 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-24:atom-4 sps:next:train:state-24
  (quote (sps:next-score sps:next:train:state-24 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-24:atom-5 sps:next:train:state-24
  (quote (sps:next-score sps:next:train:state-24 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-24:atom-6 sps:next:train:state-24
  (quote (sps:next-score sps:next:train:state-24 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-24:atom-7 sps:next:train:state-24
  (quote (sps:next-score sps:next:train:state-24 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-24:atom-8 sps:next:train:state-24
  (quote (sps:next-score sps:next:train:state-24 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-24:atom-9 sps:next:train:state-24
  (quote (sps:next-step sps:next:train:state-24 sps:n0)))
!(iggp:sps:classify sps:next:train:state-24:atom-10 sps:next:train:state-24
  (quote (sps:next-step sps:next:train:state-24 sps:n1)))
!(iggp:sps:classify sps:next:train:state-24:atom-11 sps:next:train:state-24
  (quote (sps:next-step sps:next:train:state-24 sps:n2)))
!(iggp:sps:classify sps:next:train:state-24:atom-12 sps:next:train:state-24
  (quote (sps:next-step sps:next:train:state-24 sps:n3)))
!(iggp:sps:classify sps:next:train:state-25:atom-1 sps:next:train:state-25
  (quote (sps:next-score sps:next:train:state-25 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-25:atom-2 sps:next:train:state-25
  (quote (sps:next-score sps:next:train:state-25 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-25:atom-3 sps:next:train:state-25
  (quote (sps:next-score sps:next:train:state-25 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-25:atom-4 sps:next:train:state-25
  (quote (sps:next-score sps:next:train:state-25 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-25:atom-5 sps:next:train:state-25
  (quote (sps:next-score sps:next:train:state-25 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-25:atom-6 sps:next:train:state-25
  (quote (sps:next-score sps:next:train:state-25 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-25:atom-7 sps:next:train:state-25
  (quote (sps:next-score sps:next:train:state-25 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-25:atom-8 sps:next:train:state-25
  (quote (sps:next-score sps:next:train:state-25 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-25:atom-9 sps:next:train:state-25
  (quote (sps:next-step sps:next:train:state-25 sps:n0)))
!(iggp:sps:classify sps:next:train:state-25:atom-10 sps:next:train:state-25
  (quote (sps:next-step sps:next:train:state-25 sps:n1)))
!(iggp:sps:classify sps:next:train:state-25:atom-11 sps:next:train:state-25
  (quote (sps:next-step sps:next:train:state-25 sps:n2)))
!(iggp:sps:classify sps:next:train:state-25:atom-12 sps:next:train:state-25
  (quote (sps:next-step sps:next:train:state-25 sps:n3)))
!(iggp:sps:classify sps:next:train:state-26:atom-1 sps:next:train:state-26
  (quote (sps:next-score sps:next:train:state-26 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-26:atom-2 sps:next:train:state-26
  (quote (sps:next-score sps:next:train:state-26 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-26:atom-3 sps:next:train:state-26
  (quote (sps:next-score sps:next:train:state-26 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-26:atom-4 sps:next:train:state-26
  (quote (sps:next-score sps:next:train:state-26 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-26:atom-5 sps:next:train:state-26
  (quote (sps:next-score sps:next:train:state-26 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-26:atom-6 sps:next:train:state-26
  (quote (sps:next-score sps:next:train:state-26 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-26:atom-7 sps:next:train:state-26
  (quote (sps:next-score sps:next:train:state-26 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-26:atom-8 sps:next:train:state-26
  (quote (sps:next-score sps:next:train:state-26 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-26:atom-9 sps:next:train:state-26
  (quote (sps:next-step sps:next:train:state-26 sps:n0)))
!(iggp:sps:classify sps:next:train:state-26:atom-10 sps:next:train:state-26
  (quote (sps:next-step sps:next:train:state-26 sps:n1)))
!(iggp:sps:classify sps:next:train:state-26:atom-11 sps:next:train:state-26
  (quote (sps:next-step sps:next:train:state-26 sps:n2)))
!(iggp:sps:classify sps:next:train:state-26:atom-12 sps:next:train:state-26
  (quote (sps:next-step sps:next:train:state-26 sps:n3)))
!(iggp:sps:classify sps:next:train:state-27:atom-1 sps:next:train:state-27
  (quote (sps:next-score sps:next:train:state-27 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-27:atom-2 sps:next:train:state-27
  (quote (sps:next-score sps:next:train:state-27 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-27:atom-3 sps:next:train:state-27
  (quote (sps:next-score sps:next:train:state-27 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-27:atom-4 sps:next:train:state-27
  (quote (sps:next-score sps:next:train:state-27 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-27:atom-5 sps:next:train:state-27
  (quote (sps:next-score sps:next:train:state-27 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-27:atom-6 sps:next:train:state-27
  (quote (sps:next-score sps:next:train:state-27 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-27:atom-7 sps:next:train:state-27
  (quote (sps:next-score sps:next:train:state-27 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-27:atom-8 sps:next:train:state-27
  (quote (sps:next-score sps:next:train:state-27 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-27:atom-9 sps:next:train:state-27
  (quote (sps:next-step sps:next:train:state-27 sps:n0)))
!(iggp:sps:classify sps:next:train:state-27:atom-10 sps:next:train:state-27
  (quote (sps:next-step sps:next:train:state-27 sps:n1)))
!(iggp:sps:classify sps:next:train:state-27:atom-11 sps:next:train:state-27
  (quote (sps:next-step sps:next:train:state-27 sps:n2)))
!(iggp:sps:classify sps:next:train:state-27:atom-12 sps:next:train:state-27
  (quote (sps:next-step sps:next:train:state-27 sps:n3)))
!(iggp:sps:classify sps:next:train:state-28:atom-1 sps:next:train:state-28
  (quote (sps:next-score sps:next:train:state-28 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-28:atom-2 sps:next:train:state-28
  (quote (sps:next-score sps:next:train:state-28 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-28:atom-3 sps:next:train:state-28
  (quote (sps:next-score sps:next:train:state-28 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-28:atom-4 sps:next:train:state-28
  (quote (sps:next-score sps:next:train:state-28 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-28:atom-5 sps:next:train:state-28
  (quote (sps:next-score sps:next:train:state-28 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-28:atom-6 sps:next:train:state-28
  (quote (sps:next-score sps:next:train:state-28 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-28:atom-7 sps:next:train:state-28
  (quote (sps:next-score sps:next:train:state-28 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-28:atom-8 sps:next:train:state-28
  (quote (sps:next-score sps:next:train:state-28 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-28:atom-9 sps:next:train:state-28
  (quote (sps:next-step sps:next:train:state-28 sps:n0)))
!(iggp:sps:classify sps:next:train:state-28:atom-10 sps:next:train:state-28
  (quote (sps:next-step sps:next:train:state-28 sps:n1)))
!(iggp:sps:classify sps:next:train:state-28:atom-11 sps:next:train:state-28
  (quote (sps:next-step sps:next:train:state-28 sps:n2)))
!(iggp:sps:classify sps:next:train:state-28:atom-12 sps:next:train:state-28
  (quote (sps:next-step sps:next:train:state-28 sps:n3)))
!(iggp:sps:classify sps:next:train:state-29:atom-1 sps:next:train:state-29
  (quote (sps:next-score sps:next:train:state-29 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-29:atom-2 sps:next:train:state-29
  (quote (sps:next-score sps:next:train:state-29 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-29:atom-3 sps:next:train:state-29
  (quote (sps:next-score sps:next:train:state-29 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-29:atom-4 sps:next:train:state-29
  (quote (sps:next-score sps:next:train:state-29 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-29:atom-5 sps:next:train:state-29
  (quote (sps:next-score sps:next:train:state-29 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-29:atom-6 sps:next:train:state-29
  (quote (sps:next-score sps:next:train:state-29 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-29:atom-7 sps:next:train:state-29
  (quote (sps:next-score sps:next:train:state-29 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-29:atom-8 sps:next:train:state-29
  (quote (sps:next-score sps:next:train:state-29 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-29:atom-9 sps:next:train:state-29
  (quote (sps:next-step sps:next:train:state-29 sps:n0)))
!(iggp:sps:classify sps:next:train:state-29:atom-10 sps:next:train:state-29
  (quote (sps:next-step sps:next:train:state-29 sps:n1)))
!(iggp:sps:classify sps:next:train:state-29:atom-11 sps:next:train:state-29
  (quote (sps:next-step sps:next:train:state-29 sps:n2)))
!(iggp:sps:classify sps:next:train:state-29:atom-12 sps:next:train:state-29
  (quote (sps:next-step sps:next:train:state-29 sps:n3)))
!(iggp:sps:classify sps:next:train:state-30:atom-1 sps:next:train:state-30
  (quote (sps:next-score sps:next:train:state-30 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-30:atom-2 sps:next:train:state-30
  (quote (sps:next-score sps:next:train:state-30 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-30:atom-3 sps:next:train:state-30
  (quote (sps:next-score sps:next:train:state-30 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-30:atom-4 sps:next:train:state-30
  (quote (sps:next-score sps:next:train:state-30 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-30:atom-5 sps:next:train:state-30
  (quote (sps:next-score sps:next:train:state-30 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-30:atom-6 sps:next:train:state-30
  (quote (sps:next-score sps:next:train:state-30 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-30:atom-7 sps:next:train:state-30
  (quote (sps:next-score sps:next:train:state-30 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-30:atom-8 sps:next:train:state-30
  (quote (sps:next-score sps:next:train:state-30 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-30:atom-9 sps:next:train:state-30
  (quote (sps:next-step sps:next:train:state-30 sps:n0)))
!(iggp:sps:classify sps:next:train:state-30:atom-10 sps:next:train:state-30
  (quote (sps:next-step sps:next:train:state-30 sps:n1)))
!(iggp:sps:classify sps:next:train:state-30:atom-11 sps:next:train:state-30
  (quote (sps:next-step sps:next:train:state-30 sps:n2)))
!(iggp:sps:classify sps:next:train:state-30:atom-12 sps:next:train:state-30
  (quote (sps:next-step sps:next:train:state-30 sps:n3)))
!(iggp:sps:classify sps:next:train:state-31:atom-1 sps:next:train:state-31
  (quote (sps:next-score sps:next:train:state-31 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-31:atom-2 sps:next:train:state-31
  (quote (sps:next-score sps:next:train:state-31 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-31:atom-3 sps:next:train:state-31
  (quote (sps:next-score sps:next:train:state-31 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-31:atom-4 sps:next:train:state-31
  (quote (sps:next-score sps:next:train:state-31 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-31:atom-5 sps:next:train:state-31
  (quote (sps:next-score sps:next:train:state-31 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-31:atom-6 sps:next:train:state-31
  (quote (sps:next-score sps:next:train:state-31 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-31:atom-7 sps:next:train:state-31
  (quote (sps:next-score sps:next:train:state-31 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-31:atom-8 sps:next:train:state-31
  (quote (sps:next-score sps:next:train:state-31 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-31:atom-9 sps:next:train:state-31
  (quote (sps:next-step sps:next:train:state-31 sps:n0)))
!(iggp:sps:classify sps:next:train:state-31:atom-10 sps:next:train:state-31
  (quote (sps:next-step sps:next:train:state-31 sps:n1)))
!(iggp:sps:classify sps:next:train:state-31:atom-11 sps:next:train:state-31
  (quote (sps:next-step sps:next:train:state-31 sps:n2)))
!(iggp:sps:classify sps:next:train:state-31:atom-12 sps:next:train:state-31
  (quote (sps:next-step sps:next:train:state-31 sps:n3)))
!(iggp:sps:classify sps:next:train:state-32:atom-1 sps:next:train:state-32
  (quote (sps:next-score sps:next:train:state-32 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-32:atom-2 sps:next:train:state-32
  (quote (sps:next-score sps:next:train:state-32 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-32:atom-3 sps:next:train:state-32
  (quote (sps:next-score sps:next:train:state-32 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-32:atom-4 sps:next:train:state-32
  (quote (sps:next-score sps:next:train:state-32 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-32:atom-5 sps:next:train:state-32
  (quote (sps:next-score sps:next:train:state-32 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-32:atom-6 sps:next:train:state-32
  (quote (sps:next-score sps:next:train:state-32 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-32:atom-7 sps:next:train:state-32
  (quote (sps:next-score sps:next:train:state-32 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-32:atom-8 sps:next:train:state-32
  (quote (sps:next-score sps:next:train:state-32 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-32:atom-9 sps:next:train:state-32
  (quote (sps:next-step sps:next:train:state-32 sps:n0)))
!(iggp:sps:classify sps:next:train:state-32:atom-10 sps:next:train:state-32
  (quote (sps:next-step sps:next:train:state-32 sps:n1)))
!(iggp:sps:classify sps:next:train:state-32:atom-11 sps:next:train:state-32
  (quote (sps:next-step sps:next:train:state-32 sps:n2)))
!(iggp:sps:classify sps:next:train:state-32:atom-12 sps:next:train:state-32
  (quote (sps:next-step sps:next:train:state-32 sps:n3)))
!(iggp:sps:classify sps:next:train:state-33:atom-1 sps:next:train:state-33
  (quote (sps:next-score sps:next:train:state-33 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-33:atom-2 sps:next:train:state-33
  (quote (sps:next-score sps:next:train:state-33 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-33:atom-3 sps:next:train:state-33
  (quote (sps:next-score sps:next:train:state-33 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-33:atom-4 sps:next:train:state-33
  (quote (sps:next-score sps:next:train:state-33 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-33:atom-5 sps:next:train:state-33
  (quote (sps:next-score sps:next:train:state-33 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-33:atom-6 sps:next:train:state-33
  (quote (sps:next-score sps:next:train:state-33 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-33:atom-7 sps:next:train:state-33
  (quote (sps:next-score sps:next:train:state-33 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-33:atom-8 sps:next:train:state-33
  (quote (sps:next-score sps:next:train:state-33 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-33:atom-9 sps:next:train:state-33
  (quote (sps:next-step sps:next:train:state-33 sps:n0)))
!(iggp:sps:classify sps:next:train:state-33:atom-10 sps:next:train:state-33
  (quote (sps:next-step sps:next:train:state-33 sps:n1)))
!(iggp:sps:classify sps:next:train:state-33:atom-11 sps:next:train:state-33
  (quote (sps:next-step sps:next:train:state-33 sps:n2)))
!(iggp:sps:classify sps:next:train:state-33:atom-12 sps:next:train:state-33
  (quote (sps:next-step sps:next:train:state-33 sps:n3)))
!(iggp:sps:classify sps:next:train:state-34:atom-1 sps:next:train:state-34
  (quote (sps:next-score sps:next:train:state-34 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-34:atom-2 sps:next:train:state-34
  (quote (sps:next-score sps:next:train:state-34 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-34:atom-3 sps:next:train:state-34
  (quote (sps:next-score sps:next:train:state-34 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-34:atom-4 sps:next:train:state-34
  (quote (sps:next-score sps:next:train:state-34 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-34:atom-5 sps:next:train:state-34
  (quote (sps:next-score sps:next:train:state-34 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-34:atom-6 sps:next:train:state-34
  (quote (sps:next-score sps:next:train:state-34 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-34:atom-7 sps:next:train:state-34
  (quote (sps:next-score sps:next:train:state-34 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-34:atom-8 sps:next:train:state-34
  (quote (sps:next-score sps:next:train:state-34 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-34:atom-9 sps:next:train:state-34
  (quote (sps:next-step sps:next:train:state-34 sps:n0)))
!(iggp:sps:classify sps:next:train:state-34:atom-10 sps:next:train:state-34
  (quote (sps:next-step sps:next:train:state-34 sps:n1)))
!(iggp:sps:classify sps:next:train:state-34:atom-11 sps:next:train:state-34
  (quote (sps:next-step sps:next:train:state-34 sps:n2)))
!(iggp:sps:classify sps:next:train:state-34:atom-12 sps:next:train:state-34
  (quote (sps:next-step sps:next:train:state-34 sps:n3)))
!(iggp:sps:classify sps:next:train:state-35:atom-1 sps:next:train:state-35
  (quote (sps:next-score sps:next:train:state-35 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-35:atom-2 sps:next:train:state-35
  (quote (sps:next-score sps:next:train:state-35 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-35:atom-3 sps:next:train:state-35
  (quote (sps:next-score sps:next:train:state-35 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-35:atom-4 sps:next:train:state-35
  (quote (sps:next-score sps:next:train:state-35 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-35:atom-5 sps:next:train:state-35
  (quote (sps:next-score sps:next:train:state-35 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-35:atom-6 sps:next:train:state-35
  (quote (sps:next-score sps:next:train:state-35 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-35:atom-7 sps:next:train:state-35
  (quote (sps:next-score sps:next:train:state-35 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-35:atom-8 sps:next:train:state-35
  (quote (sps:next-score sps:next:train:state-35 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-35:atom-9 sps:next:train:state-35
  (quote (sps:next-step sps:next:train:state-35 sps:n0)))
!(iggp:sps:classify sps:next:train:state-35:atom-10 sps:next:train:state-35
  (quote (sps:next-step sps:next:train:state-35 sps:n1)))
!(iggp:sps:classify sps:next:train:state-35:atom-11 sps:next:train:state-35
  (quote (sps:next-step sps:next:train:state-35 sps:n2)))
!(iggp:sps:classify sps:next:train:state-35:atom-12 sps:next:train:state-35
  (quote (sps:next-step sps:next:train:state-35 sps:n3)))
!(iggp:sps:classify sps:next:train:state-36:atom-1 sps:next:train:state-36
  (quote (sps:next-score sps:next:train:state-36 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-36:atom-2 sps:next:train:state-36
  (quote (sps:next-score sps:next:train:state-36 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-36:atom-3 sps:next:train:state-36
  (quote (sps:next-score sps:next:train:state-36 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-36:atom-4 sps:next:train:state-36
  (quote (sps:next-score sps:next:train:state-36 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-36:atom-5 sps:next:train:state-36
  (quote (sps:next-score sps:next:train:state-36 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-36:atom-6 sps:next:train:state-36
  (quote (sps:next-score sps:next:train:state-36 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-36:atom-7 sps:next:train:state-36
  (quote (sps:next-score sps:next:train:state-36 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-36:atom-8 sps:next:train:state-36
  (quote (sps:next-score sps:next:train:state-36 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-36:atom-9 sps:next:train:state-36
  (quote (sps:next-step sps:next:train:state-36 sps:n0)))
!(iggp:sps:classify sps:next:train:state-36:atom-10 sps:next:train:state-36
  (quote (sps:next-step sps:next:train:state-36 sps:n1)))
!(iggp:sps:classify sps:next:train:state-36:atom-11 sps:next:train:state-36
  (quote (sps:next-step sps:next:train:state-36 sps:n2)))
!(iggp:sps:classify sps:next:train:state-36:atom-12 sps:next:train:state-36
  (quote (sps:next-step sps:next:train:state-36 sps:n3)))
!(iggp:sps:classify sps:next:train:state-37:atom-1 sps:next:train:state-37
  (quote (sps:next-score sps:next:train:state-37 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-37:atom-2 sps:next:train:state-37
  (quote (sps:next-score sps:next:train:state-37 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-37:atom-3 sps:next:train:state-37
  (quote (sps:next-score sps:next:train:state-37 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-37:atom-4 sps:next:train:state-37
  (quote (sps:next-score sps:next:train:state-37 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-37:atom-5 sps:next:train:state-37
  (quote (sps:next-score sps:next:train:state-37 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-37:atom-6 sps:next:train:state-37
  (quote (sps:next-score sps:next:train:state-37 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-37:atom-7 sps:next:train:state-37
  (quote (sps:next-score sps:next:train:state-37 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-37:atom-8 sps:next:train:state-37
  (quote (sps:next-score sps:next:train:state-37 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-37:atom-9 sps:next:train:state-37
  (quote (sps:next-step sps:next:train:state-37 sps:n0)))
!(iggp:sps:classify sps:next:train:state-37:atom-10 sps:next:train:state-37
  (quote (sps:next-step sps:next:train:state-37 sps:n1)))
!(iggp:sps:classify sps:next:train:state-37:atom-11 sps:next:train:state-37
  (quote (sps:next-step sps:next:train:state-37 sps:n2)))
!(iggp:sps:classify sps:next:train:state-37:atom-12 sps:next:train:state-37
  (quote (sps:next-step sps:next:train:state-37 sps:n3)))
!(iggp:sps:classify sps:next:train:state-38:atom-1 sps:next:train:state-38
  (quote (sps:next-score sps:next:train:state-38 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-38:atom-2 sps:next:train:state-38
  (quote (sps:next-score sps:next:train:state-38 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-38:atom-3 sps:next:train:state-38
  (quote (sps:next-score sps:next:train:state-38 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-38:atom-4 sps:next:train:state-38
  (quote (sps:next-score sps:next:train:state-38 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-38:atom-5 sps:next:train:state-38
  (quote (sps:next-score sps:next:train:state-38 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-38:atom-6 sps:next:train:state-38
  (quote (sps:next-score sps:next:train:state-38 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-38:atom-7 sps:next:train:state-38
  (quote (sps:next-score sps:next:train:state-38 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-38:atom-8 sps:next:train:state-38
  (quote (sps:next-score sps:next:train:state-38 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-38:atom-9 sps:next:train:state-38
  (quote (sps:next-step sps:next:train:state-38 sps:n0)))
!(iggp:sps:classify sps:next:train:state-38:atom-10 sps:next:train:state-38
  (quote (sps:next-step sps:next:train:state-38 sps:n1)))
!(iggp:sps:classify sps:next:train:state-38:atom-11 sps:next:train:state-38
  (quote (sps:next-step sps:next:train:state-38 sps:n2)))
!(iggp:sps:classify sps:next:train:state-38:atom-12 sps:next:train:state-38
  (quote (sps:next-step sps:next:train:state-38 sps:n3)))
!(iggp:sps:classify sps:next:train:state-39:atom-1 sps:next:train:state-39
  (quote (sps:next-score sps:next:train:state-39 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-39:atom-2 sps:next:train:state-39
  (quote (sps:next-score sps:next:train:state-39 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-39:atom-3 sps:next:train:state-39
  (quote (sps:next-score sps:next:train:state-39 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-39:atom-4 sps:next:train:state-39
  (quote (sps:next-score sps:next:train:state-39 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-39:atom-5 sps:next:train:state-39
  (quote (sps:next-score sps:next:train:state-39 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-39:atom-6 sps:next:train:state-39
  (quote (sps:next-score sps:next:train:state-39 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-39:atom-7 sps:next:train:state-39
  (quote (sps:next-score sps:next:train:state-39 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-39:atom-8 sps:next:train:state-39
  (quote (sps:next-score sps:next:train:state-39 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-39:atom-9 sps:next:train:state-39
  (quote (sps:next-step sps:next:train:state-39 sps:n0)))
!(iggp:sps:classify sps:next:train:state-39:atom-10 sps:next:train:state-39
  (quote (sps:next-step sps:next:train:state-39 sps:n1)))
!(iggp:sps:classify sps:next:train:state-39:atom-11 sps:next:train:state-39
  (quote (sps:next-step sps:next:train:state-39 sps:n2)))
!(iggp:sps:classify sps:next:train:state-39:atom-12 sps:next:train:state-39
  (quote (sps:next-step sps:next:train:state-39 sps:n3)))
!(iggp:sps:classify sps:next:train:state-40:atom-1 sps:next:train:state-40
  (quote (sps:next-score sps:next:train:state-40 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-40:atom-2 sps:next:train:state-40
  (quote (sps:next-score sps:next:train:state-40 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-40:atom-3 sps:next:train:state-40
  (quote (sps:next-score sps:next:train:state-40 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-40:atom-4 sps:next:train:state-40
  (quote (sps:next-score sps:next:train:state-40 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-40:atom-5 sps:next:train:state-40
  (quote (sps:next-score sps:next:train:state-40 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-40:atom-6 sps:next:train:state-40
  (quote (sps:next-score sps:next:train:state-40 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-40:atom-7 sps:next:train:state-40
  (quote (sps:next-score sps:next:train:state-40 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-40:atom-8 sps:next:train:state-40
  (quote (sps:next-score sps:next:train:state-40 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-40:atom-9 sps:next:train:state-40
  (quote (sps:next-step sps:next:train:state-40 sps:n0)))
!(iggp:sps:classify sps:next:train:state-40:atom-10 sps:next:train:state-40
  (quote (sps:next-step sps:next:train:state-40 sps:n1)))
!(iggp:sps:classify sps:next:train:state-40:atom-11 sps:next:train:state-40
  (quote (sps:next-step sps:next:train:state-40 sps:n2)))
!(iggp:sps:classify sps:next:train:state-40:atom-12 sps:next:train:state-40
  (quote (sps:next-step sps:next:train:state-40 sps:n3)))
!(iggp:sps:classify sps:next:train:state-41:atom-1 sps:next:train:state-41
  (quote (sps:next-score sps:next:train:state-41 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-41:atom-2 sps:next:train:state-41
  (quote (sps:next-score sps:next:train:state-41 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-41:atom-3 sps:next:train:state-41
  (quote (sps:next-score sps:next:train:state-41 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-41:atom-4 sps:next:train:state-41
  (quote (sps:next-score sps:next:train:state-41 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-41:atom-5 sps:next:train:state-41
  (quote (sps:next-score sps:next:train:state-41 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-41:atom-6 sps:next:train:state-41
  (quote (sps:next-score sps:next:train:state-41 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-41:atom-7 sps:next:train:state-41
  (quote (sps:next-score sps:next:train:state-41 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-41:atom-8 sps:next:train:state-41
  (quote (sps:next-score sps:next:train:state-41 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-41:atom-9 sps:next:train:state-41
  (quote (sps:next-step sps:next:train:state-41 sps:n0)))
!(iggp:sps:classify sps:next:train:state-41:atom-10 sps:next:train:state-41
  (quote (sps:next-step sps:next:train:state-41 sps:n1)))
!(iggp:sps:classify sps:next:train:state-41:atom-11 sps:next:train:state-41
  (quote (sps:next-step sps:next:train:state-41 sps:n2)))
!(iggp:sps:classify sps:next:train:state-41:atom-12 sps:next:train:state-41
  (quote (sps:next-step sps:next:train:state-41 sps:n3)))
!(iggp:sps:classify sps:next:train:state-42:atom-1 sps:next:train:state-42
  (quote (sps:next-score sps:next:train:state-42 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-42:atom-2 sps:next:train:state-42
  (quote (sps:next-score sps:next:train:state-42 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-42:atom-3 sps:next:train:state-42
  (quote (sps:next-score sps:next:train:state-42 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-42:atom-4 sps:next:train:state-42
  (quote (sps:next-score sps:next:train:state-42 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-42:atom-5 sps:next:train:state-42
  (quote (sps:next-score sps:next:train:state-42 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-42:atom-6 sps:next:train:state-42
  (quote (sps:next-score sps:next:train:state-42 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-42:atom-7 sps:next:train:state-42
  (quote (sps:next-score sps:next:train:state-42 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-42:atom-8 sps:next:train:state-42
  (quote (sps:next-score sps:next:train:state-42 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-42:atom-9 sps:next:train:state-42
  (quote (sps:next-step sps:next:train:state-42 sps:n0)))
!(iggp:sps:classify sps:next:train:state-42:atom-10 sps:next:train:state-42
  (quote (sps:next-step sps:next:train:state-42 sps:n1)))
!(iggp:sps:classify sps:next:train:state-42:atom-11 sps:next:train:state-42
  (quote (sps:next-step sps:next:train:state-42 sps:n2)))
!(iggp:sps:classify sps:next:train:state-42:atom-12 sps:next:train:state-42
  (quote (sps:next-step sps:next:train:state-42 sps:n3)))
!(iggp:sps:classify sps:next:train:state-43:atom-1 sps:next:train:state-43
  (quote (sps:next-score sps:next:train:state-43 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-43:atom-2 sps:next:train:state-43
  (quote (sps:next-score sps:next:train:state-43 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-43:atom-3 sps:next:train:state-43
  (quote (sps:next-score sps:next:train:state-43 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-43:atom-4 sps:next:train:state-43
  (quote (sps:next-score sps:next:train:state-43 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-43:atom-5 sps:next:train:state-43
  (quote (sps:next-score sps:next:train:state-43 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-43:atom-6 sps:next:train:state-43
  (quote (sps:next-score sps:next:train:state-43 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-43:atom-7 sps:next:train:state-43
  (quote (sps:next-score sps:next:train:state-43 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-43:atom-8 sps:next:train:state-43
  (quote (sps:next-score sps:next:train:state-43 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-43:atom-9 sps:next:train:state-43
  (quote (sps:next-step sps:next:train:state-43 sps:n0)))
!(iggp:sps:classify sps:next:train:state-43:atom-10 sps:next:train:state-43
  (quote (sps:next-step sps:next:train:state-43 sps:n1)))
!(iggp:sps:classify sps:next:train:state-43:atom-11 sps:next:train:state-43
  (quote (sps:next-step sps:next:train:state-43 sps:n2)))
!(iggp:sps:classify sps:next:train:state-43:atom-12 sps:next:train:state-43
  (quote (sps:next-step sps:next:train:state-43 sps:n3)))
!(iggp:sps:classify sps:next:train:state-44:atom-1 sps:next:train:state-44
  (quote (sps:next-score sps:next:train:state-44 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-44:atom-2 sps:next:train:state-44
  (quote (sps:next-score sps:next:train:state-44 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-44:atom-3 sps:next:train:state-44
  (quote (sps:next-score sps:next:train:state-44 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-44:atom-4 sps:next:train:state-44
  (quote (sps:next-score sps:next:train:state-44 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-44:atom-5 sps:next:train:state-44
  (quote (sps:next-score sps:next:train:state-44 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-44:atom-6 sps:next:train:state-44
  (quote (sps:next-score sps:next:train:state-44 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-44:atom-7 sps:next:train:state-44
  (quote (sps:next-score sps:next:train:state-44 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-44:atom-8 sps:next:train:state-44
  (quote (sps:next-score sps:next:train:state-44 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-44:atom-9 sps:next:train:state-44
  (quote (sps:next-step sps:next:train:state-44 sps:n0)))
!(iggp:sps:classify sps:next:train:state-44:atom-10 sps:next:train:state-44
  (quote (sps:next-step sps:next:train:state-44 sps:n1)))
!(iggp:sps:classify sps:next:train:state-44:atom-11 sps:next:train:state-44
  (quote (sps:next-step sps:next:train:state-44 sps:n2)))
!(iggp:sps:classify sps:next:train:state-44:atom-12 sps:next:train:state-44
  (quote (sps:next-step sps:next:train:state-44 sps:n3)))
!(iggp:sps:classify sps:next:train:state-45:atom-1 sps:next:train:state-45
  (quote (sps:next-score sps:next:train:state-45 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-45:atom-2 sps:next:train:state-45
  (quote (sps:next-score sps:next:train:state-45 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-45:atom-3 sps:next:train:state-45
  (quote (sps:next-score sps:next:train:state-45 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-45:atom-4 sps:next:train:state-45
  (quote (sps:next-score sps:next:train:state-45 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-45:atom-5 sps:next:train:state-45
  (quote (sps:next-score sps:next:train:state-45 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-45:atom-6 sps:next:train:state-45
  (quote (sps:next-score sps:next:train:state-45 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-45:atom-7 sps:next:train:state-45
  (quote (sps:next-score sps:next:train:state-45 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-45:atom-8 sps:next:train:state-45
  (quote (sps:next-score sps:next:train:state-45 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-45:atom-9 sps:next:train:state-45
  (quote (sps:next-step sps:next:train:state-45 sps:n0)))
!(iggp:sps:classify sps:next:train:state-45:atom-10 sps:next:train:state-45
  (quote (sps:next-step sps:next:train:state-45 sps:n1)))
!(iggp:sps:classify sps:next:train:state-45:atom-11 sps:next:train:state-45
  (quote (sps:next-step sps:next:train:state-45 sps:n2)))
!(iggp:sps:classify sps:next:train:state-45:atom-12 sps:next:train:state-45
  (quote (sps:next-step sps:next:train:state-45 sps:n3)))
!(iggp:sps:classify sps:next:train:state-46:atom-1 sps:next:train:state-46
  (quote (sps:next-score sps:next:train:state-46 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-46:atom-2 sps:next:train:state-46
  (quote (sps:next-score sps:next:train:state-46 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-46:atom-3 sps:next:train:state-46
  (quote (sps:next-score sps:next:train:state-46 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-46:atom-4 sps:next:train:state-46
  (quote (sps:next-score sps:next:train:state-46 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-46:atom-5 sps:next:train:state-46
  (quote (sps:next-score sps:next:train:state-46 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-46:atom-6 sps:next:train:state-46
  (quote (sps:next-score sps:next:train:state-46 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-46:atom-7 sps:next:train:state-46
  (quote (sps:next-score sps:next:train:state-46 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-46:atom-8 sps:next:train:state-46
  (quote (sps:next-score sps:next:train:state-46 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-46:atom-9 sps:next:train:state-46
  (quote (sps:next-step sps:next:train:state-46 sps:n0)))
!(iggp:sps:classify sps:next:train:state-46:atom-10 sps:next:train:state-46
  (quote (sps:next-step sps:next:train:state-46 sps:n1)))
!(iggp:sps:classify sps:next:train:state-46:atom-11 sps:next:train:state-46
  (quote (sps:next-step sps:next:train:state-46 sps:n2)))
!(iggp:sps:classify sps:next:train:state-46:atom-12 sps:next:train:state-46
  (quote (sps:next-step sps:next:train:state-46 sps:n3)))
!(iggp:sps:classify sps:next:train:state-47:atom-1 sps:next:train:state-47
  (quote (sps:next-score sps:next:train:state-47 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-47:atom-2 sps:next:train:state-47
  (quote (sps:next-score sps:next:train:state-47 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-47:atom-3 sps:next:train:state-47
  (quote (sps:next-score sps:next:train:state-47 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-47:atom-4 sps:next:train:state-47
  (quote (sps:next-score sps:next:train:state-47 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-47:atom-5 sps:next:train:state-47
  (quote (sps:next-score sps:next:train:state-47 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-47:atom-6 sps:next:train:state-47
  (quote (sps:next-score sps:next:train:state-47 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-47:atom-7 sps:next:train:state-47
  (quote (sps:next-score sps:next:train:state-47 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-47:atom-8 sps:next:train:state-47
  (quote (sps:next-score sps:next:train:state-47 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-47:atom-9 sps:next:train:state-47
  (quote (sps:next-step sps:next:train:state-47 sps:n0)))
!(iggp:sps:classify sps:next:train:state-47:atom-10 sps:next:train:state-47
  (quote (sps:next-step sps:next:train:state-47 sps:n1)))
!(iggp:sps:classify sps:next:train:state-47:atom-11 sps:next:train:state-47
  (quote (sps:next-step sps:next:train:state-47 sps:n2)))
!(iggp:sps:classify sps:next:train:state-47:atom-12 sps:next:train:state-47
  (quote (sps:next-step sps:next:train:state-47 sps:n3)))
!(iggp:sps:classify sps:next:train:state-48:atom-1 sps:next:train:state-48
  (quote (sps:next-score sps:next:train:state-48 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-48:atom-2 sps:next:train:state-48
  (quote (sps:next-score sps:next:train:state-48 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-48:atom-3 sps:next:train:state-48
  (quote (sps:next-score sps:next:train:state-48 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-48:atom-4 sps:next:train:state-48
  (quote (sps:next-score sps:next:train:state-48 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-48:atom-5 sps:next:train:state-48
  (quote (sps:next-score sps:next:train:state-48 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-48:atom-6 sps:next:train:state-48
  (quote (sps:next-score sps:next:train:state-48 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-48:atom-7 sps:next:train:state-48
  (quote (sps:next-score sps:next:train:state-48 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-48:atom-8 sps:next:train:state-48
  (quote (sps:next-score sps:next:train:state-48 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-48:atom-9 sps:next:train:state-48
  (quote (sps:next-step sps:next:train:state-48 sps:n0)))
!(iggp:sps:classify sps:next:train:state-48:atom-10 sps:next:train:state-48
  (quote (sps:next-step sps:next:train:state-48 sps:n1)))
!(iggp:sps:classify sps:next:train:state-48:atom-11 sps:next:train:state-48
  (quote (sps:next-step sps:next:train:state-48 sps:n2)))
!(iggp:sps:classify sps:next:train:state-48:atom-12 sps:next:train:state-48
  (quote (sps:next-step sps:next:train:state-48 sps:n3)))
!(iggp:sps:classify sps:next:train:state-49:atom-1 sps:next:train:state-49
  (quote (sps:next-score sps:next:train:state-49 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-49:atom-2 sps:next:train:state-49
  (quote (sps:next-score sps:next:train:state-49 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-49:atom-3 sps:next:train:state-49
  (quote (sps:next-score sps:next:train:state-49 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-49:atom-4 sps:next:train:state-49
  (quote (sps:next-score sps:next:train:state-49 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-49:atom-5 sps:next:train:state-49
  (quote (sps:next-score sps:next:train:state-49 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-49:atom-6 sps:next:train:state-49
  (quote (sps:next-score sps:next:train:state-49 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-49:atom-7 sps:next:train:state-49
  (quote (sps:next-score sps:next:train:state-49 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-49:atom-8 sps:next:train:state-49
  (quote (sps:next-score sps:next:train:state-49 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-49:atom-9 sps:next:train:state-49
  (quote (sps:next-step sps:next:train:state-49 sps:n0)))
!(iggp:sps:classify sps:next:train:state-49:atom-10 sps:next:train:state-49
  (quote (sps:next-step sps:next:train:state-49 sps:n1)))
!(iggp:sps:classify sps:next:train:state-49:atom-11 sps:next:train:state-49
  (quote (sps:next-step sps:next:train:state-49 sps:n2)))
!(iggp:sps:classify sps:next:train:state-49:atom-12 sps:next:train:state-49
  (quote (sps:next-step sps:next:train:state-49 sps:n3)))
!(iggp:sps:classify sps:next:train:state-50:atom-1 sps:next:train:state-50
  (quote (sps:next-score sps:next:train:state-50 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-50:atom-2 sps:next:train:state-50
  (quote (sps:next-score sps:next:train:state-50 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-50:atom-3 sps:next:train:state-50
  (quote (sps:next-score sps:next:train:state-50 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-50:atom-4 sps:next:train:state-50
  (quote (sps:next-score sps:next:train:state-50 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-50:atom-5 sps:next:train:state-50
  (quote (sps:next-score sps:next:train:state-50 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-50:atom-6 sps:next:train:state-50
  (quote (sps:next-score sps:next:train:state-50 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-50:atom-7 sps:next:train:state-50
  (quote (sps:next-score sps:next:train:state-50 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-50:atom-8 sps:next:train:state-50
  (quote (sps:next-score sps:next:train:state-50 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-50:atom-9 sps:next:train:state-50
  (quote (sps:next-step sps:next:train:state-50 sps:n0)))
!(iggp:sps:classify sps:next:train:state-50:atom-10 sps:next:train:state-50
  (quote (sps:next-step sps:next:train:state-50 sps:n1)))
!(iggp:sps:classify sps:next:train:state-50:atom-11 sps:next:train:state-50
  (quote (sps:next-step sps:next:train:state-50 sps:n2)))
!(iggp:sps:classify sps:next:train:state-50:atom-12 sps:next:train:state-50
  (quote (sps:next-step sps:next:train:state-50 sps:n3)))
!(iggp:sps:classify sps:next:train:state-51:atom-1 sps:next:train:state-51
  (quote (sps:next-score sps:next:train:state-51 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-51:atom-2 sps:next:train:state-51
  (quote (sps:next-score sps:next:train:state-51 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-51:atom-3 sps:next:train:state-51
  (quote (sps:next-score sps:next:train:state-51 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-51:atom-4 sps:next:train:state-51
  (quote (sps:next-score sps:next:train:state-51 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-51:atom-5 sps:next:train:state-51
  (quote (sps:next-score sps:next:train:state-51 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-51:atom-6 sps:next:train:state-51
  (quote (sps:next-score sps:next:train:state-51 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-51:atom-7 sps:next:train:state-51
  (quote (sps:next-score sps:next:train:state-51 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-51:atom-8 sps:next:train:state-51
  (quote (sps:next-score sps:next:train:state-51 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-51:atom-9 sps:next:train:state-51
  (quote (sps:next-step sps:next:train:state-51 sps:n0)))
!(iggp:sps:classify sps:next:train:state-51:atom-10 sps:next:train:state-51
  (quote (sps:next-step sps:next:train:state-51 sps:n1)))
!(iggp:sps:classify sps:next:train:state-51:atom-11 sps:next:train:state-51
  (quote (sps:next-step sps:next:train:state-51 sps:n2)))
!(iggp:sps:classify sps:next:train:state-51:atom-12 sps:next:train:state-51
  (quote (sps:next-step sps:next:train:state-51 sps:n3)))
!(iggp:sps:classify sps:next:train:state-52:atom-1 sps:next:train:state-52
  (quote (sps:next-score sps:next:train:state-52 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-52:atom-2 sps:next:train:state-52
  (quote (sps:next-score sps:next:train:state-52 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-52:atom-3 sps:next:train:state-52
  (quote (sps:next-score sps:next:train:state-52 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-52:atom-4 sps:next:train:state-52
  (quote (sps:next-score sps:next:train:state-52 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-52:atom-5 sps:next:train:state-52
  (quote (sps:next-score sps:next:train:state-52 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-52:atom-6 sps:next:train:state-52
  (quote (sps:next-score sps:next:train:state-52 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-52:atom-7 sps:next:train:state-52
  (quote (sps:next-score sps:next:train:state-52 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-52:atom-8 sps:next:train:state-52
  (quote (sps:next-score sps:next:train:state-52 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-52:atom-9 sps:next:train:state-52
  (quote (sps:next-step sps:next:train:state-52 sps:n0)))
!(iggp:sps:classify sps:next:train:state-52:atom-10 sps:next:train:state-52
  (quote (sps:next-step sps:next:train:state-52 sps:n1)))
!(iggp:sps:classify sps:next:train:state-52:atom-11 sps:next:train:state-52
  (quote (sps:next-step sps:next:train:state-52 sps:n2)))
!(iggp:sps:classify sps:next:train:state-52:atom-12 sps:next:train:state-52
  (quote (sps:next-step sps:next:train:state-52 sps:n3)))
!(iggp:sps:classify sps:next:train:state-53:atom-1 sps:next:train:state-53
  (quote (sps:next-score sps:next:train:state-53 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-53:atom-2 sps:next:train:state-53
  (quote (sps:next-score sps:next:train:state-53 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-53:atom-3 sps:next:train:state-53
  (quote (sps:next-score sps:next:train:state-53 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-53:atom-4 sps:next:train:state-53
  (quote (sps:next-score sps:next:train:state-53 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-53:atom-5 sps:next:train:state-53
  (quote (sps:next-score sps:next:train:state-53 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-53:atom-6 sps:next:train:state-53
  (quote (sps:next-score sps:next:train:state-53 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-53:atom-7 sps:next:train:state-53
  (quote (sps:next-score sps:next:train:state-53 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-53:atom-8 sps:next:train:state-53
  (quote (sps:next-score sps:next:train:state-53 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-53:atom-9 sps:next:train:state-53
  (quote (sps:next-step sps:next:train:state-53 sps:n0)))
!(iggp:sps:classify sps:next:train:state-53:atom-10 sps:next:train:state-53
  (quote (sps:next-step sps:next:train:state-53 sps:n1)))
!(iggp:sps:classify sps:next:train:state-53:atom-11 sps:next:train:state-53
  (quote (sps:next-step sps:next:train:state-53 sps:n2)))
!(iggp:sps:classify sps:next:train:state-53:atom-12 sps:next:train:state-53
  (quote (sps:next-step sps:next:train:state-53 sps:n3)))
!(iggp:sps:classify sps:next:train:state-54:atom-1 sps:next:train:state-54
  (quote (sps:next-score sps:next:train:state-54 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-54:atom-2 sps:next:train:state-54
  (quote (sps:next-score sps:next:train:state-54 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-54:atom-3 sps:next:train:state-54
  (quote (sps:next-score sps:next:train:state-54 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-54:atom-4 sps:next:train:state-54
  (quote (sps:next-score sps:next:train:state-54 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-54:atom-5 sps:next:train:state-54
  (quote (sps:next-score sps:next:train:state-54 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-54:atom-6 sps:next:train:state-54
  (quote (sps:next-score sps:next:train:state-54 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-54:atom-7 sps:next:train:state-54
  (quote (sps:next-score sps:next:train:state-54 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-54:atom-8 sps:next:train:state-54
  (quote (sps:next-score sps:next:train:state-54 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-54:atom-9 sps:next:train:state-54
  (quote (sps:next-step sps:next:train:state-54 sps:n0)))
!(iggp:sps:classify sps:next:train:state-54:atom-10 sps:next:train:state-54
  (quote (sps:next-step sps:next:train:state-54 sps:n1)))
!(iggp:sps:classify sps:next:train:state-54:atom-11 sps:next:train:state-54
  (quote (sps:next-step sps:next:train:state-54 sps:n2)))
!(iggp:sps:classify sps:next:train:state-54:atom-12 sps:next:train:state-54
  (quote (sps:next-step sps:next:train:state-54 sps:n3)))
!(iggp:sps:classify sps:next:train:state-55:atom-1 sps:next:train:state-55
  (quote (sps:next-score sps:next:train:state-55 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-55:atom-2 sps:next:train:state-55
  (quote (sps:next-score sps:next:train:state-55 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-55:atom-3 sps:next:train:state-55
  (quote (sps:next-score sps:next:train:state-55 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-55:atom-4 sps:next:train:state-55
  (quote (sps:next-score sps:next:train:state-55 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-55:atom-5 sps:next:train:state-55
  (quote (sps:next-score sps:next:train:state-55 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-55:atom-6 sps:next:train:state-55
  (quote (sps:next-score sps:next:train:state-55 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-55:atom-7 sps:next:train:state-55
  (quote (sps:next-score sps:next:train:state-55 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-55:atom-8 sps:next:train:state-55
  (quote (sps:next-score sps:next:train:state-55 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-55:atom-9 sps:next:train:state-55
  (quote (sps:next-step sps:next:train:state-55 sps:n0)))
!(iggp:sps:classify sps:next:train:state-55:atom-10 sps:next:train:state-55
  (quote (sps:next-step sps:next:train:state-55 sps:n1)))
!(iggp:sps:classify sps:next:train:state-55:atom-11 sps:next:train:state-55
  (quote (sps:next-step sps:next:train:state-55 sps:n2)))
!(iggp:sps:classify sps:next:train:state-55:atom-12 sps:next:train:state-55
  (quote (sps:next-step sps:next:train:state-55 sps:n3)))
!(iggp:sps:classify sps:next:train:state-56:atom-1 sps:next:train:state-56
  (quote (sps:next-score sps:next:train:state-56 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-56:atom-2 sps:next:train:state-56
  (quote (sps:next-score sps:next:train:state-56 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-56:atom-3 sps:next:train:state-56
  (quote (sps:next-score sps:next:train:state-56 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-56:atom-4 sps:next:train:state-56
  (quote (sps:next-score sps:next:train:state-56 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-56:atom-5 sps:next:train:state-56
  (quote (sps:next-score sps:next:train:state-56 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-56:atom-6 sps:next:train:state-56
  (quote (sps:next-score sps:next:train:state-56 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-56:atom-7 sps:next:train:state-56
  (quote (sps:next-score sps:next:train:state-56 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-56:atom-8 sps:next:train:state-56
  (quote (sps:next-score sps:next:train:state-56 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-56:atom-9 sps:next:train:state-56
  (quote (sps:next-step sps:next:train:state-56 sps:n0)))
!(iggp:sps:classify sps:next:train:state-56:atom-10 sps:next:train:state-56
  (quote (sps:next-step sps:next:train:state-56 sps:n1)))
!(iggp:sps:classify sps:next:train:state-56:atom-11 sps:next:train:state-56
  (quote (sps:next-step sps:next:train:state-56 sps:n2)))
!(iggp:sps:classify sps:next:train:state-56:atom-12 sps:next:train:state-56
  (quote (sps:next-step sps:next:train:state-56 sps:n3)))
!(iggp:sps:classify sps:next:train:state-57:atom-1 sps:next:train:state-57
  (quote (sps:next-score sps:next:train:state-57 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-57:atom-2 sps:next:train:state-57
  (quote (sps:next-score sps:next:train:state-57 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-57:atom-3 sps:next:train:state-57
  (quote (sps:next-score sps:next:train:state-57 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-57:atom-4 sps:next:train:state-57
  (quote (sps:next-score sps:next:train:state-57 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-57:atom-5 sps:next:train:state-57
  (quote (sps:next-score sps:next:train:state-57 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-57:atom-6 sps:next:train:state-57
  (quote (sps:next-score sps:next:train:state-57 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-57:atom-7 sps:next:train:state-57
  (quote (sps:next-score sps:next:train:state-57 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-57:atom-8 sps:next:train:state-57
  (quote (sps:next-score sps:next:train:state-57 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-57:atom-9 sps:next:train:state-57
  (quote (sps:next-step sps:next:train:state-57 sps:n0)))
!(iggp:sps:classify sps:next:train:state-57:atom-10 sps:next:train:state-57
  (quote (sps:next-step sps:next:train:state-57 sps:n1)))
!(iggp:sps:classify sps:next:train:state-57:atom-11 sps:next:train:state-57
  (quote (sps:next-step sps:next:train:state-57 sps:n2)))
!(iggp:sps:classify sps:next:train:state-57:atom-12 sps:next:train:state-57
  (quote (sps:next-step sps:next:train:state-57 sps:n3)))
!(iggp:sps:classify sps:next:train:state-58:atom-1 sps:next:train:state-58
  (quote (sps:next-score sps:next:train:state-58 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:train:state-58:atom-2 sps:next:train:state-58
  (quote (sps:next-score sps:next:train:state-58 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:train:state-58:atom-3 sps:next:train:state-58
  (quote (sps:next-score sps:next:train:state-58 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:train:state-58:atom-4 sps:next:train:state-58
  (quote (sps:next-score sps:next:train:state-58 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:train:state-58:atom-5 sps:next:train:state-58
  (quote (sps:next-score sps:next:train:state-58 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:train:state-58:atom-6 sps:next:train:state-58
  (quote (sps:next-score sps:next:train:state-58 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:train:state-58:atom-7 sps:next:train:state-58
  (quote (sps:next-score sps:next:train:state-58 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:train:state-58:atom-8 sps:next:train:state-58
  (quote (sps:next-score sps:next:train:state-58 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:train:state-58:atom-9 sps:next:train:state-58
  (quote (sps:next-step sps:next:train:state-58 sps:n0)))
!(iggp:sps:classify sps:next:train:state-58:atom-10 sps:next:train:state-58
  (quote (sps:next-step sps:next:train:state-58 sps:n1)))
!(iggp:sps:classify sps:next:train:state-58:atom-11 sps:next:train:state-58
  (quote (sps:next-step sps:next:train:state-58 sps:n2)))
!(iggp:sps:classify sps:next:train:state-58:atom-12 sps:next:train:state-58
  (quote (sps:next-step sps:next:train:state-58 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-1:atom-1 sps:next:validate:state-1
  (quote (sps:next-score sps:next:validate:state-1 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-1:atom-2 sps:next:validate:state-1
  (quote (sps:next-score sps:next:validate:state-1 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-1:atom-3 sps:next:validate:state-1
  (quote (sps:next-score sps:next:validate:state-1 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-1:atom-4 sps:next:validate:state-1
  (quote (sps:next-score sps:next:validate:state-1 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-1:atom-5 sps:next:validate:state-1
  (quote (sps:next-score sps:next:validate:state-1 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-1:atom-6 sps:next:validate:state-1
  (quote (sps:next-score sps:next:validate:state-1 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-1:atom-7 sps:next:validate:state-1
  (quote (sps:next-score sps:next:validate:state-1 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-1:atom-8 sps:next:validate:state-1
  (quote (sps:next-score sps:next:validate:state-1 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-1:atom-9 sps:next:validate:state-1
  (quote (sps:next-step sps:next:validate:state-1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-1:atom-10 sps:next:validate:state-1
  (quote (sps:next-step sps:next:validate:state-1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-1:atom-11 sps:next:validate:state-1
  (quote (sps:next-step sps:next:validate:state-1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-1:atom-12 sps:next:validate:state-1
  (quote (sps:next-step sps:next:validate:state-1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-2:atom-1 sps:next:validate:state-2
  (quote (sps:next-score sps:next:validate:state-2 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-2:atom-2 sps:next:validate:state-2
  (quote (sps:next-score sps:next:validate:state-2 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-2:atom-3 sps:next:validate:state-2
  (quote (sps:next-score sps:next:validate:state-2 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-2:atom-4 sps:next:validate:state-2
  (quote (sps:next-score sps:next:validate:state-2 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-2:atom-5 sps:next:validate:state-2
  (quote (sps:next-score sps:next:validate:state-2 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-2:atom-6 sps:next:validate:state-2
  (quote (sps:next-score sps:next:validate:state-2 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-2:atom-7 sps:next:validate:state-2
  (quote (sps:next-score sps:next:validate:state-2 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-2:atom-8 sps:next:validate:state-2
  (quote (sps:next-score sps:next:validate:state-2 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-2:atom-9 sps:next:validate:state-2
  (quote (sps:next-step sps:next:validate:state-2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-2:atom-10 sps:next:validate:state-2
  (quote (sps:next-step sps:next:validate:state-2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-2:atom-11 sps:next:validate:state-2
  (quote (sps:next-step sps:next:validate:state-2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-2:atom-12 sps:next:validate:state-2
  (quote (sps:next-step sps:next:validate:state-2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-3:atom-1 sps:next:validate:state-3
  (quote (sps:next-score sps:next:validate:state-3 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-3:atom-2 sps:next:validate:state-3
  (quote (sps:next-score sps:next:validate:state-3 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-3:atom-3 sps:next:validate:state-3
  (quote (sps:next-score sps:next:validate:state-3 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-3:atom-4 sps:next:validate:state-3
  (quote (sps:next-score sps:next:validate:state-3 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-3:atom-5 sps:next:validate:state-3
  (quote (sps:next-score sps:next:validate:state-3 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-3:atom-6 sps:next:validate:state-3
  (quote (sps:next-score sps:next:validate:state-3 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-3:atom-7 sps:next:validate:state-3
  (quote (sps:next-score sps:next:validate:state-3 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-3:atom-8 sps:next:validate:state-3
  (quote (sps:next-score sps:next:validate:state-3 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-3:atom-9 sps:next:validate:state-3
  (quote (sps:next-step sps:next:validate:state-3 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-3:atom-10 sps:next:validate:state-3
  (quote (sps:next-step sps:next:validate:state-3 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-3:atom-11 sps:next:validate:state-3
  (quote (sps:next-step sps:next:validate:state-3 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-3:atom-12 sps:next:validate:state-3
  (quote (sps:next-step sps:next:validate:state-3 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-4:atom-1 sps:next:validate:state-4
  (quote (sps:next-score sps:next:validate:state-4 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-4:atom-2 sps:next:validate:state-4
  (quote (sps:next-score sps:next:validate:state-4 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-4:atom-3 sps:next:validate:state-4
  (quote (sps:next-score sps:next:validate:state-4 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-4:atom-4 sps:next:validate:state-4
  (quote (sps:next-score sps:next:validate:state-4 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-4:atom-5 sps:next:validate:state-4
  (quote (sps:next-score sps:next:validate:state-4 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-4:atom-6 sps:next:validate:state-4
  (quote (sps:next-score sps:next:validate:state-4 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-4:atom-7 sps:next:validate:state-4
  (quote (sps:next-score sps:next:validate:state-4 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-4:atom-8 sps:next:validate:state-4
  (quote (sps:next-score sps:next:validate:state-4 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-4:atom-9 sps:next:validate:state-4
  (quote (sps:next-step sps:next:validate:state-4 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-4:atom-10 sps:next:validate:state-4
  (quote (sps:next-step sps:next:validate:state-4 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-4:atom-11 sps:next:validate:state-4
  (quote (sps:next-step sps:next:validate:state-4 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-4:atom-12 sps:next:validate:state-4
  (quote (sps:next-step sps:next:validate:state-4 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-5:atom-1 sps:next:validate:state-5
  (quote (sps:next-score sps:next:validate:state-5 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-5:atom-2 sps:next:validate:state-5
  (quote (sps:next-score sps:next:validate:state-5 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-5:atom-3 sps:next:validate:state-5
  (quote (sps:next-score sps:next:validate:state-5 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-5:atom-4 sps:next:validate:state-5
  (quote (sps:next-score sps:next:validate:state-5 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-5:atom-5 sps:next:validate:state-5
  (quote (sps:next-score sps:next:validate:state-5 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-5:atom-6 sps:next:validate:state-5
  (quote (sps:next-score sps:next:validate:state-5 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-5:atom-7 sps:next:validate:state-5
  (quote (sps:next-score sps:next:validate:state-5 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-5:atom-8 sps:next:validate:state-5
  (quote (sps:next-score sps:next:validate:state-5 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-5:atom-9 sps:next:validate:state-5
  (quote (sps:next-step sps:next:validate:state-5 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-5:atom-10 sps:next:validate:state-5
  (quote (sps:next-step sps:next:validate:state-5 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-5:atom-11 sps:next:validate:state-5
  (quote (sps:next-step sps:next:validate:state-5 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-5:atom-12 sps:next:validate:state-5
  (quote (sps:next-step sps:next:validate:state-5 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-6:atom-1 sps:next:validate:state-6
  (quote (sps:next-score sps:next:validate:state-6 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-6:atom-2 sps:next:validate:state-6
  (quote (sps:next-score sps:next:validate:state-6 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-6:atom-3 sps:next:validate:state-6
  (quote (sps:next-score sps:next:validate:state-6 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-6:atom-4 sps:next:validate:state-6
  (quote (sps:next-score sps:next:validate:state-6 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-6:atom-5 sps:next:validate:state-6
  (quote (sps:next-score sps:next:validate:state-6 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-6:atom-6 sps:next:validate:state-6
  (quote (sps:next-score sps:next:validate:state-6 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-6:atom-7 sps:next:validate:state-6
  (quote (sps:next-score sps:next:validate:state-6 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-6:atom-8 sps:next:validate:state-6
  (quote (sps:next-score sps:next:validate:state-6 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-6:atom-9 sps:next:validate:state-6
  (quote (sps:next-step sps:next:validate:state-6 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-6:atom-10 sps:next:validate:state-6
  (quote (sps:next-step sps:next:validate:state-6 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-6:atom-11 sps:next:validate:state-6
  (quote (sps:next-step sps:next:validate:state-6 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-6:atom-12 sps:next:validate:state-6
  (quote (sps:next-step sps:next:validate:state-6 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-7:atom-1 sps:next:validate:state-7
  (quote (sps:next-score sps:next:validate:state-7 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-7:atom-2 sps:next:validate:state-7
  (quote (sps:next-score sps:next:validate:state-7 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-7:atom-3 sps:next:validate:state-7
  (quote (sps:next-score sps:next:validate:state-7 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-7:atom-4 sps:next:validate:state-7
  (quote (sps:next-score sps:next:validate:state-7 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-7:atom-5 sps:next:validate:state-7
  (quote (sps:next-score sps:next:validate:state-7 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-7:atom-6 sps:next:validate:state-7
  (quote (sps:next-score sps:next:validate:state-7 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-7:atom-7 sps:next:validate:state-7
  (quote (sps:next-score sps:next:validate:state-7 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-7:atom-8 sps:next:validate:state-7
  (quote (sps:next-score sps:next:validate:state-7 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-7:atom-9 sps:next:validate:state-7
  (quote (sps:next-step sps:next:validate:state-7 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-7:atom-10 sps:next:validate:state-7
  (quote (sps:next-step sps:next:validate:state-7 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-7:atom-11 sps:next:validate:state-7
  (quote (sps:next-step sps:next:validate:state-7 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-7:atom-12 sps:next:validate:state-7
  (quote (sps:next-step sps:next:validate:state-7 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-8:atom-1 sps:next:validate:state-8
  (quote (sps:next-score sps:next:validate:state-8 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-8:atom-2 sps:next:validate:state-8
  (quote (sps:next-score sps:next:validate:state-8 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-8:atom-3 sps:next:validate:state-8
  (quote (sps:next-score sps:next:validate:state-8 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-8:atom-4 sps:next:validate:state-8
  (quote (sps:next-score sps:next:validate:state-8 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-8:atom-5 sps:next:validate:state-8
  (quote (sps:next-score sps:next:validate:state-8 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-8:atom-6 sps:next:validate:state-8
  (quote (sps:next-score sps:next:validate:state-8 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-8:atom-7 sps:next:validate:state-8
  (quote (sps:next-score sps:next:validate:state-8 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-8:atom-8 sps:next:validate:state-8
  (quote (sps:next-score sps:next:validate:state-8 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-8:atom-9 sps:next:validate:state-8
  (quote (sps:next-step sps:next:validate:state-8 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-8:atom-10 sps:next:validate:state-8
  (quote (sps:next-step sps:next:validate:state-8 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-8:atom-11 sps:next:validate:state-8
  (quote (sps:next-step sps:next:validate:state-8 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-8:atom-12 sps:next:validate:state-8
  (quote (sps:next-step sps:next:validate:state-8 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-9:atom-1 sps:next:validate:state-9
  (quote (sps:next-score sps:next:validate:state-9 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-9:atom-2 sps:next:validate:state-9
  (quote (sps:next-score sps:next:validate:state-9 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-9:atom-3 sps:next:validate:state-9
  (quote (sps:next-score sps:next:validate:state-9 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-9:atom-4 sps:next:validate:state-9
  (quote (sps:next-score sps:next:validate:state-9 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-9:atom-5 sps:next:validate:state-9
  (quote (sps:next-score sps:next:validate:state-9 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-9:atom-6 sps:next:validate:state-9
  (quote (sps:next-score sps:next:validate:state-9 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-9:atom-7 sps:next:validate:state-9
  (quote (sps:next-score sps:next:validate:state-9 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-9:atom-8 sps:next:validate:state-9
  (quote (sps:next-score sps:next:validate:state-9 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-9:atom-9 sps:next:validate:state-9
  (quote (sps:next-step sps:next:validate:state-9 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-9:atom-10 sps:next:validate:state-9
  (quote (sps:next-step sps:next:validate:state-9 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-9:atom-11 sps:next:validate:state-9
  (quote (sps:next-step sps:next:validate:state-9 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-9:atom-12 sps:next:validate:state-9
  (quote (sps:next-step sps:next:validate:state-9 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-10:atom-1 sps:next:validate:state-10
  (quote (sps:next-score sps:next:validate:state-10 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-10:atom-2 sps:next:validate:state-10
  (quote (sps:next-score sps:next:validate:state-10 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-10:atom-3 sps:next:validate:state-10
  (quote (sps:next-score sps:next:validate:state-10 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-10:atom-4 sps:next:validate:state-10
  (quote (sps:next-score sps:next:validate:state-10 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-10:atom-5 sps:next:validate:state-10
  (quote (sps:next-score sps:next:validate:state-10 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-10:atom-6 sps:next:validate:state-10
  (quote (sps:next-score sps:next:validate:state-10 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-10:atom-7 sps:next:validate:state-10
  (quote (sps:next-score sps:next:validate:state-10 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-10:atom-8 sps:next:validate:state-10
  (quote (sps:next-score sps:next:validate:state-10 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-10:atom-9 sps:next:validate:state-10
  (quote (sps:next-step sps:next:validate:state-10 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-10:atom-10 sps:next:validate:state-10
  (quote (sps:next-step sps:next:validate:state-10 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-10:atom-11 sps:next:validate:state-10
  (quote (sps:next-step sps:next:validate:state-10 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-10:atom-12 sps:next:validate:state-10
  (quote (sps:next-step sps:next:validate:state-10 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-11:atom-1 sps:next:validate:state-11
  (quote (sps:next-score sps:next:validate:state-11 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-11:atom-2 sps:next:validate:state-11
  (quote (sps:next-score sps:next:validate:state-11 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-11:atom-3 sps:next:validate:state-11
  (quote (sps:next-score sps:next:validate:state-11 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-11:atom-4 sps:next:validate:state-11
  (quote (sps:next-score sps:next:validate:state-11 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-11:atom-5 sps:next:validate:state-11
  (quote (sps:next-score sps:next:validate:state-11 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-11:atom-6 sps:next:validate:state-11
  (quote (sps:next-score sps:next:validate:state-11 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-11:atom-7 sps:next:validate:state-11
  (quote (sps:next-score sps:next:validate:state-11 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-11:atom-8 sps:next:validate:state-11
  (quote (sps:next-score sps:next:validate:state-11 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-11:atom-9 sps:next:validate:state-11
  (quote (sps:next-step sps:next:validate:state-11 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-11:atom-10 sps:next:validate:state-11
  (quote (sps:next-step sps:next:validate:state-11 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-11:atom-11 sps:next:validate:state-11
  (quote (sps:next-step sps:next:validate:state-11 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-11:atom-12 sps:next:validate:state-11
  (quote (sps:next-step sps:next:validate:state-11 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-12:atom-1 sps:next:validate:state-12
  (quote (sps:next-score sps:next:validate:state-12 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-12:atom-2 sps:next:validate:state-12
  (quote (sps:next-score sps:next:validate:state-12 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-12:atom-3 sps:next:validate:state-12
  (quote (sps:next-score sps:next:validate:state-12 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-12:atom-4 sps:next:validate:state-12
  (quote (sps:next-score sps:next:validate:state-12 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-12:atom-5 sps:next:validate:state-12
  (quote (sps:next-score sps:next:validate:state-12 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-12:atom-6 sps:next:validate:state-12
  (quote (sps:next-score sps:next:validate:state-12 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-12:atom-7 sps:next:validate:state-12
  (quote (sps:next-score sps:next:validate:state-12 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-12:atom-8 sps:next:validate:state-12
  (quote (sps:next-score sps:next:validate:state-12 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-12:atom-9 sps:next:validate:state-12
  (quote (sps:next-step sps:next:validate:state-12 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-12:atom-10 sps:next:validate:state-12
  (quote (sps:next-step sps:next:validate:state-12 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-12:atom-11 sps:next:validate:state-12
  (quote (sps:next-step sps:next:validate:state-12 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-12:atom-12 sps:next:validate:state-12
  (quote (sps:next-step sps:next:validate:state-12 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-13:atom-1 sps:next:validate:state-13
  (quote (sps:next-score sps:next:validate:state-13 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-13:atom-2 sps:next:validate:state-13
  (quote (sps:next-score sps:next:validate:state-13 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-13:atom-3 sps:next:validate:state-13
  (quote (sps:next-score sps:next:validate:state-13 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-13:atom-4 sps:next:validate:state-13
  (quote (sps:next-score sps:next:validate:state-13 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-13:atom-5 sps:next:validate:state-13
  (quote (sps:next-score sps:next:validate:state-13 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-13:atom-6 sps:next:validate:state-13
  (quote (sps:next-score sps:next:validate:state-13 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-13:atom-7 sps:next:validate:state-13
  (quote (sps:next-score sps:next:validate:state-13 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-13:atom-8 sps:next:validate:state-13
  (quote (sps:next-score sps:next:validate:state-13 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-13:atom-9 sps:next:validate:state-13
  (quote (sps:next-step sps:next:validate:state-13 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-13:atom-10 sps:next:validate:state-13
  (quote (sps:next-step sps:next:validate:state-13 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-13:atom-11 sps:next:validate:state-13
  (quote (sps:next-step sps:next:validate:state-13 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-13:atom-12 sps:next:validate:state-13
  (quote (sps:next-step sps:next:validate:state-13 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-14:atom-1 sps:next:validate:state-14
  (quote (sps:next-score sps:next:validate:state-14 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-14:atom-2 sps:next:validate:state-14
  (quote (sps:next-score sps:next:validate:state-14 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-14:atom-3 sps:next:validate:state-14
  (quote (sps:next-score sps:next:validate:state-14 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-14:atom-4 sps:next:validate:state-14
  (quote (sps:next-score sps:next:validate:state-14 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-14:atom-5 sps:next:validate:state-14
  (quote (sps:next-score sps:next:validate:state-14 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-14:atom-6 sps:next:validate:state-14
  (quote (sps:next-score sps:next:validate:state-14 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-14:atom-7 sps:next:validate:state-14
  (quote (sps:next-score sps:next:validate:state-14 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-14:atom-8 sps:next:validate:state-14
  (quote (sps:next-score sps:next:validate:state-14 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:validate:state-14:atom-9 sps:next:validate:state-14
  (quote (sps:next-step sps:next:validate:state-14 sps:n0)))
!(iggp:sps:classify sps:next:validate:state-14:atom-10 sps:next:validate:state-14
  (quote (sps:next-step sps:next:validate:state-14 sps:n1)))
!(iggp:sps:classify sps:next:validate:state-14:atom-11 sps:next:validate:state-14
  (quote (sps:next-step sps:next:validate:state-14 sps:n2)))
!(iggp:sps:classify sps:next:validate:state-14:atom-12 sps:next:validate:state-14
  (quote (sps:next-step sps:next:validate:state-14 sps:n3)))
!(iggp:sps:classify sps:next:test:state-1:atom-1 sps:next:test:state-1
  (quote (sps:next-score sps:next:test:state-1 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-1:atom-2 sps:next:test:state-1
  (quote (sps:next-score sps:next:test:state-1 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-1:atom-3 sps:next:test:state-1
  (quote (sps:next-score sps:next:test:state-1 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-1:atom-4 sps:next:test:state-1
  (quote (sps:next-score sps:next:test:state-1 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-1:atom-5 sps:next:test:state-1
  (quote (sps:next-score sps:next:test:state-1 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-1:atom-6 sps:next:test:state-1
  (quote (sps:next-score sps:next:test:state-1 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-1:atom-7 sps:next:test:state-1
  (quote (sps:next-score sps:next:test:state-1 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-1:atom-8 sps:next:test:state-1
  (quote (sps:next-score sps:next:test:state-1 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-1:atom-9 sps:next:test:state-1
  (quote (sps:next-step sps:next:test:state-1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-1:atom-10 sps:next:test:state-1
  (quote (sps:next-step sps:next:test:state-1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-1:atom-11 sps:next:test:state-1
  (quote (sps:next-step sps:next:test:state-1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-1:atom-12 sps:next:test:state-1
  (quote (sps:next-step sps:next:test:state-1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-2:atom-1 sps:next:test:state-2
  (quote (sps:next-score sps:next:test:state-2 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-2:atom-2 sps:next:test:state-2
  (quote (sps:next-score sps:next:test:state-2 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-2:atom-3 sps:next:test:state-2
  (quote (sps:next-score sps:next:test:state-2 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-2:atom-4 sps:next:test:state-2
  (quote (sps:next-score sps:next:test:state-2 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-2:atom-5 sps:next:test:state-2
  (quote (sps:next-score sps:next:test:state-2 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-2:atom-6 sps:next:test:state-2
  (quote (sps:next-score sps:next:test:state-2 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-2:atom-7 sps:next:test:state-2
  (quote (sps:next-score sps:next:test:state-2 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-2:atom-8 sps:next:test:state-2
  (quote (sps:next-score sps:next:test:state-2 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-2:atom-9 sps:next:test:state-2
  (quote (sps:next-step sps:next:test:state-2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-2:atom-10 sps:next:test:state-2
  (quote (sps:next-step sps:next:test:state-2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-2:atom-11 sps:next:test:state-2
  (quote (sps:next-step sps:next:test:state-2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-2:atom-12 sps:next:test:state-2
  (quote (sps:next-step sps:next:test:state-2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-3:atom-1 sps:next:test:state-3
  (quote (sps:next-score sps:next:test:state-3 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-3:atom-2 sps:next:test:state-3
  (quote (sps:next-score sps:next:test:state-3 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-3:atom-3 sps:next:test:state-3
  (quote (sps:next-score sps:next:test:state-3 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-3:atom-4 sps:next:test:state-3
  (quote (sps:next-score sps:next:test:state-3 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-3:atom-5 sps:next:test:state-3
  (quote (sps:next-score sps:next:test:state-3 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-3:atom-6 sps:next:test:state-3
  (quote (sps:next-score sps:next:test:state-3 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-3:atom-7 sps:next:test:state-3
  (quote (sps:next-score sps:next:test:state-3 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-3:atom-8 sps:next:test:state-3
  (quote (sps:next-score sps:next:test:state-3 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-3:atom-9 sps:next:test:state-3
  (quote (sps:next-step sps:next:test:state-3 sps:n0)))
!(iggp:sps:classify sps:next:test:state-3:atom-10 sps:next:test:state-3
  (quote (sps:next-step sps:next:test:state-3 sps:n1)))
!(iggp:sps:classify sps:next:test:state-3:atom-11 sps:next:test:state-3
  (quote (sps:next-step sps:next:test:state-3 sps:n2)))
!(iggp:sps:classify sps:next:test:state-3:atom-12 sps:next:test:state-3
  (quote (sps:next-step sps:next:test:state-3 sps:n3)))
!(iggp:sps:classify sps:next:test:state-4:atom-1 sps:next:test:state-4
  (quote (sps:next-score sps:next:test:state-4 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-4:atom-2 sps:next:test:state-4
  (quote (sps:next-score sps:next:test:state-4 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-4:atom-3 sps:next:test:state-4
  (quote (sps:next-score sps:next:test:state-4 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-4:atom-4 sps:next:test:state-4
  (quote (sps:next-score sps:next:test:state-4 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-4:atom-5 sps:next:test:state-4
  (quote (sps:next-score sps:next:test:state-4 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-4:atom-6 sps:next:test:state-4
  (quote (sps:next-score sps:next:test:state-4 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-4:atom-7 sps:next:test:state-4
  (quote (sps:next-score sps:next:test:state-4 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-4:atom-8 sps:next:test:state-4
  (quote (sps:next-score sps:next:test:state-4 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-4:atom-9 sps:next:test:state-4
  (quote (sps:next-step sps:next:test:state-4 sps:n0)))
!(iggp:sps:classify sps:next:test:state-4:atom-10 sps:next:test:state-4
  (quote (sps:next-step sps:next:test:state-4 sps:n1)))
!(iggp:sps:classify sps:next:test:state-4:atom-11 sps:next:test:state-4
  (quote (sps:next-step sps:next:test:state-4 sps:n2)))
!(iggp:sps:classify sps:next:test:state-4:atom-12 sps:next:test:state-4
  (quote (sps:next-step sps:next:test:state-4 sps:n3)))
!(iggp:sps:classify sps:next:test:state-5:atom-1 sps:next:test:state-5
  (quote (sps:next-score sps:next:test:state-5 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-5:atom-2 sps:next:test:state-5
  (quote (sps:next-score sps:next:test:state-5 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-5:atom-3 sps:next:test:state-5
  (quote (sps:next-score sps:next:test:state-5 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-5:atom-4 sps:next:test:state-5
  (quote (sps:next-score sps:next:test:state-5 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-5:atom-5 sps:next:test:state-5
  (quote (sps:next-score sps:next:test:state-5 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-5:atom-6 sps:next:test:state-5
  (quote (sps:next-score sps:next:test:state-5 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-5:atom-7 sps:next:test:state-5
  (quote (sps:next-score sps:next:test:state-5 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-5:atom-8 sps:next:test:state-5
  (quote (sps:next-score sps:next:test:state-5 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-5:atom-9 sps:next:test:state-5
  (quote (sps:next-step sps:next:test:state-5 sps:n0)))
!(iggp:sps:classify sps:next:test:state-5:atom-10 sps:next:test:state-5
  (quote (sps:next-step sps:next:test:state-5 sps:n1)))
!(iggp:sps:classify sps:next:test:state-5:atom-11 sps:next:test:state-5
  (quote (sps:next-step sps:next:test:state-5 sps:n2)))
!(iggp:sps:classify sps:next:test:state-5:atom-12 sps:next:test:state-5
  (quote (sps:next-step sps:next:test:state-5 sps:n3)))
!(iggp:sps:classify sps:next:test:state-6:atom-1 sps:next:test:state-6
  (quote (sps:next-score sps:next:test:state-6 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-6:atom-2 sps:next:test:state-6
  (quote (sps:next-score sps:next:test:state-6 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-6:atom-3 sps:next:test:state-6
  (quote (sps:next-score sps:next:test:state-6 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-6:atom-4 sps:next:test:state-6
  (quote (sps:next-score sps:next:test:state-6 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-6:atom-5 sps:next:test:state-6
  (quote (sps:next-score sps:next:test:state-6 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-6:atom-6 sps:next:test:state-6
  (quote (sps:next-score sps:next:test:state-6 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-6:atom-7 sps:next:test:state-6
  (quote (sps:next-score sps:next:test:state-6 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-6:atom-8 sps:next:test:state-6
  (quote (sps:next-score sps:next:test:state-6 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-6:atom-9 sps:next:test:state-6
  (quote (sps:next-step sps:next:test:state-6 sps:n0)))
!(iggp:sps:classify sps:next:test:state-6:atom-10 sps:next:test:state-6
  (quote (sps:next-step sps:next:test:state-6 sps:n1)))
!(iggp:sps:classify sps:next:test:state-6:atom-11 sps:next:test:state-6
  (quote (sps:next-step sps:next:test:state-6 sps:n2)))
!(iggp:sps:classify sps:next:test:state-6:atom-12 sps:next:test:state-6
  (quote (sps:next-step sps:next:test:state-6 sps:n3)))
!(iggp:sps:classify sps:next:test:state-7:atom-1 sps:next:test:state-7
  (quote (sps:next-score sps:next:test:state-7 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-7:atom-2 sps:next:test:state-7
  (quote (sps:next-score sps:next:test:state-7 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-7:atom-3 sps:next:test:state-7
  (quote (sps:next-score sps:next:test:state-7 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-7:atom-4 sps:next:test:state-7
  (quote (sps:next-score sps:next:test:state-7 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-7:atom-5 sps:next:test:state-7
  (quote (sps:next-score sps:next:test:state-7 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-7:atom-6 sps:next:test:state-7
  (quote (sps:next-score sps:next:test:state-7 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-7:atom-7 sps:next:test:state-7
  (quote (sps:next-score sps:next:test:state-7 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-7:atom-8 sps:next:test:state-7
  (quote (sps:next-score sps:next:test:state-7 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-7:atom-9 sps:next:test:state-7
  (quote (sps:next-step sps:next:test:state-7 sps:n0)))
!(iggp:sps:classify sps:next:test:state-7:atom-10 sps:next:test:state-7
  (quote (sps:next-step sps:next:test:state-7 sps:n1)))
!(iggp:sps:classify sps:next:test:state-7:atom-11 sps:next:test:state-7
  (quote (sps:next-step sps:next:test:state-7 sps:n2)))
!(iggp:sps:classify sps:next:test:state-7:atom-12 sps:next:test:state-7
  (quote (sps:next-step sps:next:test:state-7 sps:n3)))
!(iggp:sps:classify sps:next:test:state-8:atom-1 sps:next:test:state-8
  (quote (sps:next-score sps:next:test:state-8 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-8:atom-2 sps:next:test:state-8
  (quote (sps:next-score sps:next:test:state-8 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-8:atom-3 sps:next:test:state-8
  (quote (sps:next-score sps:next:test:state-8 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-8:atom-4 sps:next:test:state-8
  (quote (sps:next-score sps:next:test:state-8 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-8:atom-5 sps:next:test:state-8
  (quote (sps:next-score sps:next:test:state-8 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-8:atom-6 sps:next:test:state-8
  (quote (sps:next-score sps:next:test:state-8 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-8:atom-7 sps:next:test:state-8
  (quote (sps:next-score sps:next:test:state-8 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-8:atom-8 sps:next:test:state-8
  (quote (sps:next-score sps:next:test:state-8 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-8:atom-9 sps:next:test:state-8
  (quote (sps:next-step sps:next:test:state-8 sps:n0)))
!(iggp:sps:classify sps:next:test:state-8:atom-10 sps:next:test:state-8
  (quote (sps:next-step sps:next:test:state-8 sps:n1)))
!(iggp:sps:classify sps:next:test:state-8:atom-11 sps:next:test:state-8
  (quote (sps:next-step sps:next:test:state-8 sps:n2)))
!(iggp:sps:classify sps:next:test:state-8:atom-12 sps:next:test:state-8
  (quote (sps:next-step sps:next:test:state-8 sps:n3)))
!(iggp:sps:classify sps:next:test:state-9:atom-1 sps:next:test:state-9
  (quote (sps:next-score sps:next:test:state-9 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-9:atom-2 sps:next:test:state-9
  (quote (sps:next-score sps:next:test:state-9 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-9:atom-3 sps:next:test:state-9
  (quote (sps:next-score sps:next:test:state-9 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-9:atom-4 sps:next:test:state-9
  (quote (sps:next-score sps:next:test:state-9 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-9:atom-5 sps:next:test:state-9
  (quote (sps:next-score sps:next:test:state-9 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-9:atom-6 sps:next:test:state-9
  (quote (sps:next-score sps:next:test:state-9 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-9:atom-7 sps:next:test:state-9
  (quote (sps:next-score sps:next:test:state-9 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-9:atom-8 sps:next:test:state-9
  (quote (sps:next-score sps:next:test:state-9 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-9:atom-9 sps:next:test:state-9
  (quote (sps:next-step sps:next:test:state-9 sps:n0)))
!(iggp:sps:classify sps:next:test:state-9:atom-10 sps:next:test:state-9
  (quote (sps:next-step sps:next:test:state-9 sps:n1)))
!(iggp:sps:classify sps:next:test:state-9:atom-11 sps:next:test:state-9
  (quote (sps:next-step sps:next:test:state-9 sps:n2)))
!(iggp:sps:classify sps:next:test:state-9:atom-12 sps:next:test:state-9
  (quote (sps:next-step sps:next:test:state-9 sps:n3)))
!(iggp:sps:classify sps:next:test:state-10:atom-1 sps:next:test:state-10
  (quote (sps:next-score sps:next:test:state-10 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-10:atom-2 sps:next:test:state-10
  (quote (sps:next-score sps:next:test:state-10 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-10:atom-3 sps:next:test:state-10
  (quote (sps:next-score sps:next:test:state-10 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-10:atom-4 sps:next:test:state-10
  (quote (sps:next-score sps:next:test:state-10 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-10:atom-5 sps:next:test:state-10
  (quote (sps:next-score sps:next:test:state-10 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-10:atom-6 sps:next:test:state-10
  (quote (sps:next-score sps:next:test:state-10 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-10:atom-7 sps:next:test:state-10
  (quote (sps:next-score sps:next:test:state-10 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-10:atom-8 sps:next:test:state-10
  (quote (sps:next-score sps:next:test:state-10 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-10:atom-9 sps:next:test:state-10
  (quote (sps:next-step sps:next:test:state-10 sps:n0)))
!(iggp:sps:classify sps:next:test:state-10:atom-10 sps:next:test:state-10
  (quote (sps:next-step sps:next:test:state-10 sps:n1)))
!(iggp:sps:classify sps:next:test:state-10:atom-11 sps:next:test:state-10
  (quote (sps:next-step sps:next:test:state-10 sps:n2)))
!(iggp:sps:classify sps:next:test:state-10:atom-12 sps:next:test:state-10
  (quote (sps:next-step sps:next:test:state-10 sps:n3)))
!(iggp:sps:classify sps:next:test:state-11:atom-1 sps:next:test:state-11
  (quote (sps:next-score sps:next:test:state-11 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-11:atom-2 sps:next:test:state-11
  (quote (sps:next-score sps:next:test:state-11 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-11:atom-3 sps:next:test:state-11
  (quote (sps:next-score sps:next:test:state-11 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-11:atom-4 sps:next:test:state-11
  (quote (sps:next-score sps:next:test:state-11 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-11:atom-5 sps:next:test:state-11
  (quote (sps:next-score sps:next:test:state-11 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-11:atom-6 sps:next:test:state-11
  (quote (sps:next-score sps:next:test:state-11 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-11:atom-7 sps:next:test:state-11
  (quote (sps:next-score sps:next:test:state-11 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-11:atom-8 sps:next:test:state-11
  (quote (sps:next-score sps:next:test:state-11 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-11:atom-9 sps:next:test:state-11
  (quote (sps:next-step sps:next:test:state-11 sps:n0)))
!(iggp:sps:classify sps:next:test:state-11:atom-10 sps:next:test:state-11
  (quote (sps:next-step sps:next:test:state-11 sps:n1)))
!(iggp:sps:classify sps:next:test:state-11:atom-11 sps:next:test:state-11
  (quote (sps:next-step sps:next:test:state-11 sps:n2)))
!(iggp:sps:classify sps:next:test:state-11:atom-12 sps:next:test:state-11
  (quote (sps:next-step sps:next:test:state-11 sps:n3)))
!(iggp:sps:classify sps:next:test:state-12:atom-1 sps:next:test:state-12
  (quote (sps:next-score sps:next:test:state-12 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-12:atom-2 sps:next:test:state-12
  (quote (sps:next-score sps:next:test:state-12 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-12:atom-3 sps:next:test:state-12
  (quote (sps:next-score sps:next:test:state-12 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-12:atom-4 sps:next:test:state-12
  (quote (sps:next-score sps:next:test:state-12 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-12:atom-5 sps:next:test:state-12
  (quote (sps:next-score sps:next:test:state-12 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-12:atom-6 sps:next:test:state-12
  (quote (sps:next-score sps:next:test:state-12 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-12:atom-7 sps:next:test:state-12
  (quote (sps:next-score sps:next:test:state-12 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-12:atom-8 sps:next:test:state-12
  (quote (sps:next-score sps:next:test:state-12 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-12:atom-9 sps:next:test:state-12
  (quote (sps:next-step sps:next:test:state-12 sps:n0)))
!(iggp:sps:classify sps:next:test:state-12:atom-10 sps:next:test:state-12
  (quote (sps:next-step sps:next:test:state-12 sps:n1)))
!(iggp:sps:classify sps:next:test:state-12:atom-11 sps:next:test:state-12
  (quote (sps:next-step sps:next:test:state-12 sps:n2)))
!(iggp:sps:classify sps:next:test:state-12:atom-12 sps:next:test:state-12
  (quote (sps:next-step sps:next:test:state-12 sps:n3)))
!(iggp:sps:classify sps:next:test:state-13:atom-1 sps:next:test:state-13
  (quote (sps:next-score sps:next:test:state-13 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-13:atom-2 sps:next:test:state-13
  (quote (sps:next-score sps:next:test:state-13 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-13:atom-3 sps:next:test:state-13
  (quote (sps:next-score sps:next:test:state-13 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-13:atom-4 sps:next:test:state-13
  (quote (sps:next-score sps:next:test:state-13 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-13:atom-5 sps:next:test:state-13
  (quote (sps:next-score sps:next:test:state-13 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-13:atom-6 sps:next:test:state-13
  (quote (sps:next-score sps:next:test:state-13 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-13:atom-7 sps:next:test:state-13
  (quote (sps:next-score sps:next:test:state-13 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-13:atom-8 sps:next:test:state-13
  (quote (sps:next-score sps:next:test:state-13 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-13:atom-9 sps:next:test:state-13
  (quote (sps:next-step sps:next:test:state-13 sps:n0)))
!(iggp:sps:classify sps:next:test:state-13:atom-10 sps:next:test:state-13
  (quote (sps:next-step sps:next:test:state-13 sps:n1)))
!(iggp:sps:classify sps:next:test:state-13:atom-11 sps:next:test:state-13
  (quote (sps:next-step sps:next:test:state-13 sps:n2)))
!(iggp:sps:classify sps:next:test:state-13:atom-12 sps:next:test:state-13
  (quote (sps:next-step sps:next:test:state-13 sps:n3)))
!(iggp:sps:classify sps:next:test:state-14:atom-1 sps:next:test:state-14
  (quote (sps:next-score sps:next:test:state-14 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-14:atom-2 sps:next:test:state-14
  (quote (sps:next-score sps:next:test:state-14 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-14:atom-3 sps:next:test:state-14
  (quote (sps:next-score sps:next:test:state-14 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-14:atom-4 sps:next:test:state-14
  (quote (sps:next-score sps:next:test:state-14 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-14:atom-5 sps:next:test:state-14
  (quote (sps:next-score sps:next:test:state-14 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-14:atom-6 sps:next:test:state-14
  (quote (sps:next-score sps:next:test:state-14 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-14:atom-7 sps:next:test:state-14
  (quote (sps:next-score sps:next:test:state-14 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-14:atom-8 sps:next:test:state-14
  (quote (sps:next-score sps:next:test:state-14 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-14:atom-9 sps:next:test:state-14
  (quote (sps:next-step sps:next:test:state-14 sps:n0)))
!(iggp:sps:classify sps:next:test:state-14:atom-10 sps:next:test:state-14
  (quote (sps:next-step sps:next:test:state-14 sps:n1)))
!(iggp:sps:classify sps:next:test:state-14:atom-11 sps:next:test:state-14
  (quote (sps:next-step sps:next:test:state-14 sps:n2)))
!(iggp:sps:classify sps:next:test:state-14:atom-12 sps:next:test:state-14
  (quote (sps:next-step sps:next:test:state-14 sps:n3)))
!(iggp:sps:classify sps:next:test:state-15:atom-1 sps:next:test:state-15
  (quote (sps:next-score sps:next:test:state-15 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-15:atom-2 sps:next:test:state-15
  (quote (sps:next-score sps:next:test:state-15 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-15:atom-3 sps:next:test:state-15
  (quote (sps:next-score sps:next:test:state-15 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-15:atom-4 sps:next:test:state-15
  (quote (sps:next-score sps:next:test:state-15 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-15:atom-5 sps:next:test:state-15
  (quote (sps:next-score sps:next:test:state-15 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-15:atom-6 sps:next:test:state-15
  (quote (sps:next-score sps:next:test:state-15 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-15:atom-7 sps:next:test:state-15
  (quote (sps:next-score sps:next:test:state-15 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-15:atom-8 sps:next:test:state-15
  (quote (sps:next-score sps:next:test:state-15 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-15:atom-9 sps:next:test:state-15
  (quote (sps:next-step sps:next:test:state-15 sps:n0)))
!(iggp:sps:classify sps:next:test:state-15:atom-10 sps:next:test:state-15
  (quote (sps:next-step sps:next:test:state-15 sps:n1)))
!(iggp:sps:classify sps:next:test:state-15:atom-11 sps:next:test:state-15
  (quote (sps:next-step sps:next:test:state-15 sps:n2)))
!(iggp:sps:classify sps:next:test:state-15:atom-12 sps:next:test:state-15
  (quote (sps:next-step sps:next:test:state-15 sps:n3)))
!(iggp:sps:classify sps:next:test:state-16:atom-1 sps:next:test:state-16
  (quote (sps:next-score sps:next:test:state-16 sps:p1 sps:n0)))
!(iggp:sps:classify sps:next:test:state-16:atom-2 sps:next:test:state-16
  (quote (sps:next-score sps:next:test:state-16 sps:p2 sps:n0)))
!(iggp:sps:classify sps:next:test:state-16:atom-3 sps:next:test:state-16
  (quote (sps:next-score sps:next:test:state-16 sps:p1 sps:n1)))
!(iggp:sps:classify sps:next:test:state-16:atom-4 sps:next:test:state-16
  (quote (sps:next-score sps:next:test:state-16 sps:p2 sps:n1)))
!(iggp:sps:classify sps:next:test:state-16:atom-5 sps:next:test:state-16
  (quote (sps:next-score sps:next:test:state-16 sps:p1 sps:n2)))
!(iggp:sps:classify sps:next:test:state-16:atom-6 sps:next:test:state-16
  (quote (sps:next-score sps:next:test:state-16 sps:p2 sps:n2)))
!(iggp:sps:classify sps:next:test:state-16:atom-7 sps:next:test:state-16
  (quote (sps:next-score sps:next:test:state-16 sps:p1 sps:n3)))
!(iggp:sps:classify sps:next:test:state-16:atom-8 sps:next:test:state-16
  (quote (sps:next-score sps:next:test:state-16 sps:p2 sps:n3)))
!(iggp:sps:classify sps:next:test:state-16:atom-9 sps:next:test:state-16
  (quote (sps:next-step sps:next:test:state-16 sps:n0)))
!(iggp:sps:classify sps:next:test:state-16:atom-10 sps:next:test:state-16
  (quote (sps:next-step sps:next:test:state-16 sps:n1)))
!(iggp:sps:classify sps:next:test:state-16:atom-11 sps:next:test:state-16
  (quote (sps:next-step sps:next:test:state-16 sps:n2)))
!(iggp:sps:classify sps:next:test:state-16:atom-12 sps:next:test:state-16
  (quote (sps:next-step sps:next:test:state-16 sps:n3)))
!(iggp:sps:classify sps:terminal:train:state-1:atom-1 sps:terminal:train:state-1
  (quote (sps:terminal sps:terminal:train:state-1)))
!(iggp:sps:classify sps:terminal:train:state-2:atom-1 sps:terminal:train:state-2
  (quote (sps:terminal sps:terminal:train:state-2)))
!(iggp:sps:classify sps:terminal:train:state-3:atom-1 sps:terminal:train:state-3
  (quote (sps:terminal sps:terminal:train:state-3)))
!(iggp:sps:classify sps:terminal:train:state-4:atom-1 sps:terminal:train:state-4
  (quote (sps:terminal sps:terminal:train:state-4)))
!(iggp:sps:classify sps:terminal:train:state-5:atom-1 sps:terminal:train:state-5
  (quote (sps:terminal sps:terminal:train:state-5)))
!(iggp:sps:classify sps:terminal:train:state-6:atom-1 sps:terminal:train:state-6
  (quote (sps:terminal sps:terminal:train:state-6)))
!(iggp:sps:classify sps:terminal:train:state-7:atom-1 sps:terminal:train:state-7
  (quote (sps:terminal sps:terminal:train:state-7)))
!(iggp:sps:classify sps:terminal:train:state-8:atom-1 sps:terminal:train:state-8
  (quote (sps:terminal sps:terminal:train:state-8)))
!(iggp:sps:classify sps:terminal:train:state-9:atom-1 sps:terminal:train:state-9
  (quote (sps:terminal sps:terminal:train:state-9)))
!(iggp:sps:classify sps:terminal:train:state-10:atom-1 sps:terminal:train:state-10
  (quote (sps:terminal sps:terminal:train:state-10)))
!(iggp:sps:classify sps:terminal:train:state-11:atom-1 sps:terminal:train:state-11
  (quote (sps:terminal sps:terminal:train:state-11)))
!(iggp:sps:classify sps:terminal:train:state-12:atom-1 sps:terminal:train:state-12
  (quote (sps:terminal sps:terminal:train:state-12)))
!(iggp:sps:classify sps:terminal:train:state-13:atom-1 sps:terminal:train:state-13
  (quote (sps:terminal sps:terminal:train:state-13)))
!(iggp:sps:classify sps:terminal:validate:state-1:atom-1 sps:terminal:validate:state-1
  (quote (sps:terminal sps:terminal:validate:state-1)))
!(iggp:sps:classify sps:terminal:validate:state-2:atom-1 sps:terminal:validate:state-2
  (quote (sps:terminal sps:terminal:validate:state-2)))
!(iggp:sps:classify sps:terminal:validate:state-3:atom-1 sps:terminal:validate:state-3
  (quote (sps:terminal sps:terminal:validate:state-3)))
!(iggp:sps:classify sps:terminal:test:state-1:atom-1 sps:terminal:test:state-1
  (quote (sps:terminal sps:terminal:test:state-1)))
!(iggp:sps:classify sps:terminal:test:state-2:atom-1 sps:terminal:test:state-2
  (quote (sps:terminal sps:terminal:test:state-2)))
!(iggp:sps:classify sps:terminal:test:state-3:atom-1 sps:terminal:test:state-3
  (quote (sps:terminal sps:terminal:test:state-3)))
!(iggp:sps:classify sps:terminal:test:state-4:atom-1 sps:terminal:test:state-4
  (quote (sps:terminal sps:terminal:test:state-4)))

!(iggp:sps:proof-shapes
  sps:next:train:state-3
  (quote (sps:next-score sps:next:train:state-3 sps:p1 sps:n2)))

!(iggp:sps:proof-shapes
  sps:next:train:state-6
  (quote (sps:next-score sps:next:train:state-6 sps:p1 sps:n0)))

!(iggp:sps:proof-shapes
  sps:next:train:state-1
  (quote (sps:next-score sps:next:train:state-1 sps:p1 sps:n0)))
