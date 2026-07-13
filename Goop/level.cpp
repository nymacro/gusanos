#include "level.h"

#ifndef DEDSERV
#include "gfx.h"
#include "blitters/context.h"
#include "viewport.h"
#endif
#include "material.h"
#include "base_player.h"
#include "sprite_set.h"
#include "sprite.h"
#include "util/vec.h"
#include "level_effect.h"
#include "culling.h"
#include "events.h"
#include "game.h"


#include "allegro_compat.h"
#include <string>
#include <vector>

using namespace std;

ResourceLocator<Level> levelLocator;

#ifndef DEDSERV
struct AddCuller : Culler<AddCuller>
{
	AddCuller( Level& level, BITMAP* dest, BITMAP* source, int alpha,int dOffx, int dOffy, int sOffx, int sOffy, Rect const& rect )
	:
		Culler<AddCuller>(rect),
		m_level(level),
		m_dest( dest ),
		m_source( source),
		m_destOffx(dOffx),
		m_destOffy(dOffy),
		m_sourceOffx(sOffx),
		m_sourceOffy(sOffy),
		m_alpha(alpha)
	{
	}
	
	bool block(int x, int y)
	{
		return m_level.unsafeGetMaterial(x,y).blocks_light;
	}
		
	void line(int y, int x1, int x2)
	{
		drawSpriteLine_add(
			m_dest,
			m_source,
			x1 - m_destOffx,
			y - m_destOffy,
			x1 - m_sourceOffx,
			y - m_sourceOffy,
			x2 - m_sourceOffx + 1,
			m_alpha
		);
	}

private:
	
	Level const &m_level;
	
	BITMAP* m_dest;
	BITMAP* m_source;
	
	int m_destOffx;
	int m_destOffy;
	int m_sourceOffx;
	int m_sourceOffy;

	int m_alpha;
	
};
#endif

Level::Level()
{
	loaded = false;
	m_firstFrame = true;
	m_config = 0;
	
#ifndef DEDSERV
	image = NULL;
	background = NULL;	
	paralax = NULL;
	lightmap = NULL;
#endif
	material = NULL;

	// Rock
	m_materialList[0].worm_pass = false;
	m_materialList[0].particle_pass = false;
	m_materialList[0].draw_exps = false;
	m_materialList[0].blocks_light = true;
	
	// Background
	m_materialList[1].worm_pass = true;
	m_materialList[1].particle_pass = true;
	m_materialList[1].draw_exps = true;
	
	// Dirt
	m_materialList[2].worm_pass = false;
	m_materialList[2].particle_pass = false;
	m_materialList[2].draw_exps = false;
	m_materialList[2].destroyable = true;
	m_materialList[2].blocks_light = true;
	m_materialList[2].flows = false;
	
	// Special dirt
	m_materialList[3].worm_pass = true;
	m_materialList[3].particle_pass = false;
	m_materialList[3].draw_exps = false;
	m_materialList[3].destroyable = true;
	
	// Special rock
	m_materialList[4].worm_pass = false;
	m_materialList[4].particle_pass = true;
	m_materialList[4].draw_exps = false;
	
	m_materialList[7].worm_pass = false;
	m_materialList[7].particle_pass = false;
	m_materialList[7].draw_exps = false;
	
	for ( size_t i = 0; i < m_materialList.size() ; ++i )
	{
		m_materialList[i].index = i;
		if ( m_materialList[i].flows && !m_materialList[i].is_stagnated_water && i < m_materialList.size()-1 )
		{
			m_materialList[i+1] = m_materialList[i];
			m_materialList[i+1].is_stagnated_water = true;
		}
	}
}

Level::~Level()
{
}

void Level::unload()
{
	loaded = false;
	path = "";
	m_firstFrame = true;
	
	delete m_config;
	m_config = 0;

#ifndef DEDSERV
	destroy_bitmap(image); image = NULL;
	destroy_bitmap(background); background = NULL;
	destroy_bitmap(paralax); paralax = NULL;
	destroy_bitmap(lightmap); lightmap = NULL;
	destroy_bitmap(watermap); watermap = NULL;
#endif
	destroy_bitmap(material); material = NULL;

	vectorEncoding = Encoding::VectorEncoding();
}

