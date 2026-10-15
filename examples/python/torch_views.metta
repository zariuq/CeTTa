; Requires PyTorch in the Python interpreter selected by the build.
; Results are 15, 15, true; these operations use CPU tensors.
!(let $a ((py-atom torch.arange) 6)
   (let $sum ((py-dot $a sum))
     ((py-dot $sum item))))
!(let $a ((py-atom torch.arange) 6)
   (let $view ((py-dot $a reshape) 2 3)
     (let $sum ((py-dot $view sum))
       ((py-dot $sum item)))))
!(let $a ((py-atom torch.arange) 6)
   (let $view ((py-dot $a reshape) 2 3)
     ((py-atom operator.eq) ((py-dot $a data_ptr)) ((py-dot $view data_ptr)))))
