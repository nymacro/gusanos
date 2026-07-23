#include "allegro_compat.h"

#include "gconsole.h"
#include "resource_list.h"
#include "sprite.h"

#include "level.h"
#include "game.h"
#include "updater.h"
#include "part_type.h"
#ifndef DEDSERV
#include "gamepad.h"
#endif
#include "particle.h"
#include "worm.h"
#include "player.h"
//#include "util/log.h"
#ifndef DEDSERV
#include "mouse.h"
#include "viewport.h"
#include "font.h"
#include "gfx.h"
#include "sfx.h"
#include "menu.h"
#include "distortion.h"
#include "keyboard.h"
#endif
#include "sprite_set.h"
#include "player_ai.h"
#include "network.h"


#include "script.h"
#include "glua.h"
#include "luaapi/context.h"
#include "util/log.h"
#include "http.h"
#include <memory>

#ifdef WINDOWS
	#include <winalleg.h>
#endif

#include <string>
#include <vector>

#ifdef POSIX
#include <unistd.h>
#include <execinfo.h>
#include <signal.h>
#include <cxxabi.h>
#include <cstdlib>
#include <sstream>
#include <cstdio>
#endif

using namespace std;

bool quit = false;
int showFps;
int showDebug;
int showGamepadInputs;

// Tear down game objects BEFORE the network control: BasePlayer::deleteThis
// does `delete m_node`, whose destructor calls m_control->removeNode(this).
// If we destroyed m_control first, that would be a use-after-free on the
// already-destructed unordered_set m_pendingRemove.
// Called on every exit path (normal return and both exception handlers) so
// loaded resources (e.g. WeaponType objects holding BaseAction*s) are freed.
void shutdown()
{
	game.unload();
	network.shutDown();
#ifndef DEDSERV
	OmfgGUI::menu.destroy();
#endif
	console.shutDown();
#ifndef DEDSERV
		sfx.shutDown();

		/* Shut down gamepad */
		gamepadHandler.shutDown();
#endif
	gfx.shutDown();
	lua.close();

	SDL_Quit();
}

//millisecond timer
volatile unsigned int timer = 0;
void timerUpdate(void) { timer++; } END_OF_FUNCTION(timerUpdate);

void exit()
{
	quit = true;
	network.disconnect();
}

string exitCmd(list<string> const& args)
{
	exit();
	return "";
}

// --- Crash handler: print backtrace on fatal signals ---
#ifdef POSIX
namespace {
    const int MAX_BT_FRAMES = 128;

    void crashHandler(int sig)
    {
        const char* sigName = "UNKNOWN";
        switch(sig) {
            case SIGSEGV: sigName = "SIGSEGV"; break;
            case SIGABRT: sigName = "SIGABRT"; break;
            case SIGFPE:  sigName = "SIGFPE"; break;
            case SIGILL:  sigName = "SIGILL"; break;
            case SIGBUS:  sigName = "SIGBUS"; break;
        }

        std::cerr << "\n*** CRASH: " << sigName << " (signal " << sig << ") ***\n";

        void* buffer[MAX_BT_FRAMES];
        int frames = backtrace(buffer, MAX_BT_FRAMES);

        char** symbols = backtrace_symbols(buffer, frames);
        if (symbols) {
            for (int i = 0; i < frames; ++i) {
                std::string frame(symbols[i]);
                size_t paren = frame.find('(');
                size_t plus = frame.find('+', paren);
                if (paren != std::string::npos && plus != std::string::npos) {
                    std::string mangled = frame.substr(paren + 1, plus - paren - 1);
                    int status;
                    char* dm = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
                    if (dm) {
                        frame = frame.substr(0, paren + 1) + dm + frame.substr(plus);
                        free(dm);
                    }
                }
                std::cerr << "  " << (i == 0 ? "=>" : "  ") << "  [" << i << "] " << frame << "\n";
            }
            free(symbols);
        }

        // Resolve source locations via addr2line
        std::ostringstream cmd;
        cmd << "addr2line -e /proc/self/exe -f -C -i";
        for (int i = 0; i < frames; ++i)
            cmd << " " << std::hex << buffer[i];
        cmd << " 2>/dev/null";

        FILE* pipe = popen(cmd.str().c_str(), "r");
        if (pipe) {
            std::cerr << "\n  Source locations:\n";
            char line[512];
            int idx = 0;
            while (fgets(line, sizeof(line), pipe)) {
                char* nl = line;
                while (*nl && *nl != '\n') ++nl;
                *nl = '\0';
                if (idx % 2 == 0)
                    std::cerr << "    " << (idx/2) << ": " << line << "\n";
                else
                    std::cerr << "       " << line << "\n";
                ++idx;
            }
            pclose(pipe);
        }

        std::cerr << "\n";

        signal(sig, SIG_DFL);
        raise(sig);
    }

