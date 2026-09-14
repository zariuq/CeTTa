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

!(println! (fund-before (getAttentionParam FUNDS_STI)))
!(println! (type-space (TypeSpace)))
!(println! (type-space-match (collapse (match (TypeSpace) (A $value) $value))))
!(println! (type-space-fold-direct
    (foldall union-atom (match &typeSpace (A $value) ($value)) ())))
!(println! (type-space-fold
    (foldall union-atom (match (TypeSpace) (A $value) ($value)) ())))
!(println! (type-space-collector (collector (TypeSpace) (A $value) $value)))
!(println! (get-value-type-definitions
    (collapse
        (match &self
               (= (getValueType $pattern) $body)
               found))))
!(println! (type-space-definitions
    (collapse
        (match &self
               (= (TypeSpace) $body)
               found))))
!(println! (set-av-definitions
    (collapse
        (match &self
               (= (setAv $pattern $value) $body)
               found))))
!(println! (collector-is-empty
    (== (collector (TypeSpace) (A $value) $value) ())))
!(println! (value-inline
    (let $value (collector (TypeSpace) (A $type) $type)
         (if (== $value ()) %Undefined% (car-atom $value)))))
(= (collector-probe $pattern)
    (collector (TypeSpace) ($pattern $value) $value))
(= (value-probe $pattern)
    (let $value (collector (TypeSpace) ($pattern $type) $type)
         (if (== $value ()) %Undefined% (car-atom $value))))
!(println! (collector-probe-result (collector-probe A)))
!(println! (value-probe-result (value-probe A)))
!(println! (value-before (getValueType A)))
!(println! (set-result (setAv A (200 200 0))))
!(println! (value-after (getValueType A)))
!(println! (av-after (getAv A)))
!(println! (fund-after (getAttentionParam FUNDS_STI)))
