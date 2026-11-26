; A language whose definition is a pipeline of stages (a manifest whose root is
; gslt-language-pipeline-v1): its source text is read with a compiled
; presentation, and each stage, a MeTTa program and its entry function, takes
; the value of the stage it names as its input, or of the stage before it.
;
;   (gslt-language-pipeline-v1
;     (name NAME)
;     (presentation PRESENTATION)            the syntax: a registered presentation
;     [(refuse ENTRY)]                        the language's own report of a refusal
;     (stage STAGE PROGRAM ENTRY [INPUT]) ...) in order; PROGRAM relative to the
;                                              manifest; the first stage's input
;                                              is the presentation's tree
;
;   language_pipeline.metta MANIFEST SOURCE [STAGE]
;
; prints the value of the last stage, or of STAGE, computing only that stage
; and the stages it takes its input from.  A source the presentation refuses,
; or a stage whose value is an (Error WHERE WHY), is reported on standard
; error, and the run exits with status 1: by (ENTRY SOURCE WHY FULL) when the
; language names a refuse ENTRY (FULL: whether the run goes through the last
; stage), else as SOURCE: WHY.  A stage that raises is reported as SOURCE:
; internal error in stage STAGE, with status 1.  The stages' programs are imported
; first, by an evaluation of their own: an evaluation reads the space as it
; was when it began, so equations an import adds serve the evaluations after
; it.
!(import! &self syntax)
!(import! &self fs)
!(import! &self system)
!(import! &self str)
!(import! &self module_files)

(= (pipeline:text $x) (if (== (get-metatype $x) Symbol) (str:symbol->string $x) $x))

