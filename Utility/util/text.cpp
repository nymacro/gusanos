#include "text.h"
#include <list>
#include <cctype>

using namespace std;

namespace {
typedef std::string::size_type size_type;

int minimum(size_type a, size_type b, size_type c) {
	size_type min = a;
	if (b < min)
		min = b;
	if (c < min)
		min = c;
	return min;
}
} // namespace

int levenshteinDistance(std::string const &a, std::string const &b) {
	if (a == b)
		return 0;

	size_type alen = a.size();
	size_type blen = b.size();

	if (alen && blen) {
		++alen;
		++blen;
		size_type *d = new size_type[alen * blen];

		for (size_type k = 0; k < alen; ++k)
			d[k] = k;

		for (size_type k = 0; k < blen; ++k)
			d[k * alen] = k;

		for (size_type i = 1; i < alen; ++i)
			for (size_type j = 1; j < blen; ++j) {
				size_type cost = (tolower(a[i - 1]) == tolower(b[j - 1])) ? 0 : 1;

				d[j * alen + i] =
					minimum(d[(j - 1) * alen + i] + 1, d[j * alen + i - 1] + 1, d[(j - 1) * alen + i - 1] + cost);
			}
		size_type distance = d[alen * blen - 1];
		delete[] d;
		return distance;
	} else
		return std::max(alen, blen);
}
