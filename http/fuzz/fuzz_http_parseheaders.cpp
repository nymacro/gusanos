#include "http.h"
#include <cstdint>
#include <cstddef>
#include <string>

namespace {
class FuzzRequest : public HTTP::Request {
  public:
	FuzzRequest() : HTTP::Request(-1, "", "") {}
	void fuzzParse(const uint8_t *data, size_t size) {
		const char *b = reinterpret_cast<const char *>(data);
		(void)this->parseHeaders(b, b + size);
	}
	void fuzzAddHeader(const std::string &h) {
		this->addHeader(h);
	}
};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size == 0)
		return 0;
	FuzzRequest req;
	req.fuzzParse(data, size);
	std::string line(reinterpret_cast<const char *>(data), size);
	req.fuzzAddHeader(line);
	return 0;
}