bool Level::isLoaded()
{
	return loaded;
}



void Level::checkWBorders( int x, int y )
{
	if ( getMaterial( x, y-1 ).is_stagnated_water )
	{
		unsigned char mat = getMaterialIndex(x, y-1) - 1;
		m_water.push_back( WaterParticle( x, y-1, mat ) );
		putMaterial( mat, x, y-1 );
	}
	if ( getMaterial( x+1, y ).is_stagnated_water )
	{
		unsigned char mat = getMaterialIndex(x+1, y) - 1;
		m_water.push_back( WaterParticle( x+1, y, mat ) );
		putMaterial( mat, x+1, y );
	}
	if ( getMaterial( x-1, y ).is_stagnated_water )
	{
		unsigned char mat = getMaterialIndex(x-1, y) - 1;
		m_water.push_back( WaterParticle( x-1, y, mat ) );
		putMaterial( mat, x-1, y );
	}
	
}


void Level::think()
{
	if(!isLoaded())
		return;
	
	if( m_firstFrame )
	{
		m_firstFrame = false;
		if ( m_config && m_config->gameStart )
			m_config->gameStart->run(0,0,0,0);
	}
#ifndef DEDSERV
		for (auto it = m_water.begin(); it != m_water.end(); )
		{
			if ( getMaterialIndex( it->x, it->y ) != it->mat )
			{
					putpixel_solid(image, it->x, it->y, getpixel(background, it->x, it->y) );
					it = m_water.erase(it);
			}else
			if ( rnd() > WaterSkipFactor )
			{
				unsigned char mat = getMaterialIndex( it->x, it->y+1 );
				if ( m_materialList[mat].particle_pass && !m_materialList[mat].flows)
				{
					checkWBorders( it->x, it->y  );
					putpixel_solid(image, it->x, it->y, getpixel(background, it->x, it->y) );
					putMaterial( 1, it->x, it->y );
					++it->y;
					putpixel_solid(image, it->x, it->y, getpixel(watermap, it->x, it->y) );
					putMaterial( it->mat, it->x, it->y );
					it->count = 0; // Reset stagnation counter because it moved
					++it;
				}else
				{
					char dir;
					if ( it->dir ) dir = 1;
					else dir = -1;
					
					mat = getMaterialIndex( it->x+dir, it->y );
					if ( m_materialList[mat].particle_pass && !m_materialList[mat].flows )
					{
						checkWBorders( it->x, it->y );
						putpixel_solid(image, it->x, it->y, getpixel(background, it->x, it->y) );
						putMaterial( 1, it->x, it->y );
						it->x += dir;
						putpixel_solid(image, it->x, it->y, getpixel(watermap, it->x, it->y) );
						putMaterial( it->mat, it->x, it->y );
						it->count = 0;
						// Reset stagnation counter because it moved
						++it;
					}
					else
					{
						it->dir = !it->dir;
						++it->count; // It didnt move so the stagnation counter gets incremented.
						if ( it->count > 1 )
						{
							mat = getMaterialIndex( it->x-dir, it->y );
							if ( !m_materialList[mat].particle_pass || m_materialList[mat].flows )
							{
								putMaterial( it->mat+1, it->x, it->y );
								putpixel_solid(image, it->x, it->y, getpixel(watermap, it->x, it->y) );
								it = m_water.erase(it);
							}
							else
							{
								++it;
							}
						}
						else
						{
							++it;
						}
					}
				}
			}
			else
			{
				++it;
			}
		}
#endif
}

#ifndef DEDSERV
void Level::draw(BITMAP* where, int x, int y)
{
	if (image)
	{
		if (!paralax)
		{
			blit(image,where,x,y,0,0,where->w,where->h);
		}
		else
		{
			int px = int(x * (paralax->w - where->w) / float( material->w - where->w ));
			int py = int(y * (paralax->h - where->h) / float( material->h - where->h ));
			blit(paralax,where,px,py,0,0,where->w,where->h);
			masked_blit(image,where,x,y,0,0,where->w,where->h);
		}

		if ( game.options.showMapDebug )
		{
			for (auto s : m_config->spawnPoints)
			{
				int c = (s.team == 0 ? makecol( 255,0,0 ) : makecol( 0, 255, 0 ));
				circle( where, s.pos.x - x, s.pos.y - y, 4, c );
			}
		}
	}
}


