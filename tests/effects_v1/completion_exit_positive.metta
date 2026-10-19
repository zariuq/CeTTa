!(import! &self system)
!(system:write stdout "published-before-exit\n")
!(system:exit 7)
!(system:write stdout "forbidden-after-exit\n")
