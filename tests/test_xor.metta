; HE arithmetics.rs: xor is a grounded boolean operator. It evaluates on boolean
; atoms and stays unreduced when symbolic inputs prevent grounding.

!(xor True False)
!(xor True True)
!(xor False False)
!(xor True A)
