cetta_foreign_open_image(N, Image) :-
    integer(N), N >= 0,
    cetta_foreign_open_image_go(N, [X,X], Image).
cetta_foreign_open_image_go(0, Image, Image).
cetta_foreign_open_image_go(N, Inner, Image) :-
    N > 0, Previous is N - 1,
    cetta_foreign_open_image_go(Previous, [Inner], Image).
