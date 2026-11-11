; Run: cetta --lang petta examples/kb/db_quickstart.metta -- claims.db
; Re-running inserts another occurrence of each fact.
!(import! &self (library db))
!(import! &self (library system))
!(db:open claims (system:arg 1))
!(db:transaction claims
   (|-> ($facts)
     (let* (($_ (add-atom $facts (supports e1 c1)))
            ($_ (add-atom $facts (source e1 paper7))))
       done)))
!(println! (collapse
   (match (db:snapshot claims)
          (, (supports $e $claim) (source $e $origin))
          ($claim $origin))))
!(db:close claims)
