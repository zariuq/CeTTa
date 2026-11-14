; The settings of a run of transform: which version of the transform step it uses.
; transform and tcalc are moving targets: a family of transform steps that differ in a few
; choices, and each run is one point of that family. The choices, their values and the dialects are
; declared once in lib/transform_axes.data; the rows come from lib/transform_axes.metta, generated from
; it (make transform-axes-metta, in the tree that holds native/tcalc/build.mk). This file names no
; choice: adding one is a row in that table.
;
; Read:   (step-setting AXIS)          the value this run requests for that choice
;         (step-setting dialect)       the dialect this run started from
; Write:  (step-setting! AXIS VALUE)   replaces it, for a value declared for that choice
;         (step-dialect! NAME)         starts again from a dialect's values
; Honour: (step-honours! AXIS)         a library that acts on a choice says so
; Record: (step-interpretation)        the base dialect, and per choice: value, base or override,
;                                      requested or honoured
;
; All the choices of a run are one list of (AXIS VALUE) pairs in one equation, visible to worker
; threads like every imported definition, and a write replaces that one equation. The replacement is
; a removal followed by an addition, not one atomic step: configure a run before it starts its workers.
; An axis, value or dialect that is not declared gets no equation, so a call that names one stays as it
; is (HE) or gives no answer (PeTTa), and changes nothing.

!(import! &self (library transform_axes))

; ---- the declaration, as the generated rows -------------------------------------------
(= (step-rows $table) (match &self (cesta-table-rows/1 $table $columns $rows) (cdr-atom $rows)))
(= (step-axis-value-table $axis) (let $list (step-rows Axes) (let (Row $axis $table) (superpose $list) $table)))
(= (step-declared-value $axis) (let $table (step-axis-value-table $axis) (let $list (step-rows $table) (let (Row $v) (superpose $list) $v))))
(= (step-preset-pairs $d) (let $list (step-rows Presets) (collapse (let (Row $d $axis $v) (superpose $list) ($axis $v)))))

; One read equation per declared axis, one setter equation per declared value, one dialect equation
; per declared dialect, and the petta dialect as this run's start.
!(collapse (let $axis (let $list (step-rows Axes) (let (Row $a $t) (superpose $list) $a))
   (add-atom &self (= (step-setting $axis) (step-read $axis)))))
!(collapse (let $axis (let $list (step-rows Axes) (let (Row $a $t) (superpose $list) $a))
   (let $v (step-declared-value $axis) (add-atom &self (= (step-setting! $axis $v) (step-set! $axis $v))))))
!(collapse (let $d (let $list (step-rows Dialects) (let (Row $x) (superpose $list) $x))
   (add-atom &self (= (step-dialect! $d) (step-apply-preset! $d)))))
!(let $pairs (step-preset-pairs petta) (add-atom &self (= (step-profile) (step-profile-is petta $pairs))))

; ---- reading ----------------------------------------------------------------------------
(= (step-setting dialect) (let (step-profile-is $d $pairs) (step-profile) $d))
(= (step-read $axis) (let (step-profile-is $d $pairs) (step-profile) (let ($axis $v) (superpose $pairs) $v)))

; ---- writing ----------------------------------------------------------------------------
(= (step-replace! $new)
   (let $old (step-profile)
     (let $removed (remove-atom &self (= (step-profile) $old))
       (add-atom &self (= (step-profile) $new)))))

(= (step-set! $axis $value)
   (let (step-profile-is $d $pairs) (step-profile)
     (let $new (collapse (let ($a $v) (superpose $pairs) (if (== $a $axis) ($a $value) ($a $v))))
       (let $done (step-replace! (step-profile-is $d $new))
         (step-setting-is $axis $value)))))

(= (step-apply-preset! $name)
   (let $pairs (step-preset-pairs $name)
     (let $done (step-replace! (step-profile-is $name $pairs))
       (step-dialect-is $name))))

; ---- honouring --------------------------------------------------------------------------
(= (step-honours! $axis)
   (if (== (collapse (match &self (step-honoured $axis) yes)) ())
       (let $added (add-atom &self (step-honoured $axis)) (step-honours-is $axis))
       (step-honours-is $axis)))

; ---- the record -------------------------------------------------------------------------
(= (step-choice-record $base $presets ($axis $value))
   (let* (($preset (collapse (let ($axis $p) (superpose $presets) $p)))
          ($source (if (== $preset ($value)) base override))
          ($acted (if (== (collapse (match &self (step-honoured $axis) yes)) ()) requested honoured)))
     ($axis $value $source $acted)))

(= (step-interpretation)
   (let (step-profile-is $base $pairs) (step-profile)
     (let $presets (step-preset-pairs $base)
       (let $choices (collapse (let $pair (superpose $pairs) (step-choice-record $base $presets $pair)))
         (interpretation (dialect $base) (choices $choices))))))
