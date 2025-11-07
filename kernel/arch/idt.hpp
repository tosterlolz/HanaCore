#pragma once
#include <stdint.h>

extern "C" void idt_install();
extern "C" void idt_set_handler(int vec, void (*handler)());

// C++ namespaced API
namespace hanacore { namespace arch { namespace idt {
	void install();
	void set_handler(int vec, void (*handler)());
}}}
