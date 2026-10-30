; A library native receives normal forms at its strict operand positions.
; Raw positions follow the dispatch data contract. A deterministic-equation program run through
; an equation's parameters matches the same envelope it matches when written
; directly.
!(import! &self lib_tptp)
(= (raw-run $h $call) (collapse (__cetta_lib_deterministic_equations_run $h $call)))
!(R1 (let $program (tptp:open-resolution-input-view) (raw-run $program (LangDef:DeterministicCall tptp-res-v1:expanded-items (tptp-ci-v1:expanded-inputs-nil)))))
!(R2 (let $program (tptp:open-resolution-input-view)
   (raw-run $program (LangDef:DeterministicCall tptp-res-v1:expanded-items
       (tptp-ci-v1:expanded-inputs-cons
          (TPTP:ExpandedInputV1 (TPTP:SourceOccurrenceV1 p abc zero S) (TPTP:IncludePathRootV1 p) N Q R)
          (tptp-ci-v1:expanded-inputs-nil))))))
!(R3 (let $program (tptp:open-resolution-input-view) (raw-run $program (LangDef:DeterministicCall tptp-res-v1:occurrence-at abc zero (TPTP:IncludePathRootV1 p)))))

; A produced constructor retains its pending field across an Atom result.
; A native value reader discharges that field in its branch, preserving its
; shared choice and effect. Quoted and explicitly held syntax stays inert.
!(import! &self json)
(: demand:number (-> %Undefined% Proc))
(= (demand:number $digits) (JsonNumberV1 $digits))
(: demand:shared-array (-> %Undefined% Proc))
(= (demand:shared-array $digits)
   (JsonArrayV1 ((JsonNumberV1 $digits) (JsonNumberV1 $digits))))
(: demand:independent-array (-> %Undefined% %Undefined% Proc))
(= (demand:independent-array $left $right)
   (JsonArrayV1 ((JsonNumberV1 $left) (JsonNumberV1 $right))))
(= (demand:digits) "7")
(= (demand:noisy-digits) (let $_ (println! structural-field-forced) "7"))
(= (demand:choice) (superpose ("1" "1" "2")))
(= (demand:independent-choice) (superpose ("1" "2")))
(= (demand:twice $value) (Pair (json:stringify $value) (json:stringify $value)))
(= (demand:shadow $value)
   (let $digits "9" (demand:twice $value)))
(= (demand:malformed $value)
   (case (json:stringify $value)
     (((JsonFailureV1 JsonStringifyV1 JsonMalformedValueV1 $source $detail) malformed)
      ($unexpected (unexpected $unexpected)))))
(: demand:hold (-> Atom Atom))
(= (demand:hold $syntax) $syntax)
(= (demand:quoted-number $digits) (JsonNumberV1 (quote $digits)))
!(json:stringify (demand:number (demand:digits)))
!(demand:twice (demand:number (demand:noisy-digits)))
; A later binder with the same spelling cannot change the field's captures.
!(let $digits (demand:digits) (demand:shadow (demand:number $digits)))
!(json:stringify (demand:shared-array (demand:choice)))
!(json:stringify (demand:independent-array (demand:independent-choice) (demand:independent-choice)))
!(demand:malformed (demand:hold (JsonNumberV1 (demand:noisy-digits))))
!(demand:malformed (demand:quoted-number (demand:noisy-digits)))
; Discharging an implicit field does not activate an explicit suspension.
!(demand:malformed (demand:number (delay (demand:noisy-digits))))
