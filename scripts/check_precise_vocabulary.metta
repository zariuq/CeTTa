; Incremental repository vocabulary checking on the core C runtime.
; Directory links are not traversed; links to ordinary files are read.
; Excluded paths and vanished entries are normal skips. Other I/O failures
; prevent a clean verdict. The authored regex compiler and matcher run on
; the core C runtime; the pattern is prepared once for the whole scan.
!(import! &self fs)
!(import! &self str)
!(import! &self system)
!(import! &self re)

(= (vocabulary:needle) (str:concat "sur" "face"))
(= (vocabulary:contains $text)
  (match &self (vocabulary:pattern $pattern)
    (case (re:is-match $pattern $text)
      (((re:failed re:invalid-utf8 $detail) (vocabulary:contains-bytes $text))
       ($result (vocabulary:regex-result $result))))))
(= (vocabulary:regex-result $result)
    (case $result
      (((re:failed $kind $detail)
        (let $shown (system:write stderr "Cannot complete vocabulary check: regex execution failed\n")
          (system:exit 2)))
       ((re:incomplete $work)
        (let $shown (system:write stderr "Cannot complete vocabulary check: regex execution unfinished\n")
          (system:exit 2)))
       ($boolean $boolean))))

; The exact serialized fields retain upstream evidence identities. An
; unrelated field, path, spelling or malformed digest is still authored text.
(= (vocabulary:pinned-source "reference")
  (str:concat "^\\s*\"examples/import_error_" (str:concat (vocabulary:needle)
    "\\.metta\":\\s*\"[0-9a-f]{64}\",?\\s*$")))
(= (vocabulary:pinned-source "observation")
  (str:concat "^\\s*\"source\":\\s*\"examples/import_error_" (str:concat (vocabulary:needle)
    "\\.metta\",?\\s*$")))
(= (vocabulary:pinned-match $kind $text)
  (match &self (vocabulary:pinned-pattern $kind $pattern)
    (case (re:is-match $pattern $text)
      (((re:failed re:invalid-utf8 $detail) False)
       ($result (vocabulary:regex-result $result))))))
(= (vocabulary:pinned-field $relative $text)
  (case $relative
    (("langdef/petta/reference.json" (vocabulary:pinned-match "reference" $text))
     ("tests/petta/corrected_stable_observations.json"
        (vocabulary:pinned-match "observation" $text))
     ($other False))))

(= (vocabulary:prepare-pinned)
 (let $reference (re:compile (vocabulary:pinned-source "reference"))
 (let $observation (re:compile (vocabulary:pinned-source "observation"))
 (case (VocabularyPatterns $reference $observation)
  (((VocabularyPatterns (NativeHandle $kind1 $id1) (NativeHandle $kind2 $id2))
    (let $saved1 (add-atom &self (vocabulary:pinned-pattern "reference" $reference))
     (let $saved2 (add-atom &self (vocabulary:pinned-pattern "observation" $observation))
      (vocabulary:main))))
   ($fault
    (let $shown (system:write stderr "Cannot complete vocabulary check: regex preparation failed\n")
      (system:exit 2))))))))
; POSIX filenames can contain non-text bytes. Keep each such byte as a
; barrier, as Python's surrogateescaped path spelling does, rather than
; deleting bytes and accidentally joining two halves of a word.
(= (vocabulary:contains-bytes $text)
  (let $needle (vocabulary:needle)
    (case (str:find-ascii-uncased $text $needle)
      (((TextNotFound $literal) False)
       ($offset (>= $offset 0))))))

(= (vocabulary:hit $count $message)
  (let $heading (if (== $count 0)
      (system:write stderr "Banned syntax/interface metaphor found; use the concrete noun:\n")
      True)
    (let $written (system:write stderr (str:concat $message "\n"))
      (+ $count 1))))
(= (vocabulary:problem $count $message)
  (let $written (system:write stderr (str:concat "Cannot complete vocabulary check: "
                                   (str:concat $message "\n")))
    (+ $count 1)))
