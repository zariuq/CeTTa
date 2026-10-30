; Equal values from independent argument cells are distinct causes.
(= (independent-cause:goal heads) goal)
!(superpose ((independent-cause:goal (superpose (heads tails)))
             (independent-cause:goal (superpose (heads tails)))))
