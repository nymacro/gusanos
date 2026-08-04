#ifndef GUSANOS_LOADERS_LIERO_H
#define GUSANOS_LOADERS_LIERO_H

#include "extension_loader.h"
#include "../level.h"
#ifndef DEDSERV
#include "../font.h"
#endif

struct LieroLevelLoader : ExtensionLoader<Level> {
	LieroLevelLoader() : ExtensionLoader<Level>("Liero level loader", {".lev"}) {}
	bool load(Level *, fs::path const &path) override;
	static LieroLevelLoader instance;
};

#ifndef DEDSERV

struct LieroFontLoader : ExtensionLoader<Font> {
	LieroFontLoader() : ExtensionLoader<Font>("Liero font loader", {".lft"}) {}
	bool load(Font *, fs::path const &path) override;
	static LieroFontLoader instance;
};

#endif

#endif // GUSANOS_LOADERS_LIERO_H
