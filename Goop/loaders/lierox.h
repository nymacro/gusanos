#ifndef GUSANOS_LOADERS_LIEROX_H
#define GUSANOS_LOADERS_LIEROX_H

#include "extension_loader.h"
#include "../level.h"

struct LieroXLevelLoader : ExtensionLoader<Level> {
	LieroXLevelLoader() : ExtensionLoader<Level>("LieroX level loader", {".lxl"}) {}
	bool load(Level *, fs::path const &path) override;
	static LieroXLevelLoader instance;
};

#endif // GUSANOS_LOADERS_LIEROX_H
