	.set noreorder

#include "pspstub.s"

	STUB_START "kernel_functions",0x40090000,0x00010005
	STUB_FUNC  0xC2E1F88E,exitspawn_kernel
	STUB_END
