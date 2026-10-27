; Source templates are representations, never executable evaluator rules.
; Header metadata and form-local name inventories remain charged IC1 roots.
; No helper is installed into a source space by preparing or learning a task.
!(import! &self "lib/petta/lib_source_codec.metta")
!(import! &self "lib/petta/lib_ic_tree_archive.metta")
!(import! &self "lib/petta/lib_ic_search.metta")
!(import! &self "lib/petta/lib_ic_packed.metta")

(= (source-archive:encode-forms $schema $forms)
   (if (== $forms ()) (IC:Some ())
     (let ($form $tail) (decons-atom $forms)
       (case ((binary-schema:to-tree $schema (Record Syntax:Form) $form)
              (source-archive:encode-forms $schema $tail))
         ((((Binary:Tree $tree) (IC:Some $rest))
           (let $trees (cons-atom $tree $rest) (IC:Some $trees)))
          ($other IC:None))))))

(= (ic:prepare-source $document)
   (if (source-codec:valid? $document)
     (case $document
       (((Syntax:Document $version $reader $forms)
         (let $schema (source-codec:schema)
           (case ((binary-schema:to-tree $schema (Record Syntax:Document)
                    (Syntax:Document $version $reader ()))
                  (source-archive:encode-forms $schema $forms))
             ((((Binary:Tree $header) (IC:Some $trees))
               (let $forest (cons-atom $header $trees) (ic:prepare-trees $forest)))
              ($other (IC:TreeFault InvalidSource))))))))
     (IC:TreeFault InvalidSource)))

(= (source-archive:decode-forms $schema $trees)
   (if (== $trees ()) (IC:Some ())
     (let ($tree $tail) (decons-atom $trees)
       (case ((binary-schema:from-tree $schema (Record Syntax:Form) $tree)
              (source-archive:decode-forms $schema $tail))
         ((((Binary:Value $form) (IC:Some $rest))
           (let $forms (cons-atom $form $rest) (IC:Some $forms)))
          ($other IC:None))))))

(= (ic:restore-source $archive)
   (case (ic:restore-trees $archive)
     (((IC:Some $trees)
       (if (== $trees ()) IC:None
         (let* ((($header $forms) (decons-atom $trees)) ($schema (source-codec:schema)))
           (case ((binary-schema:from-tree $schema (Record Syntax:Document) $header)
                  (source-archive:decode-forms $schema $forms))
             ((((Binary:Value (Syntax:Document $version $reader ())) (IC:Some $decoded))
               (let $document (Syntax:Document $version $reader $decoded)
                 (if (source-codec:valid? $document) (IC:Some $document) IC:None)))
              ($other IC:None))))))
      ($other IC:None))))

; SIC1 envelopes identify the decoder and charge every payload byte. Plain
; ST1 is the original candidate, not the larger ranked-tree intermediate.
(= (source-archive:envelope $codec $payload)
   (binary:pack-fields ((binary:utf8 "SIC1") (binary:utf8 $codec) $payload)))

(= (ic:source-pack $document)
   (case (syntax:pack $document)
     (((Binary:TreeBytes $payload) (source-archive:envelope "ST1" $payload))
      ($other IC:None))))

(= (ic:source-from-archive $archive)
   (case (ic:restore-source $archive)
     (((IC:Some $document)
       (case (ic-archive:pack $archive)
         (((Binary:TreeBytes $payload) (source-archive:envelope "IC1" $payload))
          ($other IC:None))))
      ($other IC:None))))

(= (source-archive:decode-payload $codec $payload)
   (if (== $codec (binary:utf8 "ST1"))
     (case (syntax:unpack $payload)
       (((Syntax:Value $document) (IC:Some $document)) ($other IC:None)))
     (if (== $codec (binary:utf8 "IC1"))
       (case (ic-archive:unpack $payload)
         (((IC:Some $archive) (ic:restore-source $archive)) ($other IC:None)))
       IC:None)))

(= (ic:source-unpack $bytes)
   (case (binary:unpack-fields $bytes)
     (((Binary:Fields ($version $codec $payload))
       (if (== $version (binary:utf8 "SIC1"))
         (source-archive:decode-payload $codec $payload) IC:None))
      ($other IC:None))))

(= (ic-packed:observe IC:SourceArchive $artifact) (ic:source-unpack $artifact))
(= (ic-packed:accepts? IC:SourceArchive $original $candidate) (== $original $candidate))
(= (ic:source-check $original $current $candidate)
   (case (ic-packed:check IC:SourceArchive $original $current $candidate)
     (((IC:PackedAccepted $next $before $after) (IC:SourceAccepted $next $before $after))
      ((IC:PackedRejected $reason) (IC:SourceRejected $reason)))))

; The same resumable controller can compare representations across codecs.
; Its original observation remains the complete source document, not a node
; count or a ranked-tree encoding chosen for proposal generation.
(= (ic-search:observation IC:SourceArchive $artifact) (ic:source-unpack $artifact))
(= (ic-search:consider IC:SourceArchive $original $best $candidate)
   (case (ic:source-check $original $best $candidate)
     (((IC:SourceAccepted $next $before $after) (IC:CandidateAccepted $next))
      ((IC:SourceRejected $reason) (IC:CandidateRejected $reason)))))

(= (ic-search:materialize IC:SourceArchive $original $proposal)
   (case $proposal
     (((IC:SourceTemplate $archive $name $slots $patterns)
       (case (ic-archive:factor $archive $name $slots $patterns)
         (((IC:Proposal $candidate $support)
           (if (>= $support 2)
             (let $bytes (ic:source-from-archive $candidate)
               (if (binary-tree:bytes? $bytes) (IC:Candidate $bytes)
                 (IC:ProducerFault InvalidSourceCandidate)))
             (IC:Refused InsufficientSupport)))
          ($other (IC:ProducerFault InvalidSourceTemplate)))))
      ($other (IC:ProducerFault InvalidProposal)))))
