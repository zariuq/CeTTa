; How a refused Python program is reported (the langdef's (refuse ENTRY)).
; A syntax error, (python:syntax-error MESSAGE LINE COL ELINE ECOL) with
; 0-based columns, is reported as CPython reports it when it runs the file;
; when the run stops at an earlier stage, as "line L, column C: MESSAGE",
; C 1-based (the form the syntax-error gate reads).  Any other refusal is
; SOURCE: WHY.  The run then ends with status 1.
!(import! &self str)
!(import! &self fs)
!(import! &self system)

(= (python:placed $line $col $why)
   (str:concat (str:concat (str:concat "line " (str:number->string $line))
                           (str:concat ", column " (str:number->string (+ $col 1))))
               (str:concat ": " $why)))

(= (python:refuse $source $why $full)
   (let $w (system:write stderr (python:refusal-text $source $why $full)) (system:exit 1)))
(= (python:refusal-text $source $why $full)
   (case $why
     (((python:syntax-error $msg $l $c $el $ec)
       (if $full (python:syntax-error-block $source $msg $l $c $el $ec)
         (str:concat (str:concat $source ": ") (str:concat (python:placed $l $c $msg) "\n"))))
      ($other (str:concat (str:concat $source ": ") (str:concat (python:refusal-why $why) "\n"))))))
(= (python:refusal-why $why) (if (== (get-metatype $why) Symbol) (str:symbol->string $why) $why))

; traceback's _format_syntax_error: the file and the line; the line's text
; less its indentation, with carets under the error's columns; the message
(= (python:syntax-error-block $source $msg $l $c $el $ec)
   (str:concat
     (str:concat (str:concat "  File \"" $source) (str:concat "\", line " (str:concat (str:number->string $l) "\n")))
     (str:concat (python:error-text-lines (python:source-line $source $l) $l $c $el $ec)
                 (str:concat (str:concat "SyntaxError: " $msg) "\n"))))
; the program's line L (1-based) without its end of line; "" past its end
(= (python:source-line $source $l)
   (let $lines (str:split "\n" (fs:read-text $source))
     (if (> $l (size-atom $lines)) "" (index-atom $lines (- $l 1)))))
; the offsets 1-based; an end on another line is the line's end; an end not
; after the start is one past it; the carets under the stripped text
(= (python:error-text-lines $rtext $l $c $el $ec)
   (let $cs (str:codepoints $rtext)
     (let $n (size-atom $cs)
       (let $lcs (python:lstrip-codes $cs)
         (let $spaces (- $n (size-atom $lcs))
           (let $ltext (str:join "" $lcs)
             (let $offset0 (+ $c 1)
               (let $end0 (if (== $el $l) (+ $ec 1) (+ $n 1))
                 (let $offset (if (> $offset0 (+ $n 1)) (+ $n 1) $offset0)
                   (let $end1 (if (> $end0 (+ $n 1)) (+ $n 1) $end0)
                     (let $end (if (if (>= $offset $end1) true (< $end1 0)) (+ $offset 1) $end1)
                       (let $colno (- (- $offset 1) $spaces)
                         (let $endcol (- (- $end 1) $spaces)
                           (if (>= $colno 0)
                               (str:concat (str:concat "    " (str:concat $ltext "\n"))
                                 (str:concat (str:concat "    " (python:caret-space $lcs $colno))
                                             (str:concat (python:repeat-text "^" (- $endcol $colno)) "\n")))
                               (str:concat "    " (str:concat $ltext "\n"))))))))))))))))
(= (python:lstrip-codes $cs)
   (if (== (size-atom $cs) 0) $cs
     (let ($x $rest) (decons-atom $cs) (if (== $x " ") (python:lstrip-codes $rest) $cs))))
; the text before the carets: a tab kept, any other character a space
(= (python:caret-space $cs $k)
   (if (== $k 0) ""
     (if (== (size-atom $cs) 0) ""
       (let ($x $rest) (decons-atom $cs)
         (str:concat (if (== $x "\t") "\t" " ") (python:caret-space $rest (- $k 1)))))))
(= (python:repeat-text $s $n) (if (<= $n 0) "" (str:concat $s (python:repeat-text $s (- $n 1)))))
