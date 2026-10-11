!(case (bind! 5 3) (((Error $source $reason) $reason) ($ok bound)))
!(case (bind! "text" 3) (((Error $source $reason) $reason) ($ok bound)))
!(case (bind! (a b) 3) (((Error $source $reason) $reason) ($ok bound)))
!(bind! bind-name-word 5)
!(case (bind! bind-name-word 8) (((Error $source $reason) $reason) ($ok bound)))
! bind-name-word
