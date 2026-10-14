; Returning a constructed field must not call a host equation on that field.
(= (danger) (println! forbidden-inert-effect))
!(quote (SequenceData (danger) (Error ordinary payload)))
