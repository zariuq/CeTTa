; Execute the unchanged MM2 program from an ordinary MeTTa session.

!(include mork)
!(bind! &hexlife (mork:new-space))
!(mork:include! &hexlife "hexlife_structural.mm2")
!(mork:step! &hexlife 1000)
!(collapse (mork:match &hexlife (alive $cell) (alive $cell)))
