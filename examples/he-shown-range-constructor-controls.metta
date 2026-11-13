(: ControlBox (-> Number ControlRange))
(: good-body (-> ControlRange))
(= (good-body) (ControlBox 7))
(: bad-body (-> ControlRange))
(= (bad-body) (ControlBox "wrong-field"))

!(good-body)
!(bad-body)
