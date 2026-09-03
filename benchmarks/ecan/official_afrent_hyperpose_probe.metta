; One authentic AFRentCollectionAgent2 transition through a one-occurrence
; Hyperpose family.  The observation is intentionally order-insensitive: the
; only contract under test is whether the named-mutex-protected changes to the
; package's identity-bearing spaces become visible after the branch joins.

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
!(import! &self (library metta-attention attention/RentCollectionAgent/AFRentCollectionAgent/AFRentCollectionAgent2))

!(updateAttentionParam MAX_AF_SIZE 5.0)
!(setAv afrent_hyperpose_probe (100.0 0.0 0.0))
!(updateAttentionParam FUNDS_STI 900.0)
!(updateAttentionParam TARGET_STI 1000.0)
!(updateAttentionParam STARTING_FUNDS_STI 2000.0)
!(updateAttentionParam AFRentCoverageRatio 0.4)

!(println! (before
    (getAttentionParam FUNDS_STI)
    (getSti afrent_hyperpose_probe)))
!(collapse (hyperpose ((AFRentCollectionAgent2-run))))
!(println! (after
    (getAttentionParam FUNDS_STI)
    (getSti afrent_hyperpose_probe)))
