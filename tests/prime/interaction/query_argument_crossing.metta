!(import! &self interaction)
!(bind! &records (new-space))
!(transform &records () (add-atom &records (Record present trusted)))

; A pattern owns its names unless the caller explicitly shares them.
(= (records:fresh-field $id)
   (collapse (transform &records (read (Record $id $value)) ($id $value))))
(= (records:caller-field $id)
   (collapse (transform-prepared &records (read (Record $id $value)) ($id $value)){$id}))

!(assertEqual (records:fresh-field missing) ((present trusted)))
!(assertEqual (records:caller-field missing) ())
!(assertEqual (records:caller-field present) ((present trusted)))
