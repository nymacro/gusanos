#include "http.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <list>
#include <utility>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	std::string s(reinterpret_cast<const char *>(data), size);
	(void)HTTP::Host::urlencode(s);
	std::list<std::pair<std::string, std::string>> vals;
	if (size >= 2) {
		vals.push_back({s, std::string(reinterpret_cast<const char *>(data), size / 2)});
	}
	(void)HTTP::Host::urlencode(vals);
	return 0;
}