(= (vocabulary:fault $count $relative $phase $fault)
  (case $fault
    (((fs:failed fs:missing $operation $path $code $progress $message) $count)
     ((fs:failed $kind $operation $path $code $progress $message)
      (vocabulary:problem $count
        (str:concat $relative (str:concat ": " (str:concat $phase
          (str:concat ": " $message))))))
     ($unexpected (vocabulary:problem $count (str:concat $relative ": unexpected operation result"))))))

(: vocabulary:rows (-> %Undefined% Atom %Undefined% %Undefined%))
(= (vocabulary:rows $relative $rows $count)
  (case $rows
    ((LNil $count)
     ((LCons $row $rest)
      (let $next (vocabulary:row $relative $row $count)
        (vocabulary:rows $relative $rest $next))))))
(: vocabulary:row (-> %Undefined% Atom %Undefined% %Undefined%))
(= (vocabulary:row $relative $row $count)
  (case $row
    (((LineV1 $number $text)
      (if (vocabulary:contains $text)
          (if (vocabulary:pinned-field $relative $text) $count
            (vocabulary:hit $count (str:concat $relative (str:concat ":"
              (str:concat (str:number->string $number) (str:concat ":" $text))))))
          $count))
     ($unexpected (vocabulary:problem $count (str:concat $relative ": unexpected line record"))))))

; A match crossing a chunk seam needs at most one pattern's worth of prior
; scalar units. Four bytes per scalar is a conservative UTF-8 byte bound.
; Keep the pending-line hit flag until that line has actually been emitted.
(= (vocabulary:tail $text)
  (let $needle (vocabulary:needle)
    (let $limit (* 4 (str:byte-length $needle))
      (let $length (str:byte-length $text)
        (if (> $length $limit) (str:byte-slice $text (- $length $limit) $length) $text)))))
(: vocabulary:read (-> %Undefined% %Undefined% Atom Atom %Undefined% %Undefined% %Undefined% %Undefined%))
(= (vocabulary:read $relative $owner $decoder $buffer $lookbehind $pending-hit $count)
  (case (fs:read-chunk $owner 65536)
    (((fs:chunk $bytes $offset)
      (case (str:utf8-feed $decoder $bytes False)
        (((Utf8DecodedV1 $text $next-decoder)
          (let $probe (str:concat $lookbehind $text)
            (let $check (or $pending-hit (vocabulary:contains $probe))
              (if $check
                (case (str:feed-lines $buffer $text False)
                  (((LinesCollectedV1 $rows $next-buffer)
                    (let $next (vocabulary:rows $relative $rows $count)
                      (vocabulary:continue $relative $owner $next-decoder $next-buffer $probe $check $next)))
                   ($fault (vocabulary:problem $count (str:concat $relative ": line collection refused")))))
                (let $next-buffer (str:advance-lines $buffer $text False)
                  (vocabulary:continue $relative $owner $next-decoder $next-buffer $probe $check $count))))))
         ($fault (vocabulary:problem $count (str:concat $relative ": text decoding refused"))))))
     ((fs:end $offset)
      (case (str:utf8-feed $decoder "" True)
        (((Utf8DecodedV1 $text $finished-decoder)
              (case (str:feed-lines $buffer $text True)
                (((LinesCollectedV1 $rows $finished-buffer)
                  (vocabulary:rows $relative $rows $count))
                 ($fault (vocabulary:problem $count (str:concat $relative ": final line collection refused"))))))
         ($fault (vocabulary:problem $count (str:concat $relative ": final text decoding refused"))))))
     ($fault (vocabulary:fault $count $relative "read failed" $fault)))))

(: vocabulary:continue (-> %Undefined% %Undefined% Atom Atom %Undefined% %Undefined% %Undefined% %Undefined%))
(= (vocabulary:continue $relative $owner $decoder $buffer $probe $check $count)
  (case $buffer
    (((LineBufferV1 $fragments $number $cr)
      (let $pending (not (== $fragments LNil))
        (let $tail (if $pending (vocabulary:tail $probe) "")
          (vocabulary:read $relative $owner $decoder $buffer $tail (and $pending $check) $count))))
     ($fault (vocabulary:problem $count (str:concat $relative ": line advancement refused"))))))

