#include <allegro.h>

#include "gconsole.h"
#include "resource_list.h"
#include "sprite.h"
#include "font.h"
#include "vec.h"
#include "level.h"
#include "game.h"
#include "viewport.h"
#include "part_type.h"
#include "particle.h"
#include "worm.h"
#include "player.h"
#include "text.h"
#include "gfx.h"
#include "sfx.h"
#include "distortion.h"
#include "ai.h"

#include <string>
#include <vector>

#include <fmod/fmod.h>

using namespace std;

bool quit = false;
int showFps;
int showDebug;

//millisecond timer
volatile unsigned int _timer = 0;
void _timerUpdate(void) { _timer++; } END_OF_FUNCTION(_timerUpdate);

string Exit(const list<string> &args)
{
	quit = true;
	return "";
}

int main(int argc, char **argv)
{
	game.init();

	//Font *tempFont = fontList.load("minifont.bmp");
	
	console.registerIntVariable("CL_SHOWFPS", &showFps, 1);
	console.registerIntVariable("CL_SHOWDEBUG", &showDebug, 1);
	
	console.registerCommand("QUIT", Exit);
	
	console.parseLine("BIND A +P0_LEFT; BIND D +P0_RIGHT; BIND G +P0_JUMP; BIND W +P0_UP; BIND S +P0_DOWN; BIND F +P0_FIRE; BIND H +P0_CHANGE");
	console.parseLine("BIND LEFT +P1_LEFT; BIND RIGHT +P1_RIGHT; BIND 2_PAD +P1_JUMP; BIND UP +P1_UP; BIND DOWN +P1_DOWN; BIND 1_PAD +P1_FIRE");
	console.parseLine("BIND F12 SCREENSHOT; BIND ESC QUIT");
	
	game.loadMod();
	
	//install millisecond timer
	install_timer();
	LOCK_VARIABLE(_timer);
	LOCK_FUNCTION(_timerUpdate);
	install_int_ex(_timerUpdate, BPS_TO_TIMER(100));

	int _fpsLast = 0;
	int _fpsCount = 0;
	int _fps = 0;
	int logicLast = 0;

	while (!quit)
	{

		while ( logicLast+1 <= _timer )
		{
			if ( game.isLoaded() && game.level.isLoaded() )
			{
				
				for ( list<BaseObject*>::iterator iter = game.objects.begin(); iter != game.objects.end(); iter++)
				{
					(*iter)->think();
				}
				
				for ( vector<BasePlayer*>::iterator iter = game.players.begin(); iter != game.players.end(); iter++)
				{
					(*iter)->think();
				}
				
				for ( list<BaseObject*>::iterator iter = game.objects.begin(); iter != game.objects.end(); )
				{
					if ( (*iter)->deleteMe )
					{
						list<BaseObject*>::iterator tmp = iter;
						iter++;
						delete *tmp;
						game.objects.erase(tmp);
					}else	iter++;
				}
				
			}
			sfx.think();
			
			console.checkInput();
			console.think();
			
			logicLast+=1;
		}

		//Update FPS
		if (_fpsLast + 100 <= _timer)
		{
			_fps = _fpsCount;
			_fpsCount = 0;
			_fpsLast = _timer;
		}
		
		if ( game.isLoaded() && game.level.isLoaded() )
		{
			for ( vector<BasePlayer*>::iterator iter = game.players.begin(); iter != game.players.end(); iter++)
			{
				(*iter)->render();
			}
			
			//debug info
			if (showDebug)
			{
				//tempFont->draw(gfx.buffer, "OBJECTS: " + cast<string>(game.objects.size()), 5, 10, 0);
				//tempFont->draw(gfx.buffer, "PLAYERS: " + cast<string>(game.players.size()), 5, 15, 0);
			}
		}
		
		//show fps
		if (showFps)
		{
			//tempFont->draw(gfx.buffer, "FPS: " + cast<string>(_fps), 5, 5, 0);
		}
		_fpsCount++;

		console.render(gfx.buffer);
		
		gfx.updateScreen();
		
	}
	
	
	game.unload();
	console.shutDown();
	sfx.shutDown();
	gfx.shutDown();
	allegro_exit();

	return(0);
}
END_OF_MAIN();

