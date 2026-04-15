!(import! &self mork)

!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (nest (pair a (pair b c)) (text "x y")))
!(mork:add-atom &ws (FList (FSDepth 0) (Cons ("formula" "x") Nil)))
!(mork:add-atom &ws "solo")
!(assertEqualToResult
  (mork:size &ws)
  (4))
!(assertEqualToResult
  (mork:match &ws (edge a b) hit)
  (hit))
!(assertEqualToResult
  (mork:match &ws (nest (pair a (pair b c)) (text "x y")) hit)
  (hit))
!(assertEqualToResult
  (mork:match &ws (FList (FSDepth 0) (Cons ("formula" "x") Nil)) hit)
  (hit))
!(mork:remove-atom &ws (nest (pair a (pair b c)) (text "x y")))
!(assertEqualToResult
  (mork:size &ws)
  (3))
!(mork:add-atom &ws (nest (pair a (pair b c)) (text "x y")))
!(assertEqual
  (collapse (mork:get-atoms &ws))
  ((edge a b)
   (nest (pair a (pair b c)) (text "x y"))
   (FList (FSDepth 0) (Cons ("formula" "x") Nil))
   "solo"))

!(bind! &copy (mork:new-space))
!(bind! &atoms (collapse (mork:get-atoms &ws)))
!(mork:add-atoms &copy &atoms)
!(assertEqualToResult
  (mork:size &copy)
  (4))
!(assertEqual
  (collapse (mork:get-atoms &copy))
  ((edge a b)
   (nest (pair a (pair b c)) (text "x y"))
   (FList (FSDepth 0) (Cons ("formula" "x") Nil))
   "solo"))

!(assertEqual
  (mork:dump! &ws "../runtime/test_mork_encoding_boundary_surface.act")
  ())
!(bind! &compiled (mork:open-act "../runtime/test_mork_encoding_boundary_surface.act"))
!(assertEqualToResult
  (mork:size &compiled)
  (4))
!(assertEqual
  (collapse (mork:get-atoms &compiled))
  ((edge a b)
   (nest (pair a (pair b c)) (text "x y"))
   (FList (FSDepth 0) (Cons ("formula" "x") Nil))
   "solo"))
!(assertEqualToResult
  (mork:match &compiled (edge a b) hit)
  (hit))
!(assertEqualToResult
  (mork:match &compiled (nest (pair a (pair b c)) (text "x y")) hit)
  (hit))
!(assertEqualToResult
  (mork:match &compiled (FList (FSDepth 0) (Cons ("formula" "x") Nil)) hit)
  (hit))