(= (vocabulary:open-file $root $relative $name $count)
  (let $path (str:concat $root (str:concat "/" $relative))
    (case (fs:try-canonical-path $path)
      (((FS:CanonicalPath $resolved)
        (if (or (== $resolved (str:concat $root "/scripts/check_precise_vocabulary.py"))
                (== $resolved (str:concat $root "/scripts/check_precise_vocabulary.metta")))
          $count
          (let $next (if (vocabulary:contains $name)
              (vocabulary:hit $count (str:concat $relative ": banned filename")) $count)
            (let $owner (fs:open-read $path)
             (case $owner
              (((NativeHandle $kind $id)
                  (let $decoder (str:utf8-new ignore)
                    (let $buffer (str:line-new)
                      (let $result (vocabulary:read $relative $owner $decoder $buffer "" False $next)
                        (let $closed (fs:close $owner)
                          (case $closed
                            ((fs:closed $result)
                             ($fault (vocabulary:fault $result $relative "close failed" $fault)))))))))
               ($fault (vocabulary:fault $next $relative "read failed" $fault))))))))
       ($unresolved
        (vocabulary:fault $count $relative "metadata failed" (fs:metadata $path follow)))))))

(= (vocabulary:generated $name)
  (or (or (vocabulary:suffix $name ".a") (vocabulary:suffix $name ".d"))
    (or (or (vocabulary:suffix $name ".gcda") (vocabulary:suffix $name ".gcno"))
      (or (or (vocabulary:suffix $name ".o") (vocabulary:suffix $name ".pdf"))
        (or (vocabulary:suffix $name ".pyc") (vocabulary:suffix $name ".so"))))))
(= (vocabulary:suffix $name $suffix)
  (and (not (== $name $suffix)) (str:ends-with $name $suffix)))

(= (vocabulary:entry $root $relative $name $count)
  (if (fs:walk-excluded $name (".git" "__pycache__" "runtime" "target")) $count
    (let $path (str:concat $root (str:concat "/" $relative))
      (case (fs:metadata $path no-follow)
        (((fs:info directory $size $identity)
          (vocabulary:open-directory $root $relative $count))
         ((fs:info regular $size $identity)
          (if (vocabulary:generated $name) $count
            (vocabulary:open-file $root $relative $name $count)))
         ((fs:info symlink $size $identity)
          (case (fs:metadata $path follow)
            (((fs:info regular $target-size $target-identity)
              (if (vocabulary:generated $name) $count
                (vocabulary:open-file $root $relative $name $count)))
             ((fs:info $kind $target-size $target-identity) $count)
             ($fault (vocabulary:fault $count $relative "metadata failed" $fault)))))
         ((fs:info $kind $size $identity) $count)
         ($fault (vocabulary:fault $count $relative "metadata failed" $fault)))))))

(= (vocabulary:directory-step $root $relative $owner $count)
 (if (< $count 0) $count
  (case (fs:walk-next $owner no-follow (".git" "__pycache__" "runtime" "target"))
    (((fs:walk-entry $path $child-relative $name $hint $index)
      (let $child (str:concat $relative (str:concat "/" $child-relative))
          (case (fs:walk-metadata $owner follow)
            (((fs:info regular $size $identity)
              (if (vocabulary:generated $name) $count
                (vocabulary:open-file $root $child $name $count)))
             ((fs:info $kind $size $identity) $count)
             ($fault (vocabulary:fault $count $child "metadata failed" $fault))))))
     ((fs:end $observed) $count)
     ($fault
      (let $failed
        (case $fault
          (((fs:failed $kind $operation $path $code $progress $message)
            (vocabulary:problem $count (str:concat $relative
              (str:concat ": directory scan failed: " $message))))
           ($unexpected (vocabulary:problem $count
             (str:concat $relative ": unexpected directory result")))))
        (- 0 $failed)))))))

