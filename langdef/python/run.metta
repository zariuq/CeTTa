; Running a Python program: the core of the core stage, run on Machine.module's
; compiled program (generated/machine_program_v1.metta), whose host relations
; ask host_runtime.metta.  The run stage of langdef.metta: the program's
; output is its own, and the run ends the process with the program's status.
!(import! &self host_runtime)
!(import! &self generated/machine_program_v1)
!(import! &self generated/prelude_v1)
!(import! &self generated/lib_math_v1)
!(import! &self generated/lib___future___v1)
!(import! &self generated/lib_linecache_v1)
!(import! &self generated/lib_warnings_v1)
!(import! &self generated/lib_string/__init___v1)
!(import! &self generated/lib_string/templatelib_v1)
!(import! &self generated/lib_typing_v1)
!(import! &self generated/lib_itertools_v1)
!(import! &self generated/lib__thread_v1)
!(import! &self generated/lib__weakref_v1)
!(import! &self generated/lib__ast_v1)
; the heap the prelude leaves, written at build time (image.metta)
!(import! &py-heap generated/prelude_image_v1)

; the prelude first (the data model's protocols: its heap image, else its
; run), then the program
(= (pyrt:start-from-image $code)
   (let $st (collapse (match &py-heap (py-image-state $ids $p) (py-image-state $ids $p)))
     (if (== $st ()) (pyrt:start-prelude $code)
       (let ((py-image-state $ids $p)) $st
         (let $u1 (change-state! &py-ids $ids)
           (let $u2 (change-state! &py-prelude $p)
             (let $u3 (remove-atom &py-heap (py-image-state $ids $p))
               (let $u4 (pyrt:image-specials (collapse (match &py-heap (py-image-special $a) $a)))
                 (m:val (py:None))))))))))
; an atom of the image holding an infinity, a NaN or a string with a NUL,
; decoded into the heap
(= (pyrt:image-specials $as)
   (if (== $as ()) ()
     (let ($a $rest) (decons-atom $as)
       (let $r (remove-atom &py-heap (py-image-special $a))
         (let $d (pyrt:image-decode $a)
           (let $u (add-atom &py-heap $d) (pyrt:image-specials $rest)))))))
(= (pyrt:image-decode $a)
   (case $a
     (((py-image-float inf $s) (float:div 1.0 0.0))
      ((py-image-float -inf $s) (float:div -1.0 0.0))
      ((py-image-float nan $s) (float:copysign (float:div 0.0 0.0) (if $s -1.0 1.0)))
      ((py-image-text $cps) (str:from-codepoints $cps))
      ($other (if (== (get-metatype $a) Expression) (pyrt:image-decode-each $a) $a)))))
(= (pyrt:image-decode-each $xs)
   (if (== $xs ()) ()
     (let ($x $rest) (decons-atom $xs) (cons-atom (pyrt:image-decode $x) (pyrt:image-decode-each $rest)))))
(= (python:run $code)
   (case (pyrt:start-from-image (python:prelude-code))
     (((m:err $e) (let $w (pyrt:report $e) (system:exit 70)))
      ($ok (case (catch (collapse (gslt:entry:python-machine:rewrite:10 (quote (MRun $code)))))
             ((((gslt:result:python-machine:rewrite:10 (m:exit $status))) (let $f (pyrt:prelude-sync "_at_exit" ()) (system:exit $status)))
              ($other (pyrt:machine-stopped $other))))))))
; the machine reached no exit: a host relation gave a step nothing it can
; read, or raised (a PeTTa error, caught here): a failure of the runtime,
; reported with the state or error it stopped at, never a quiet end of the
; program
(= (pyrt:machine-stopped $states)
   (let $text (repr $states)
     (let $n (str:byte-length $text)
       (let $w (system:write stderr
                 (str:concat "Fatal Python error: the machine stopped without an exit: "
                   (str:concat (if (> $n 2000) (str:concat (str:byte-slice $text 0 2000) " ...") $text) "\n")))
         (system:exit 70)))))
