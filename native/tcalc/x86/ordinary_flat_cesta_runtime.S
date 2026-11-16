.text
.globl flat_fire
.type flat_fire,@function
flat_fire:
  .incbin "ordinary_flat_cesta_linked.text", 0, 211
.size flat_fire, .-flat_fire
.globl ordinary_flat_pack
.type ordinary_flat_pack,@function
ordinary_flat_pack:
  .incbin "ordinary_flat_cesta_linked.text", 211, 530
.size ordinary_flat_pack, .-ordinary_flat_pack
.globl ordinary_flat_adapter
.type ordinary_flat_adapter,@function
ordinary_flat_adapter:
  .incbin "ordinary_flat_cesta_linked.text", 741, 704
.size ordinary_flat_adapter, .-ordinary_flat_adapter
.globl ordinary_flat_failure
.type ordinary_flat_failure,@function
ordinary_flat_failure:
  .incbin "ordinary_flat_cesta_linked.text", 1445, 2
.size ordinary_flat_failure, .-ordinary_flat_failure
.section .note.GNU-stack,"",@progbits
