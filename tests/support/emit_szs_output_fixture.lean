import Mettapedia.Languages.TPTP.SZSOutput
import Mettapedia.GSLT.LanguageDef.ModuleAlgebra.NativeAuthoring
import Mettapedia.GSLT.LanguageDef.ModuleFormat.Text

def main : IO Unit :=
  IO.println (Mettapedia.GSLT.LanguageDef.ModuleFormat.Text.render
    (Mettapedia.GSLT.LanguageDef.ModuleAlgebra.NativeAuthoring.Wire.language
      Mettapedia.Languages.TPTP.SZSOutput.language))
