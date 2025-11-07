#pragma once
#include <stdint.h>

// C++ namespaced API
namespace hanacore { namespace arch { namespace pit {
	// Initialize PIT channel 0 to `freq` Hz (simple, legacy PIT 8253/8254)
	void init(uint32_t freq);

	// C++ ISR handler called on each PIT tick
	void isr();
}}}

// Exposed C ABI wrappers for existing call-sites / IDT
extern "C" void pit_init(uint32_t freq);
extern "C" void pit_isr();
