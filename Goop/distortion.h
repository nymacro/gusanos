#ifndef DISTORTION_H
#define DISTORTION_H

#include "vec.h"
#include <string>

struct BITMAP;

struct DistortionMap
{
	std::vector<Vec> map;
	int width;
};

class Distortion
{
	public:

	Distortion(DistortionMap* map);
	~Distortion();
	
	void apply( BITMAP* where, int x, int y, float multiply );
	
	private:
	
	int width;
	int height;
	DistortionMap* m_map;
	BITMAP* buffer;
	
};

DistortionMap* lensMap(int radius);
DistortionMap* swirlMap(int radius);
DistortionMap* spinMap(int radius);
DistortionMap* rippleMap(int radius, int frequency = 3);
DistortionMap* randomMap(int radius);
DistortionMap* bitmapMap(const std::string &filename);

#endif // _DISTORTION_H_
