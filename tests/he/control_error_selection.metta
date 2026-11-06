; HE's evaluation of an argument or a key keeps its errors only where no
; answer succeeds, and an argument that is an error is its call's answer.
; let and let* take the bound value as an argument; switch takes its key as
; one; case reads its key's answers as data, after the same selection.
(= (usq $x) (* $x $x))
(= (rng) (superpose (1 "a" 2)))
(= (bad) (usq "a"))
(= (isErr $x) (case $x (((Error $a $b) yes) ($v no))))
!(collapse (usq (rng)))
!(collapse (let $r (usq (rng)) (isErr $r)))
!(collapse (let $r (usq (rng)) (case $r (((Error $a $b) caught) ($v $v)))))
!(collapse (let* (($r (usq (rng)))) (isErr $r)))
!(collapse (case (usq (rng)) (((Error $a $b) caught) ($v $v))))
!(collapse (switch (usq (rng)) (((Error $a $b) caught) ($v $v))))
!(collapse (isErr (usq (rng))))
!(collapse (chain (usq (rng)) $r (isErr $r)))
!(collapse (case (bad) (((Error $a $b) caught) ($v $v))))
!(collapse (let $r (bad) (isErr $r)))
!(collapse (let $r (usq "a") (case $r (((Error $a $b) caught) ($v $v)))))
!(collapse (let $r (bad) (case $r (((Error $a $b) caught) ($v $v)))))
!(collapse (switch (bad) (((Error $a $b) caught) ($v $v))))
!(collapse (isErr (bad)))
