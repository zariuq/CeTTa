!(import! &self math)

!(bind! &mods (module-inventory!))

!(assertEqualToResult
  (match &mods
    (loaded-module "math" $path stdlib-file)
    ok)
  (ok))

!(get-doc math.exp)
!(get-doc math.min)
!(get-doc math.clamp)
