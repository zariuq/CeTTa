!(import! &self rho)

(= (payload-pair $head $tail)
   (rho.send $head (rho.send $tail rho.nil)))

(= (send-computed $dst $head $tail)
   (let $payload (eval (payload-pair $head $tail))
     (rho.send $dst $payload)))

!(let $program
  (eval (send-computed $out $head $tail))
  $program)
