; PeTTa's relational if accepts both two- and three-argument forms.
!(if True then-three else-three)
!(if False then-three else-three)
!(if True then-two)
!(if False then-two)

; A condition outside the Boolean relation has no branch.
!(if undecided then-unknown else-unknown)
!(if $condition then-variable else-variable)
