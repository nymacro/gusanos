#include "level.h"
#include "net_types.h"
#include "dedicated.h"

bool Level::encodeDestructionMaskRLE(ZCom_BitStream &out) const {
	if (!material || m_destructionMask.empty())
		return false;

	unsigned w = static_cast<unsigned>(material->w);
	unsigned h = static_cast<unsigned>(material->h);
	size_t totalPixels = size_t(w) * h;

	// Compose the RLE payload: 1 start-value bit, then alternating run lengths
	// encoded with Elias-delta. Run lengths are >= 1 (Elias-delta encodes n>=1).
	ZCom_BitStream rlePayload;
	bool startValue = (m_destructionMask[0] >> 0) & 1;
	rlePayload.addBool(startValue);

	bool curValue = startValue;
	size_t run = 0;
	for (size_t i = 0; i < totalPixels; ++i) {
		bool bit = (m_destructionMask[i >> 3] >> (i & 7)) & 1;
		if (bit == curValue) {
			++run;
		} else {
			Encoding::encodeEliasDelta(rlePayload, static_cast<unsigned int>(run));
			curValue = bit;
			run = 1;
		}
	}
	if (run > 0)
		Encoding::encodeEliasDelta(rlePayload, static_cast<unsigned int>(run));

	// Mode 0 = RLE, Mode 1 = raw (1 bit per pixel). Pick the smaller encoding.
	if (rlePayload.getBitCount() <= static_cast<zU32>(totalPixels)) {
		out.addBool(false); // RLE mode
		out.addBitStream(&rlePayload);
	} else {
		out.addBool(true); // raw mode
		for (size_t i = 0; i < totalPixels; ++i) {
			bool bit = (m_destructionMask[i >> 3] >> (i & 7)) & 1;
			out.addBool(bit);
		}
	}
	return true;
}

void Level::applyDestructionMaskRLE(ZCom_BitStream &in) {
	if (!material)
		return;

	unsigned w = static_cast<unsigned>(material->w);
	unsigned h = static_cast<unsigned>(material->h);
	size_t totalPixels = size_t(w) * h;
	if (totalPixels == 0)
		return;

	bool rleMode = !in.getBool(); // false bit == RLE mode
	if (rleMode) {
		bool value = in.getBool();
		size_t consumed = 0;
		while (consumed < totalPixels) {
			unsigned int run = Encoding::decodeEliasDelta(in);
			if (run == 0)
				break;
			for (unsigned int k = 0; k < run && consumed < totalPixels; ++k) {
				if (value) {
					unsigned x = static_cast<unsigned>(consumed % w);
					unsigned y = static_cast<unsigned>(consumed / w);
					putMaterial(1, x, y);
					checkWBorders(x, y);
					if (!g_dedicated)
						putpixel(image, x, y, getpixel(background, x, y));
					markDestroyed(x, y);
				}
				++consumed;
			}
			value = !value;
		}
	} else {
		for (size_t i = 0; i < totalPixels; ++i) {
			bool bit = in.getBool();
			if (bit) {
				unsigned x = static_cast<unsigned>(i % w);
				unsigned y = static_cast<unsigned>(i / w);
				putMaterial(1, x, y);
				checkWBorders(x, y);
				if (!g_dedicated)
					putpixel(image, x, y, getpixel(background, x, y));
				markDestroyed(x, y);
			}
		}
	}
}
