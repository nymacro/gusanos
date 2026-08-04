#ifndef GUSANOS_LOADERS_EXTENSION_LOADER_H
#define GUSANOS_LOADERS_EXTENSION_LOADER_H

#include "../resource_locator.h"

#include <initializer_list>
#include <string>
#include <vector>

// Convenience base for resource loaders that match a fixed set of file
// extensions. canLoad() accepts when path's extension equals one of the
// configured extensions (case-sensitive) and sets name to the stem;
// getName() returns the configured display name. Subclasses only need to
// implement load(). The T / Cache / ReturnResource template parameters
// mirror ResourceLocator's, so an ExtensionLoader is-a BaseLoader for the
// matching ResourceLocator<T, Cache, ReturnResource>.
template <class T, bool Cache = true, bool ReturnResource = true>
struct ExtensionLoader : ResourceLocator<T, Cache, ReturnResource>::BaseLoader {
	ExtensionLoader(const char *name, std::initializer_list<const char *> exts)
		: m_name(name), m_exts(exts) {}

	bool canLoad(fs::path const &path, std::string &name) override {
		std::string ext = path.extension().string();
		for (const char *e : m_exts) {
			if (ext == e) {
				name = path.stem().string();
				return true;
			}
		}
		return false;
	}

	const char *getName() override { return m_name; }

private:
	const char *m_name;
	std::vector<const char *> m_exts;
};

#endif // GUSANOS_LOADERS_EXTENSION_LOADER_H