// TODO: optimize this
void Level::specialDrawSprite( Sprite* sprite, BITMAP* where, const IVec& pos, const IVec& matPos, BlitterContext const& blitter )
{
	// load_bitmap converts the magenta maskcolor (0xFFFF00FF) to transparent
	// black (0x00000000, alpha 0), so the mask is identified by alpha==0 rather
	// than the literal magenta value. Checking for 0xFFFF00FF here would never
	// match (the magenta no longer exists) and the alpha-0 mask pixels would be
	// drawn as black -- which is the explosion black-background bug.

	int xMatStart = matPos.x - sprite->m_xPivot;
	int yMatStart = matPos.y - sprite->m_yPivot;
	int xDrawStart = pos.x - sprite->m_xPivot;
	int yDrawStart = pos.y - sprite->m_yPivot;
	for ( int y = 0; y < sprite->m_bitmap->h ; ++y )
	for ( int x = 0; x < sprite->m_bitmap->w ; ++x )
	{
		if ( getMaterial ( xMatStart + x , yMatStart + y ).draw_exps )
		{
			//int c = sprite->m_bitmap->line[y][x];
			int c = getpixel( sprite->m_bitmap, x, y );
			if ( (c & 0xFF000000) != 0 ) blitter.putpixel( where, xDrawStart + x, yDrawStart + y, c );
		}
	}
}

void Level::culledDrawSprite( Sprite* sprite, Viewport* viewport, const IVec& pos, int alpha )
{
	BITMAP* renderBitmap = sprite->m_bitmap;
	IVec off = viewport->getPos();
	IVec loff(pos - IVec(sprite->m_xPivot, sprite->m_yPivot));
	
	Rect r(0, 0, width() - 1, height() - 1);
	r &= Rect(renderBitmap) + loff;
	

	if ( r.isIntersecting( Rect( viewport->dest ) + off ) ) // Check that it can be seen
	{
		AddCuller addCuller(
			*this,
			viewport->dest,
			renderBitmap,
			alpha,
			off.x,
			off.y,
			loff.x,
			loff.y,
			r );
	
		addCuller.cullOmni(pos.x, pos.y);
	}

}

void Level::culledDrawLight( Sprite* sprite, Viewport* viewport, const IVec& pos, int alpha )
{
	BITMAP* renderBitmap = sprite->m_bitmap;
	IVec off = viewport->getPos();
	IVec loff(pos - IVec(sprite->m_xPivot, sprite->m_yPivot));
	
	Rect r(0, 0, width() - 1, height() - 1);
	r &= Rect(renderBitmap) + loff;
	

	if ( r.isIntersecting( Rect( viewport->fadeBuffer ) + off ) ) // Check that it can be seen
	{
		AddCuller addCuller(
			*this,
			viewport->fadeBuffer,
			renderBitmap,
			alpha,
			off.x,
			off.y,
			loff.x,
			loff.y,
			r );
	
		addCuller.cullOmni(pos.x, pos.y);
	}
}

#endif

bool Level::applyEffect(LevelEffect* effect, int drawX, int drawY )
{
	bool returnValue = false;
	if ( effect && effect->mask )
	{
		Sprite* tmpMask = effect->mask->getSprite();
		drawX -= tmpMask->m_xPivot;
		drawY -= tmpMask->m_yPivot;
		unsigned int colour = 0;
		int maskDepth = tmpMask->m_bitmap->format;
		for( int y = 0; y < tmpMask->m_bitmap->h; ++y )
		for( int x = 0; x < tmpMask->m_bitmap->w; ++x )
		{
			colour = getpixel( tmpMask->m_bitmap, x, y);
			bool isBlack = (maskDepth == 32) ? ((colour & 0x00FFFFFF) == 0) : (colour == 0);
			if( isBlack && getMaterial( drawX+x, drawY+y ).destroyable )
			{
				returnValue = true;
				putMaterial( 1, drawX+x, drawY+y );
				checkWBorders( drawX+x, drawY+y );
#ifndef DEDSERV
				putpixel(image, drawX+x, drawY+y, getpixel( background, drawX+x, drawY+y ) );
#endif
			}
		}
	}
	return returnValue;
}

