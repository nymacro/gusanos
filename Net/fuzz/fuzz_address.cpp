// Fuzzer for ZCom_Address's offline (non-network) API surface.
//
// setIP/setPort/setType/setControlID and all getters, formatters, hashing,
// comparison, and copy/assign are exercised with fuzzer-driven octets/ports.
// The snprintf-based formatters (getAddressIP, toString) and the bounds/index
// paths (getIP(pos), computeHashKey with max==0) are the primary memory-safety
// targets.
//
// setAddress() is deliberately NOT fuzzed: it calls enet_address_set_host(),
// which performs *synchronous DNS resolution* on non-IP-literal hostnames.
// That would block and hit the network from inside the fuzzer. The parsing
// logic that splits "host:port" is trivial (strrchr + atoi) and lives behind
// that network call, so it is excluded here.
#include "net_address.h"
#include "net_types.h"
#include <cstdint>
#include <cstddef>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size < 8)
		return 0;

	ZCom_Address a;
	a.setIP(data[0], data[1], data[2], data[3]);
	a.setPort(static_cast<zU16>(data[4] | (uint16_t(data[5]) << 8)));
	a.setType((data[6] & 1) ? eZCom_AddressUDP : eZCom_AddressLocal);
	a.setControlID(data[7]);

	// Getters (getIP(pos) is bounds-checked internally for 0..3).
	(void)a.getIP();
	zU8 pos = size > 8 ? static_cast<zU8>(data[8] % 5) : 0; // incl. out-of-range 4
	(void)a.getIP(pos);
	(void)a.getPort();
	(void)a.getType();
	(void)a.getControlID();

	// String formatters using static buffers + snprintf (buffer-safety target).
	(void)a.getAddressIP(eZCom_AddressWithPort);
	(void)a.getAddressIP(eZCom_AddressWithoutPort);
	(void)a.toString();
	(void)a.getAddressHostname(); // nullptr when not set via setAddress()

	// Hash key; impl has an explicit max==0 edge case.
	zU32 hmax = 0;
	if (size > 12)
		hmax = static_cast<zU32>(data[9]) | (uint32_t(data[10]) << 8) | (uint32_t(data[11]) << 16) |
			   (uint32_t(data[12]) << 24);
	(void)a.computeHashKey(hmax);
	(void)a.computeHashKey(0);

	// Copy constructor, assignment, and equality (property: copy == original).
	ZCom_Address b(a);
	ZCom_Address c;
	c = a;
	if (!(a == b))
		abort();
	if (!(a == c))
		abort();

	// Mutate and re-compare (must not crash; equality may change either way).
	b.setPort(static_cast<zU16>(a.getPort() ^ 1));
	(void)(a == b);

	// Hostname-resolution stubs (synchronous impl; no network I/O).
	(void)a.resolveHostname(false, 0);
	(void)a.checkHostname();
	return 0;
}
