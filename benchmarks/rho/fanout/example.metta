!(import! &self rho)

(= (fanout-source $bcast $payload)
   (rho.send $bcast $payload))

(= (fanout-sink $chan)
   (rho.recv $chan $sink rho.nil))

(= (fanout-send $chan $msg)
   (rho.send $chan (rho.drop $msg)))

!(let $program
  (eval
    (rho.par
      (fanout-source $bcast rho.nil)
      (rho.recv $bcast $msg
        (rho.par
          (fanout-send $c0 $msg)
          (fanout-send $c1 $msg)))
      (fanout-sink $c0)
      (fanout-sink $c1)))
  $program)
