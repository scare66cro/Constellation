  ;.compiler_opts --abi=eabi --arm_vmrs_si_workaround=off --code_state=16 --diag_wrap=off --embedded_constants=on --endian=little --float_support=FPv4SPD16 --hll_source=on --object_format=elf --silicon_version=7M4 --symdebug:none --unaligned_access=on
  .thumb

  .sect ".text"
  .clink
  .thumbfunc fault_isr
  .thumb
  .global fault_isr

fault_isr:
  tst lr, #4
  ite eq
  mrseq r0, msp
  mrsne r0, psp
  ldr r1, [r0, #24]
  b prvGetRegistersFromStack

  .global prvGetRegistersFromStack
