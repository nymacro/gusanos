// Round-trip property fuzzer for ZCom_BitStream.
//
// Drives a sequence of typed writes (addInt/addBool/addString/addBuffer) from
// the fuzz input, serializes the stream, deserializes into a fresh stream, and
// reads the values back in the same order — aborting on any mismatch. This
// exercises the Serialize/Deserialize integrity invariant, which is orthogonal
// to fuzz_bitstream.cpp (which fuzzes deserialization of raw attacker bytes).
//
// Floats/doubles are intentionally excluded: addFloat/getFloat use a lossy
// fixed-point encoding for <32 mantissa bits, so exact round-trip is not
// guaranteed by design.
#include "net_bitstream.h"
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Cur {
	const uint8_t *p;
	size_t n;
	bool eof() const { return n == 0; }
	uint8_t u8() {
		if (n == 0)
			return 0;
		--n;
		return *p++;
	}
	zU32 u32le() {
		zU32 v = 0;
		for (int i = 0; i < 4; ++i)
			v |= zU32(u8()) << (8 * i);
		return v;
	}
};

enum Kind { K_INT, K_BOOL, K_STR, K_BUF };

struct Rec {
	Kind kind;
	zU32 u32 = 0;
	zU8 bits = 0;
	bool b = false;
	std::string s;
	std::vector<char> buf;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	Cur c{data, size};
	ZCom_BitStream out;
	std::vector<Rec> recs;

	while (!c.eof() && recs.size() < 48) {
		uint8_t cmd = c.u8() % 4;
		Rec r;
		r.kind = static_cast<Kind>(cmd);
		if (cmd == K_INT) {
			r.bits = static_cast<zU8>((c.u8() % 32) + 1); // 1..32
			r.u32 = c.u32le();
			// addInt writes only the low `bits` bits; mask so the recorded
			// value matches what round-trips.
			if (r.bits < 32)
				r.u32 &= (zU32(1) << r.bits) - 1;
			out.addInt(r.u32, r.bits);
		} else if (cmd == K_BOOL) {
			r.b = c.u8() & 1;
			out.addBool(r.b);
		} else if (cmd == K_STR) {
			uint8_t len = c.u8() % 24; // 0..23
			r.s.reserve(len);
			for (uint8_t i = 0; i < len; ++i) {
				uint8_t ch = c.u8();
				// addString uses strlen; avoid embedded NULs so the recorded
				// length matches the written length.
				if (ch == 0)
					ch = 'A';
				r.s.push_back(static_cast<char>(ch));
			}
			out.addString(r.s.c_str());
		} else { // K_BUF
			uint8_t len = c.u8() % 24;
			r.buf.resize(len);
			for (uint8_t i = 0; i < len; ++i)
				r.buf[i] = static_cast<char>(c.u8());
			out.addBuffer(r.buf.data(), static_cast<zU16>(len));
		}
		recs.push_back(std::move(r));
	}

	// Serialize -> Deserialize into a fresh stream, then read back in order.
	char sbuf[4096];
	zU16 sdone = 0;
	if (!out.Serialize(sbuf, &sdone, sizeof(sbuf)))
		return 0;
	ZCom_BitStream restored;
	if (!restored.Deserialize(sbuf, sdone))
		return 0;

	for (auto const &r : recs) {
		if (r.kind == K_INT) {
			if (restored.getInt(r.bits) != r.u32)
				abort();
		} else if (r.kind == K_BOOL) {
			if (restored.getBool() != r.b)
				abort();
		} else if (r.kind == K_STR) {
			// addString stores strlen+1 bytes (incl. null terminator);
			// getString strips that trailing null, returning the original text.
			if (restored.getString() != r.s)
				abort();
		} else { // K_BUF
			char tmp[64];
			zU16 got = restored.getBuffer(tmp, sizeof(tmp));
			if (got != r.buf.size())
				abort();
			if (got > 0 && std::memcmp(tmp, r.buf.data(), got) != 0)
				abort();
		}
	}
	return 0;
}
