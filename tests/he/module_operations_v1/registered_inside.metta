!(register-module! "registered_leaf.metta")
!(register-module! "registered_provider.py")
!(case (mod-space! top:registered_leaf) (((Error $a $b) (Error registered-root $b)) ($ok ())))
!(case (mod-space! top:registered_provider) (((Error $a $b) (Error registered-root $b)) ($ok ())))
