!(import! &self (library lib_import))
!(git-import! "https://github.com/iCog-Labs-Dev/metta-attention.git")
!(import! &self (library lib_he))
!(import! &self (library lib_spaces))
!(import! &self (library metta-attention attention/AttentionParam))
!(import! &self (library metta-attention attention-bank/utilities/helper-functions))
!(import! &self (library metta-attention attention-bank/bank/importance-index/importance-index))
!(import! &self (library metta-attention attention-bank/bank/attention-bank))
!(import! &self (library metta-attention attention-bank/attention-value/getter-and-setter))
!(import! &self (library metta-attention attention-bank/bank/attentional-focus/attentional-focus))
!(import! &self (library metta-attention attention-bank/utilities/recentVal))

!(setAv A (400 400 0))
!(setAv Cat (400 400 1))
!(setAv Animal (400 400 1))
!(setAv Human (400 400 1))
!(setAv (a) (397 400 1))
!(setAv (C) (400 400 1))
!(setAv (EvaluationLink a b) (300 200 0))
!(setAv (Hebbianlink a b) (300 200 0))
!(setAv (Hebbianlink Human Cat) (200 200 0))
!(setAv (SimilarityLink Human Cat) (200 200 0))
!(setAv (Hebbianlink (Hebbianlink Human Cat) Animal) (300 200 0))
!(setAv (Hebbianlink (Hebbianlink Animal Human) (Hebbianlink Animal Cat)) (200 200 0))
!(setAv k (200 200 200))

!(println! (bin-match-count
    (match-count (TypeSpace) ($patt ($val (Bin $binNumber))))))
!(println! (bin-collapse
    (msort
        (collapse
            (match (TypeSpace)
                   ($patt ($val (Bin $binNumber)))
                   $patt)))))
!(println! (bin-fold
    (msort
        (foldall union-atom
                 (match (TypeSpace)
                        ($patt ($val (Bin $binNumber)))
                        ($patt))
                 ()))))
!(println! (bin-collector (msort (getAllAtomsWithBins))))
!(println! (bin-size (size-atom (getAllAtomsWithBins))))
