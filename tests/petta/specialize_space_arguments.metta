;; SWI-PeTTa's translator leaves the atom given to add-atom or remove-atom
;; untranslated: it is data.  A literal callable argument specializes the
;; enclosing function, and the stored or removed atom must still be the atom
;; written, not a run-time dispatch or a call to the derived relation.

(= (setting) (empty))
(= (configure $name $default)
   (progn (remove-atom &self (= ($name) (empty)))
          (add-atom &self (= ($name) $default))))
!(configure setting 1)
!(setting)
!(collapse (match &self (= (setting) $body) $body))

;; The same shape behind a binding, as a configuration reader writes it.
(= (limit) (empty))
(= (reconfigure $name $default)
   (let $value ""
        (progn (remove-atom &self (= ($name) (empty)))
               (if (== $value "")
                   (add-atom &self (= ($name) $default))
                   (empty)))))
!(reconfigure limit 2)
!(limit)

;; A recursive call written inside a stored atom stays a call to the source
;; function.
(= (first) 0)
(= (note $name) (add-atom &self (noted ($name) (note $name))))
!(note first)
!(match &self (noted $head $call) ($head $call))

;; A match pattern is data too: it is matched as written.
(entry (lookup first))
(= (lookup $name) (match &self (entry (lookup $name)) found))
!(lookup first)
