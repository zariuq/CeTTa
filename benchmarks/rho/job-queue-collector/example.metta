!(import! &self rho)

(= (queue-job $jobs $payload)
   (rho.send $jobs (rho.send $payload rho.nil)))

(= (queue-worker $jobs $done)
   (rho.recv $jobs $job
     (rho.send $done (rho.drop $job))))

(= (queue-collector $done0 $done1 $out0 $out1)
   (rho.recv $done0 $r0
     (rho.recv $done1 $r1
       (rho.par
         (rho.send $out0 (rho.drop $r0))
         (rho.send $out1 (rho.drop $r1))))))

(= (job-queue-program $jobs $done0 $done1 $out0 $out1 $payload0 $payload1)
   (rho.par
     (queue-collector $done0 $done1 $out0 $out1)
     (queue-worker $jobs $done0)
     (queue-worker $jobs $done1)
     (queue-job $jobs $payload0)
     (queue-job $jobs $payload1)))

!(eval
  (job-queue-program
    $jobs $done0 $done1 $out0 $out1 $payload0 $payload1))
