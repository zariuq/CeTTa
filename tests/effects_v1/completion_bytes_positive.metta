!(import! &self system)
!(import! &self str)
!(system:write stdout (str:unescape metta "before\\x00after\\xff\\n"))
!(system:write stderr "diagnostic\n")