namespace
{
	bool canPlayerRespawn(BasePlayer* player, SpawnPoint const& point)
	{
		if(game.options.teamPlay && point.team != -1 && point.team != player->team)
			return false;
		return true;
	}
}

Vec Level::getSpawnLocation(BasePlayer* player)
{
	if(m_config)
	{
		int alt = 0;
		for (auto i : m_config->spawnPoints)
		{
			if(canPlayerRespawn(player, i))
				++alt;
		}
		
		if(alt > 0)
		{
			int idx = rndInt(alt);
			for (auto i : m_config->spawnPoints)
			{
				if(canPlayerRespawn(player, i) && --idx < 0)
				{
					return i.pos;
				}
			}
		}
	}
	
	Vec pos;
		
	//pos = Vec(rnd() * material->w, rnd()*material->h);
	
	do
	{
		pos = Vec(rnd() * material->w, rnd()*material->h);
	} while ( !getMaterial( static_cast<int>(pos.x), static_cast<int>(pos.y) ).worm_pass );

	return pos;
}

/*
const Material& Level::getMaterial(int x, int y)
{
	return m_materialList[getpixel(material,x,y)+1];
}
*/

int Level::width()
{
	if ( material )
		return material->w;
	else
		return 0;
}

int Level::height()
{
	if ( material )
		return material->h;
	else
		return 0;
}

void Level::setName(const std::string& _name)
{
	name = _name;
}

const string& Level::getPath()
{
	return path;
}

const string& Level::getName()
{
	return name;
}

void Level::loaderSucceeded()
{
	loaded = true;
	m_water.clear();
	for ( int y = 0; y < material->h; ++y )
	for ( int x = 0; x < material->w; ++x )
	{
		if ( unsafeGetMaterial(x,y).flows && !unsafeGetMaterial(x,y).is_stagnated_water )
		{
			m_water.push_back( WaterParticle( x, y, getMaterialIndex(x,y) ) );
		}
		
		if ( unsafeGetMaterial(x,y).is_stagnated_water )
		{
			allegro_message( "Map is using a material that is reserved for internal use on water" );
			break;
		}
	}
	
#ifndef DEDSERV
	if ( !lightmap )
	{
		LocalSetColorDepth cd(8);
		lightmap = create_bitmap(material->w, material->h);
		clear_to_color(lightmap, 50);
		for ( int x = 0; x < lightmap->w ; ++x )
		for ( int y = 0; y < lightmap->h ; ++y )
		{
			if ( unsafeGetMaterial(x,y).blocks_light )
				putpixel( lightmap, x, y, 200 );
		}
	}
	
	if(!background)
	{
		background = create_bitmap(material->w, material->h);
		blit(image, background, 0,0,0,0,material->w, material->h);
		gfx.setBlender(ALPHA,120);
		rectfill( background, 0,0,background->w,background->h,0);
		solid_mode();
	}
	
	if ( !watermap )
	{
		watermap = create_bitmap( image->w, image->h );
		blit( background, watermap, 0,0,0,0,image->w, image->h );
		gfx.setBlender(ALPHA, 150);
		rectfill( watermap, 0,0,watermap->w, watermap->h, makecol( 0, 0, 200 ) );
		solid_mode();
	}
#endif
	// Make the domain one pixel larger than the level so that things like ninjarope hook
	// can get slightly outside the level and attach.
	vectorEncoding = Encoding::VectorEncoding(Rect(-1, -1, width() + 1, height() + 1), 2048);
	intVectorEncoding = Encoding::VectorEncoding(Rect(-1, -1, width() + 1, height() + 1), 1);
	diffVectorEncoding = Encoding::DiffVectorEncoding(1024);
	//cerr << "vectorEncoding: " << vectorEncoding.totalBits() << endl;
	
	if(!m_config)
		m_config = new LevelConfig(); // Default config
}