; a path from the command line, against the working directory (import! reads
; a relative path against the importing file's directory)
(= (pipeline:absolute $path)
   (if (str:starts-with $path "/") $path (str:concat (str:concat (system:cwd) "/") $path)))

; the manifest's fields
(= (pipeline:fields $path)
   (let $space (new-space)
     (let $loaded (import! $space $path)
       (match $space $root
         (if (== (car-atom $root) gslt-language-pipeline-v1) (cdr-atom $root) (empty))))))

(= (pipeline:presentation $fields)
   (if (== $fields ()) (empty)
     (let ($f $rest) (decons-atom $fields)
       (case $f (((presentation $p) (pipeline:text $p)) ($other (pipeline:presentation $rest)))))))

; the stages, each as (stage NAME PROGRAM ENTRY INPUT): INPUT the stage it
; names, else the one before it (pipeline:source for the first)
(= (pipeline:stages-of $fields) (pipeline:with-inputs (pipeline:declared $fields) pipeline:source))
(= (pipeline:declared $fields)
   (if (== $fields ()) ()
     (let ($f $rest) (decons-atom $fields)
       (case $f
         (((stage $name $program $entry) (cons-atom $f (pipeline:declared $rest)))
          ((stage $name $program $entry $input) (cons-atom $f (pipeline:declared $rest)))
          ($other (pipeline:declared $rest)))))))
(= (pipeline:with-inputs $stages $previous)
   (if (== $stages ()) ()
     (let ($s $rest) (decons-atom $stages)
       (let $full (pipeline:with-input $s $previous)
         (let (stage $name $program $entry $input) $full
           (cons-atom $full (pipeline:with-inputs $rest $name)))))))
(= (pipeline:with-input (stage $name $program $entry) $previous) (stage $name $program $entry $previous))
(= (pipeline:with-input (stage $name $program $entry $input) $previous) (stage $name $program $entry $input))

; the stage to stop at: the one asked for, else the last
(= (pipeline:stop-of $stages $stop)
   (if (== $stop (pipeline:none)) (pipeline:last-name $stages) $stop))
(= (pipeline:last-name $stages)
   (let ($s $rest) (decons-atom $stages)
     (if (== $rest ()) (let (stage $name $program $entry $input) $s $name) (pipeline:last-name $rest))))

; the stages a stage needs: itself and, through inputs, the ones before it
(= (pipeline:needed $stages $name)
   (if (== $name pipeline:source) ()
     (let (stage $n $program $entry $input) (pipeline:find $stages $name)
       (cons-atom $name (pipeline:needed $stages $input)))))
(= (pipeline:find $stages $name)
   (let ($s $rest) (decons-atom $stages)
     (let (stage $n $program $entry $input) $s
       (if (== $n $name) $s (pipeline:find $rest $name)))))

(= (pipeline:refuser $fields)
   (if (== $fields ()) pipeline:none
     (let ($f $rest) (decons-atom $fields)
       (case $f (((refuse $entry) $entry) ($other (pipeline:refuser $rest)))))))
(= (pipeline:refuse-by (pipeline:refusal $entry $full) $source $why)
   (if (== $entry pipeline:none) (pipeline:refuse $source $why) ($entry (pipeline:text $source) $why $full)))
(= (pipeline:refuse $source $why)
   (let $written (system:write stderr (str:concat (str:concat (pipeline:text $source) ": ") (pipeline:text $why)))
     (let $newline (system:write stderr "\n") (system:exit 1))))

; the programs of the stages that run
(= (pipeline:load $manifest $stop)
   (let $stages (pipeline:stages-of (pipeline:fields $manifest))
     (pipeline:load-each (module-files:dir-of $manifest) $stages
                         (pipeline:needed $stages (pipeline:stop-of $stages $stop)))))
(= (pipeline:load-each $dir $stages $needed)
   (if (== $stages ()) ()
     (let ($s $rest) (decons-atom $stages)
       (let (stage $name $program $entry $input) $s
         (if (is-member $name $needed)
             ; import! does not evaluate its path: the path is computed first
             (let $path (str:concat $dir (pipeline:text $program))
               (let $loaded (import! &self $path)
                 (pipeline:load-each $dir $rest $needed)))
             (pipeline:load-each $dir $rest $needed))))))

; each needed stage's entry applied, in order, to its input's value
(= (pipeline:stages $source $stages $needed $values $stop $refusal)
   (if (== $stages ()) (pipeline:value-of $values $stop)
     (let ($s $rest) (decons-atom $stages)
       (let (stage $name $program $entry $input) $s
         (if (is-member $name $needed)
             (case (catch (pipeline:value ($entry (pipeline:value-of $values $input))))
               (((pipeline:value $next)
                 (case $next
                   (((Error $where $why) (pipeline:refuse-by $refusal $source $why))
                    ($ok (if (== $name $stop) $next
                             (pipeline:stages $source $rest $needed (cons-atom ($name $next) $values) $stop $refusal))))))
                ; a stage that raised (an error no stage declares): the
                ; language's failure, reported, never a quiet end
                ($raised (pipeline:refuse $source (str:concat (str:concat "internal error in stage " (pipeline:text $name))
                                                              (str:concat ": " (repr $raised)))))))
             (pipeline:stages $source $rest $needed $values $stop $refusal))))))
(= (pipeline:value-of $values $name)
   (let ($v $rest) (decons-atom $values)
     (let ($n $x) $v (if (== $n $name) $x (pipeline:value-of $rest $name)))))

; the source being run is known to its stages, (pipeline:current-source PATH)
; in the space (a language's run stage names its program's file with it)
(= (pipeline:run $manifest $source $stop)
   (let $fields (pipeline:fields $manifest)
     (let $known (add-atom &self (pipeline:current-source $source))
     (let $stages (pipeline:stages-of $fields)
     (let $last (pipeline:stop-of $stages $stop)
     (let $refusal (pipeline:refusal (pipeline:refuser $fields) (== $last (pipeline:last-name $stages)))
     (let $read (syntax:read-in (pipeline:presentation $fields) (fs:read-text $source))
       (case $read
         (((Syntax:Fault $kind $why) (pipeline:refuse-by $refusal $source $why))
          ($cst (pipeline:stages $source $stages (pipeline:needed $stages $last)
                                 ((pipeline:source $cst)) $last $refusal)))))))))))

; the command line: [--] MANIFEST SOURCE [STAGE] (a .metta path right after
; the program would be read as one more program: -- passes it as data)
(= (pipeline:args)
   (let $all (system:args)
     (if (== $all ()) ()
       (let ($first $rest) (decons-atom $all) (if (== $first --) $rest $all)))))
(= (pipeline:manifest-arg) (let ($m $rest) (decons-atom (pipeline:args)) (pipeline:absolute (pipeline:text $m))))
(= (pipeline:source-arg)
   (let ($m $rest) (decons-atom (pipeline:args)) (let ($s $more) (decons-atom $rest) (pipeline:text $s))))
(= (pipeline:stop-arg)
   (let ($m $rest) (decons-atom (pipeline:args))
     (let ($s $more) (decons-atom $rest)
       (if (== $more ()) (pipeline:none) (let ($stop $none) (decons-atom $more) $stop)))))

!(pipeline:load (pipeline:manifest-arg) (pipeline:stop-arg))
!(println! (pipeline:run (pipeline:manifest-arg) (pipeline:source-arg) (pipeline:stop-arg)))
