(= (control-example:rows one) (Packet one 7))
(= (control-example:rows one) (Packet one 7))
(= (control-example:rows two) (Packet two 8))
!(let $value (control-example:rows $key)
  (case $value
    (((Packet $same $number) ($key $same $number)))))
