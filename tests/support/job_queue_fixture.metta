!(import! &self "../../examples/kb/space_job_queue.metta")
(= (job-fixture:run (Clock $value))
  ((get-metatype $value) (get-type $value) (jobs:time? $value)))
(= (job-fixture:run $request) (jobs:run $request))

(= (danger) (println! ACCIDENTAL_EXECUTION))
