; Regression: nested match-chain flattening must preserve joins when PathMap
; spaces are mixed with native spaces.

!(bind! &pm-left (new-space pathmap))
!(add-atom &pm-left (edge a b))
!(add-atom &pm-left (edge a c))
!(add-atom &pm-left (edge z b))

!(bind! &native-mid (new-space))
!(add-atom &native-mid (color b blue))
!(add-atom &native-mid (color c red))
!(add-atom &native-mid (color q green))

!(bind! &pm-right (new-space pathmap))
!(add-atom &pm-right (allowed blue yes))
!(add-atom &pm-right (allowed red no))

!(bind! &native-left (new-space))
!(add-atom &native-left (edge n1 b))
!(add-atom &native-left (edge n2 c))

!(bind! &pm-mid (new-space pathmap))
!(add-atom &pm-mid (color b blue))
!(add-atom &pm-mid (color c red))

!(bind! &native-right (new-space))
!(add-atom &native-right (allowed blue yes))
!(add-atom &native-right (allowed red no))

!(assertEqualToResult
  (collapse
    (match &pm-left (edge $from $mid)
      (match &native-mid (color $mid $color)
        (match &pm-right (allowed $color yes)
          (route pathmap-native-pathmap $from $mid $color)))))
  (((route pathmap-native-pathmap a b blue)
    (route pathmap-native-pathmap z b blue))))

!(assertEqualToResult
  (collapse
    (match &native-left (edge $from $mid)
      (match &pm-mid (color $mid $color)
        (match &native-right (allowed $color yes)
          (route native-pathmap-native $from $mid $color)))))
  (((route native-pathmap-native n1 b blue))))