    void sigintHandler(int)
    {
        quit = true;
    }

    struct CrashHandlerSetup {
        CrashHandlerSetup() {
            signal(SIGSEGV, crashHandler);
            signal(SIGABRT, crashHandler);
            signal(SIGFPE,  crashHandler);
            signal(SIGILL,  crashHandler);
            signal(SIGBUS,  crashHandler);
            signal(SIGINT,  sigintHandler);
        }
    } crashSetup;
}
#endif

int main(int argc, char **argv)
try
{
	console.registerVariables()
		("CL_SHOWFPS", &showFps, 1)
		("CL_SHOWDEBUG", &showDebug, 0)
		("CL_SHOWGAMEPADINPUTS", &showGamepadInputs, 0)
		("DBG_PARTICLE_WORLD_OVERLAY", &g_drawParticleWorldOverlay, 0)
	;
	
	game.init(argc, argv);

	console.registerCommands()
		("QUIT", exitCmd)
	;
	
	console.parseLine("BIND F12 SCREENSHOT");

#ifndef DEDSERV
	OmfgGUI::menu.clear();
#endif
	//game.loadMod();
	game.reloadModWithoutMap();
	//game.runInitScripts();
// SDL3 timing: replaced Allegro interrupt timer with SDL_GetTicks()
	const unsigned int LOGIC_DELTA = 10; // 10ms per logic tick = 100Hz
	unsigned int fpsLast = SDL_GetTicks();
	int fpsCount = 0;
	int fps = 0;
	unsigned int logicLast = SDL_GetTicks();
	
#ifndef DEDSERV
	console.executeConfig("autoexec.cfg");
#else
	console.executeConfig("autoexec-ded.cfg");
#endif

	//main game loop
	while (!quit || !network.isDisconnected())
	{

		while ( logicLast + LOGIC_DELTA <= SDL_GetTicks() )
		{
			
#ifdef USE_GRID
			for ( Grid::iterator iter = game.objects.beginAll(); iter;)
			{
				if(iter->deleteMe)
					iter.erase();
				else
					++iter;
			}
#else
			for ( ObjectsList::Iterator iter = game.objects.begin();  iter; )
			{
				if ( (*iter)->deleteMe )
				{
					ObjectsList::Iterator tmp = iter;
					++iter;
					delete *tmp;
					game.objects.erase(tmp);
				}
				else
					++iter;
			}
#endif
			
			if ( game.isLoaded() && game.level.isLoaded() )
			{
				
#ifdef USE_GRID
				
				for ( Grid::iterator iter = game.objects.beginAll(); iter; ++iter)
				{
					iter->think();
					game.objects.relocateIfNecessary(iter);
				}
				
				game.objects.flush(); // Insert all new objects
#else
				for ( ObjectsList::Iterator iter = game.objects.begin(); (bool)iter; ++iter)
				{
					(*iter)->think();
				}
#endif
				
				for ( list<BasePlayer*>::iterator iter = game.players.begin(); iter != game.players.end(); iter++)
				{
					(*iter)->think();
				}
			}
			
			game.think();
			updater.think(); // TODO: Move?
			
#ifndef DEDSERV
			sfx.think(); // WARNING: THIS �MUST! BE PLACED BEFORE THE OBJECT DELETE LOOP
#endif
			
			for (auto it = game.players.begin(); it != game.players.end(); )
			{
				BasePlayer* iter = *it;
				if ( iter->deleteMe )
				{
/* Done in deleteThis()
#ifdef USE_GRID
					for (Grid::iterator objIter = game.objects.beginAll(); objIter; ++objIter)
					{
						objIter->removeRefsToPlayer(iter);
					}
#else
					for ( ObjectsList::Iterator objIter = game.objects.begin(); (bool)objIter; ++objIter)
					{
						(*objIter)->removeRefsToPlayer(iter);
					}
#endif
*/
					if ( Player* player = dynamic_cast<Player*>(iter) )
					{
						for (auto lit = game.localPlayers.begin(); lit != game.localPlayers.end(); ++lit)
						{
							if ( player == *lit )
							{
								game.localPlayers.erase(lit);
								break;
							}
						}
					}
/*
					iter->removeWorm();
*/
					iter->deleteThis();
					it = game.players.erase(it);
				}
				else
				{
					++it;
				}
			}

			network.update();

#ifndef DEDSERV
		console.checkInput();
		mouseHandler.poll();
		/* Poll gamepad */
		gamepadHandler.poll();
#endif
			console.think();
			
			spriteList.think();
			
			EACH_CALLBACK(i, afterUpdate)
			{
				(lua.call(*i))();
			}
			
			logicLast += LOGIC_DELTA;
		}
		
#ifdef WINDOWS
		Sleep(0);
#else
#ifndef DEDSERV
		SDL_Delay(0);
#else
		SDL_Delay(2);
#endif
#endif

#ifndef DEDSERV
		//Update FPS
		if (fpsLast + 1000 <= SDL_GetTicks())
		{
			fps = fpsCount;
			fpsCount = 0;
			fpsLast = SDL_GetTicks();
			
			//console.addLogMsg(cast<string>(fps));
		}


		if ( game.isLoaded() && game.level.isLoaded() )
		{

			for ( list<BasePlayer*>::iterator iter = game.players.begin(); iter != game.players.end(); iter++)
			{
				(*iter)->render();
			}

			//debug info
			if (showDebug)
			{
				game.infoFont->draw(gfx.buffer, "OBJECTS: \01303" + cast<string>(game.objects.size()), 5, 10, 0, 255, 255, 255, 255, Font::Formatting);
				game.infoFont->draw(gfx.buffer, "PLAYERS: \01303" + cast<string>(game.players.size()), 5, 15, 0, 255, 255, 255, 255, Font::Formatting);
				game.infoFont->draw(gfx.buffer, "PING:    \01303" + cast<string>(network.getServerPing()), 5, 20, 0, 255, 255, 255, 255, Font::Formatting);
				game.infoFont->draw(gfx.buffer, "LUA MEM: \01303" + cast<string>(lua_gc(lua, LUA_GCCOUNT, 0)), 5, 25, 0, 255, 255, 255, 255, Font::Formatting);
			}
						
			int miny = 150;
			int maxw = 160;
			int y = 235;
			int w = 0;
			
			std::list<ScreenMessage>::reverse_iterator rmsgiter = game.messages.rbegin();
			
			for(;
		    rmsgiter != game.messages.rend() && y > miny;
		    ++rmsgiter)
			{
				ScreenMessage const& msg = *rmsgiter;
				
				string::const_iterator b = msg.str.begin(), e = msg.str.end(), n;
				
				do
				{
					pair<int, int> dim;
					n = game.infoFont->fitString(b, e, maxw, dim, 0, Font::Formatting);
					if(n == b)
						break;
					b = n;
					y -= dim.second;
					
					if(dim.first > w)
						w = dim.first;
				}
				while(b != e);
			}
			
			//rectfill_blend(gfx.buffer, 3, y-2, 3+w+5, 0, 130);
			
			for(std::list<ScreenMessage>::iterator msgiter = rmsgiter.base();
			    msgiter != game.messages.end();
			    ++msgiter)
			{
				ScreenMessage const& msg = *msgiter;
				
				string::const_iterator b = msg.str.begin(), e = msg.str.end(), n;
				
				int fact = 255;
				if(msg.timeOut < 100)
					fact = msg.timeOut * 255 / 100;
				
				Font::CharFormatting format;
				switch(msg.type)
				{
					case ScreenMessage::Chat:
						format.cur.color = Font::Color(255, 255, 255);
					break;
					
					case ScreenMessage::Death:
						format.cur.color = Font::Color(200, 255, 200);
					break;
				}
				
				do
				{
					pair<int, int> dim;
					n = game.infoFont->fitString(b, e, maxw, dim, 0, Font::Formatting);
					if(n == b)
						break;
					game.infoFont->draw(gfx.buffer, b, n, 5, y, format, 0, fact, Font::Formatting | Font::Shadow);
					y += dim.second;
					
					b = n;
				}
				while(b != e);
			}
		}
		else
		{
			clear_bitmap(gfx.buffer);
		}


		OmfgGUI::menu.render();
		console.render(gfx.buffer);

		// Input debug overlay (bottom-right)
		if (showGamepadInputs)
		{
			int y = gfx.buffer->h - 20;
			const int margin = 10;

			for (int slot = MAX_GAMEPADS - 1; slot >= 0; --slot)
			{
				if (!gamepadHandler.isConnected(slot))
					continue;

				std::string line = "GP" + cast<string>(slot) + ": ";

				// Trigger values
				float lt = gamepadHandler.getAxis(slot, 4);
				float rt = gamepadHandler.getAxis(slot, 5);
				char buf[64];
				std::snprintf(buf, sizeof(buf), "LT:%.2f RT:%.2f", lt, rt);
				line += buf;

				// Face buttons
				struct BtnDef { GamepadInput gp; const char* name; };
				static const BtnDef btns[] = {
					{ GP_A, "A" }, { GP_B, "B" }, { GP_X, "X" }, { GP_Y, "Y" },
					{ GP_LB, "LB" }, { GP_RB, "RB" },
					{ GP_START, "START" }, { GP_BACK, "BACK" },
				};
				for (const auto& b : btns)
				{
					if (gamepadHandler.isPressed(slot, b.gp))
						line += std::string(" [") + b.name + "]";
				}

				// D-pad
				struct DpadDef { GamepadInput gp; const char* sym; };
				static const DpadDef ddirs[] = {
					{ GP_DPAD_UP, "^" }, { GP_DPAD_DOWN, "v" },
					{ GP_DPAD_LEFT, "<" }, { GP_DPAD_RIGHT, ">" },
				};
				for (const auto& d : ddirs)
				{
					if (gamepadHandler.isPressed(slot, d.gp))
						line += std::string(" ") + d.sym;
				}

				// Stick virtual directions
				static const BtnDef stickDirs[] = {
					{ GP_LSTICK_LEFT, "L<" }, { GP_LSTICK_RIGHT, "L>" },
					{ GP_LSTICK_UP, "L^" }, { GP_LSTICK_DOWN, "Lv" },
					{ GP_RSTICK_LEFT, "R<" }, { GP_RSTICK_RIGHT, "R>" },
					{ GP_RSTICK_UP, "R^" }, { GP_RSTICK_DOWN, "Rv" },
				};
				for (const auto& s : stickDirs)
				{
					if (gamepadHandler.isPressed(slot, s.gp))
						line += std::string(" ") + s.name;
				}

				// Right-align the line
				pair<int, int> dim;
				game.infoFont->fitString(line.begin(), line.end(),
				                         gfx.buffer->w - margin, dim, 0,
				                         Font::Formatting);
				int x = gfx.buffer->w - dim.first - margin;
				game.infoFont->draw(gfx.buffer, line, x, y, 0, 255, 255, 255, 255,
				                    Font::Formatting);
				y -= dim.second;
			}
		}

		//show fps (on top of menu)
		if (showFps)
		{
			game.infoFont->draw(gfx.buffer, "FPS: \01303" + cast<string>(fps), 5, 5, 0, 255, 255, 255, 255, Font::Formatting);
		}
		fpsCount++;

		if(quit)
			game.infoFont->draw(gfx.buffer, "Quitting...", 15, 110, 0, 255, 255, 255, 255);

	EACH_CALLBACK(i, afterRender)
	{
		//lua.callReference(*i);
		(lua.call(*i))();
	}

	// Draw the in-game cursor on top of everything. The OS cursor is hidden
	// while inside the window, so this is the cursor the user actually sees.
	if (gfx.cursorSpriteSet)
	{
		Sprite* sprite = gfx.cursorSpriteSet->getSprite(gfx.cursorFrame);
		sprite->draw(gfx.buffer, mouseHandler.getX(), mouseHandler.getY());
	}

	gfx.updateScreen();
#endif
	}
	
	shutdown();
	return(0);
}
catch(std::exception& e)
{
	std::cerr << "Unhandled exception: " << e.what() << '\n';
	shutdown();
}
catch(...)
{
	std::cerr << "Unknown unhandled exception\n";
	shutdown();
}
