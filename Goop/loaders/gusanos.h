#ifndef GUSANOS_LOADERS_GUSANOS_H
#define GUSANOS_LOADERS_GUSANOS_H

#include "extension_loader.h"
#include "../level.h"
#ifndef DEDSERV
#include "../font.h"
#include "../menu.h"
#endif
#include "../script.h"

struct GusanosLevelLoader : ResourceLocator<Level>::BaseLoader {
	virtual bool canLoad(fs::path const &path, std::string &name);

	virtual bool load(Level *, fs::path const &path);

	virtual const char *getName();

	static GusanosLevelLoader instance;
};

#ifndef DEDSERV
struct GusanosFontLoader : ExtensionLoader<Font> {
	GusanosFontLoader() : ExtensionLoader<Font>("Gusanos 0.9 font loader", {".bmp", ".png"}) {}
	bool load(Font *, fs::path const &path) override;
	static GusanosFontLoader instance;
};

struct XMLLoader : ExtensionLoader<XMLFile, false, false> {
	XMLLoader() : ExtensionLoader<XMLFile, false, false>("XML loader", {".xml"}) {}
	bool load(XMLFile *, fs::path const &path) override;
	static XMLLoader instance;
};

struct GSSLoader : ExtensionLoader<GSSFile> {
	GSSLoader() : ExtensionLoader<GSSFile>("GSS loader", {".gss"}) {}
	bool load(GSSFile *, fs::path const &path) override;
	static GSSLoader instance;
};
#endif

struct LuaLoader : ExtensionLoader<Script> {
	LuaLoader() : ExtensionLoader<Script>("Lua loader", {".lua"}) {}
	bool load(Script *, fs::path const &path) override;
	static LuaLoader instance;
};

#endif // GUSANOS_LOADERS_GUSANOS_H
