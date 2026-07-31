#include "net_bitstream.h"
#include <cstdint>
#include <cstddef>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size == 0)
		return 0;
	uint8_t sel = data[0];
	const uint8_t *p = data + 1;
	size_t rem = size - 1;

	ZCom_BitStream bs(data, size); // construct from uncontrolled bytes
	bs.getInt(static_cast<zU8>(sel % 33));
	bs.getSignedInt(static_cast<zU8>((sel >> 1) % 33));
	bs.getBool();
	bs.getInt64(static_cast<int>(sel % 65));
	(void)bs.getString();
	(void)bs.getStringStatic();
	{
		char buf[256];
		uint16_t len = static_cast<uint16_t>(sel);
		if (len > sizeof(buf))
			len = sizeof(buf);
		bs.getBuffer(buf, len);
	}
	{
		wchar_t wbuf[64];
		bs.getStringW(wbuf, 64);
	}
	(void)bs.getStringWStatic();
	bs.getFloat(static_cast<zU8>((sel % 32) + 1));
	bs.getDouble(static_cast<int>((sel % 64) + 1));
	bs.skipBits(sel);
	ZCom_BitStream::BitPos rp;
	bs.saveReadState(rp);
	bs.skipString();
	bs.restoreReadState(rp);
	auto dup = bs.Duplicate();

	// Serialize/Deserialize round-trip on a fresh stream built from the input.
	ZCom_BitStream rt;
	rt.addBuffer(reinterpret_cast<const char *>(p), static_cast<zU16>(rem > 65535 ? 65535 : rem));
	char sbuf[512];
	zU16 sdone = 0;
	if (rt.Serialize(sbuf, &sdone, sizeof(sbuf))) {
		ZCom_BitStream restored;
		restored.Deserialize(sbuf, sdone);
	}
	return 0;
}
