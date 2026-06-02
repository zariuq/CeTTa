!(import! &self rho)

(= (pipeline-source $chan)
   (rho.send $chan rho.nil))

(= (pipeline-sink $chan)
   (rho.recv $chan $done rho.nil))

(= (fwd $src $dst)
   (rho.recv $src $msg (rho.send $dst (rho.drop $msg))))

!(let $program
  (eval
    (rho.par
      (pipeline-source $c0)
      (fwd $c0 $c1)
      (fwd $c1 $c2)
      (pipeline-sink $c2)))
  $program)