; The walker outlives each batch. The finite fold releases temporary logical
; environments after every ground accumulator; it does not replay steps.
; End is latched by this directory pass. Later ticks only observe that end.
; A negative accumulator records an already-reported fault and stops reads.
(= (vocabulary:directory $root $relative $owner $count)
 (let $space (context-space)
  (case (_minimal-foldl-llist
     (LCons tick (LCons tick (LCons tick (LCons tick
     (LCons tick (LCons tick (LCons tick (LCons tick
     (LCons tick (LCons tick (LCons tick (LCons tick
     (LCons tick (LCons tick (LCons tick (LCons tick
     (LCons tick (LCons tick (LCons tick (LCons tick
     (LCons tick (LCons tick (LCons tick (LCons tick
     (LCons tick (LCons tick (LCons tick (LCons tick
     (LCons tick (LCons tick (LCons tick (LCons tick
        LNil))))))))))))))))))))))))))))))))
     $count $acc $tick
     (vocabulary:directory-step $root $relative $owner $acc) $space)
    (((return $next)
      (if (< $next 0) (- 0 $next)
        (case (fs:walk-current $owner)
          (((fs:end $observed) $next)
           (fs:walk-ready (vocabulary:directory $root $relative $owner $next))
           ((fs:walk-entry $path $child $name $hint $index)
            (vocabulary:directory $root $relative $owner $next))
           ($fault (vocabulary:problem $next
             (str:concat $relative ": directory continuation failed")))))))
     ($fault (vocabulary:problem $count
       (str:concat $relative ": directory batch refused")))))))
(= (vocabulary:open-directory $root $relative $count)
  (let $path (str:concat $root (str:concat "/" $relative))
    (let $owner (fs:walk-open $path)
     (case $owner
      (((NativeHandle $kind $id)
          (let $result (vocabulary:directory $root $relative $owner $count)
            (let $closed (fs:close $owner)
              (case $closed
                ((fs:closed $result)
                 ($fault (vocabulary:fault $result $relative "close failed" $fault)))))))
       ($fault (vocabulary:fault $count $relative "directory scan failed" $fault)))))))

(: vocabulary:roots (-> %Undefined% Atom %Undefined% %Undefined%))
(= (vocabulary:roots $root $roots $count)
  (case $roots
    ((() $count)
     ($nonempty
      (let ($relative $rest) (decons-atom $nonempty)
        (let $next (vocabulary:entry $root $relative $relative $count)
          (vocabulary:roots $root $rest $next)))))))
(= (vocabulary:finish $count)
  (if (> $count 0) (system:exit 1)
    (let $needle (vocabulary:needle)
      (let $message (str:concat "PASS: no use of '" (str:concat $needle
          "' as a syntax/interface metaphor\n"))
        (let $written (system:write stdout $message) (system:exit 0))))))

(= (vocabulary:main)
 (let $entry (system:entry-directory)
 (let $parent (str:concat $entry "/..")
  (case (fs:try-canonical-path $parent)
  (((FS:CanonicalPath $root)
    (vocabulary:finish (vocabulary:roots $root
      ("Makefile" "README.md" "SHOWCASE.md" "MORK_TUTORIAL.md" "benchmarks" "docs"
       "examples" "experiments" "langdef" "lib" "rust" "scripts" "specs" "src" "tests" "tools") 0)))
   ($fault
    (let $shown (system:write stderr "Cannot resolve repository root from entry file\n")
      (system:exit 2))))))))

!(let $source (str:concat "(?i)" (vocabulary:needle))
 (let $pattern (re:compile $source)
 (case $pattern
  (((NativeHandle $kind $id)
    (let $saved (add-atom &self (vocabulary:pattern $pattern))
      (vocabulary:prepare-pinned)))
   ($fault
    (let $shown (system:write stderr "Cannot complete vocabulary check: regex preparation failed\n")
      (system:exit 2)))))))
