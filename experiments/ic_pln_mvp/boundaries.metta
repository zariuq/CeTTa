; Executable counterexamples to unrestricted observational equivalence.
; A successful probe reproduces a limitation, not a valid program optimization.
!(import! &self (library lib_ic_library_cost))
!(import! &self (library fs))
!(import! &self (library str))

(= (boundary:source $mode)
   (syntax:read (str:concat
      "(= (audit-f $x) (if (< $x 0) (- 0 $x) (+ (* $x $x) (* $x $x))))\n(= (audit-g $y) (if (< $y 0) (- 0 $y) (+ (* $y $y) (* $y $y))))\n(= (audit-h $z) (if (< $z 0) (- 0 $z) (+ (* $z $z) (* $z $z))))"
      (if (== $mode "reflection")
        "\n(= (audit-inspect) (match &self (= (audit-f $x) $body) (size-atom $body)))" ""))))

(= (boundary:probe $mode $directory)
   (let* (($source (boundary:source $mode))
          ($prepared (ic:prepare-library RuleHeads $source)))
     (case (context-library:seed $prepared (str:concat "ic-learned:" $mode) 0 6)
       (((Library:Candidate $bytes $support)
         (case (library-stack:view $bytes)
           (((Library:StackValue $layers $rewritten $restored)
             (let* (($a (fs:write (str:concat $directory "/original.metta") (syntax:write $source)))
                    ($b (fs:write (str:concat $directory "/rewritten.metta") (syntax:write $rewritten)))
                    ($original (ic:source-pack $source)))
               (println! (Boundary:Candidate $support
                 (== $restored $source)
                 (ic:library-cost Nodes $original)
                 (ic:library-cost Nodes $bytes)
                 (case (ic:library-check Nodes $original $original $bytes)
                   (((IC:PackedAccepted $next $before $after) Accepted)
                    ($refused $refused)))))))
            ($invalid (println! (Boundary:Fault View))))))
        ($refused (println! (Boundary:Fault Seed)))))))
