; DAG1 records use the same byte framing and schema interpreter as IC1.
; Payload bytes, every child reference and the ordered root vector are charged.
; Decoding admits the chronological records and creates new owned authority.
!(import! &self (library dag))
!(import! &self (library lib_binary_schema))

; Logical unfolded-tree lengths, not the size of the DAG1 transport.
(= (dag:encoded-lengths $handle)
   (dag:fold-unique $handle binary-tree:leaf-length binary-tree:node-length))

(= (dag:codec-schema)
   ((Binary:Form 1 Dag:Archive (Text (Record Dag:Forest)))
    (Binary:Form 2 Dag:Forest ((Vector Natural) (Vector (Either (Binary:Leaf Binary:Node)))))
    (Binary:Form 3 Binary:Leaf (Bytes))
    (Binary:Form 4 Binary:Node (Bytes (Vector Natural)))))

(= (dag:pack $handle)
   (case (dag:export $handle)
     (((Dag:Forest $roots $records)
       (binary-schema:encode (dag:codec-schema) (Record Dag:Archive)
         (Dag:Archive "DAG1" (Dag:Forest $roots $records))))
      ($other $other))))

(= (dag:unpack $bytes)
   (case (binary-schema:decode (dag:codec-schema) (Record Dag:Archive) $bytes)
     (((Binary:Value (Dag:Archive "DAG1" $records)) (dag:from-records $records))
      ((Binary:Value $other) (Dag:Fault WrongVersion))
      ($other $other))))
