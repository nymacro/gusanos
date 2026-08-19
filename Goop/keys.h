#ifndef keys_h
#define keys_h

#include <string>
#include <vector>
#include <boost/array.hpp>

int kName2Int(const std::string &name);

extern boost::array<std::string, 128> keyNames;

#endif // keys_h
