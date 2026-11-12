; The ordered-source contract is explicit: one owned Mailbox occurrence
; contains a finite ordered sequence. The surrounding space is still a bag.
!(import! &self interaction)

(= (ordered-mailbox:select $wanted $messages)
   (if (== (size-atom $messages) 0) None
     (let* (($head (car-atom $messages)) ($tail (cdr-atom $messages)))
       (case $head
         (((Message $tag $value)
           (if (== $wanted $tag)
             (function (return (Selected $value $tail)))
             (ordered-mailbox:skip $wanted $head $tail)))
          ($malformed None))))))

(= (ordered-mailbox:skip $wanted $head $tail)
   (case (ordered-mailbox:select $wanted $tail)
     (((Selected $answer $remaining)
       (let $rest (cons-atom $head $remaining)
         (function (return (Selected $answer $rest)))))
      (None None))))

(= (ordered-mailbox:receive $mailbox $wanted)
   (transform-prepared $mailbox
     (, (take (Mailbox $messages))
        (let (Selected $answer $rest) (ordered-mailbox:select $wanted $messages)))
     (, (add-atom $mailbox (Mailbox $rest)) $answer)))

!(bind! &mailbox (new-region space))
!(transform &mailbox () (add-atom &mailbox
   (Mailbox ((Message skipped 0) (Message target a) (Message target a) (Message last 9)))))
!(assertEqual (ordered-mailbox:receive &mailbox target) a)
!(assertEqual (collapse (transform &mailbox (read (Mailbox $messages)) $messages))
   (((Message skipped 0) (Message target a) (Message last 9))))
!(assertEqual (ordered-mailbox:receive &mailbox target) a)
!(assertEqual (collapse (transform &mailbox (read (Mailbox $messages)) $messages))
   (((Message skipped 0) (Message last 9))))
!(assertEqual (collapse (ordered-mailbox:receive &mailbox target)) ())
!(assertEqual (collapse (transform &mailbox (read (Mailbox $messages)) $messages))
   (((Message skipped 0) (Message last 9))))

; Same message bag, different sequence: selection and residual both differ.
!(bind! &left (new-region space))
!(bind! &right (new-region space))
!(transform &left () (add-atom &left (Mailbox ((Message target a) (Message target b)))))
!(transform &right () (add-atom &right (Mailbox ((Message target b) (Message target a)))))
!(assertEqual (ordered-mailbox:receive &left target) a)
!(assertEqual (ordered-mailbox:receive &right target) b)
!(assertEqual (collapse (transform &left (read (Mailbox $messages)) $messages))
   (((Message target b))))
!(assertEqual (collapse (transform &right (read (Mailbox $messages)) $messages))
   (((Message target a))))
!(OrderedMailbox passed)
