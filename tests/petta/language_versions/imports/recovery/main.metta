!(car-atom (catch (import! &self ./missing)))
!(car-atom (catch (import! &self ./broken)))
!(import! &self ./good)
!(collapse (recovery-value))
