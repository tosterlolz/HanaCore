#pragma once
#include <stdint.h>

// C++ namespaced API
namespace hanacore { namespace arch { namespace pic {
	// Remap the legacy PIC to avoid conflicts with CPU exceptions (typically
	// remap to 0x20/0x28).
	void remap();

	// Send End-Of-Interrupt for the given IRQ number (0-15)
	void send_eoi(uint8_t irq);
}}}

// C ABI wrappers kept for existing call-sites (kernel_main, ISRs, etc.)
extern "C" void pic_remap();
extern "C" void pic_send_eoi(uint8_t irq);
