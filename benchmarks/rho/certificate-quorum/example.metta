!(import! &self rho)

(= (certificate-proc-if $required (cert $task-tag $task-name $status $cert))
   (if-equal $status $required
     (rho.send $task-name (rho.send $cert rho.nil))
     rho.nil))

(= (auditor-proc (audit $task-tag $task-name $sink))
   (rho.recv $task-name $msg (rho.send $sink (rho.drop $msg))))

(= (build-certificates $required $certs)
   (foldl-atom $certs rho.nil $acc $cert
     (let $head (eval (certificate-proc-if $required $cert))
       (rho.par $acc $head))))

(= (build-auditors $auditors)
   (foldl-atom $auditors rho.nil $acc $audit
     (let $head (eval (auditor-proc $audit))
       (rho.par $acc $head))))

!(let $program
  (eval
    (let $cert-procs
      (eval (build-certificates valid
        ((cert task0 $task0 valid $cert0)
         (cert task0 $task0 stale $cert1)
         (cert task1 $task1 valid $cert2))))
      (let $audit-procs
        (eval (build-auditors
          ((audit task0 $task0 $sink0)
           (audit task1 $task1 $sink1))))
        (rho.par $cert-procs $audit-procs))))
  $program)
