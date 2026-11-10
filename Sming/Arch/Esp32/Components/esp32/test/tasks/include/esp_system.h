#pragma once
#include <string>
#define DRAM_STR(value) value
struct SystemAbort {
	std::string details;
};
[[noreturn]] inline void esp_system_abort(const char* details)
{
	throw SystemAbort{details};
}
