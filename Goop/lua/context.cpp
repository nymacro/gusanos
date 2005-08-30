#include "context.h"

#define FREELIST_REF 0

LuaContext::LuaContext()
: m_FirstFreeRef(1)
{
	m_State = lua_open();
}

LuaContext::LuaContext(istream& stream)
: m_FirstFreeRef(1)
{
	m_State = lua_open();
	load(stream);
}

void LuaContext::reset()
{
	if(m_State)
		lua_close(m_State);
	m_State = lua_open();
	m_FirstFreeRef = 1;
}

const char * LuaContext::istreamChunkReader(lua_State *L, void *data, size_t *size)
{
	static char buffer[1024];
	
	istream& stream = *(istream *)data;
	
	stream.read(buffer, 1024);

	*size = (size_t)stream.gcount();
	
	if(*size > 0)
		return buffer;
	else
		return 0;
}

void LuaContext::load(istream& stream)
{
	int result = lua_load(m_State, istreamChunkReader, &stream, "chunk");
	
	if(result)
	{
		cerr << "Lua error: " << lua_tostring(m_State, -1) << endl;
		return;
	}
	
	//TODO: Create an error handler function that returns additional
	//debug info.
	result = lua_pcall (m_State, 0, 0, 0);
	
	switch(result)
	{
		case LUA_ERRRUN:
		case LUA_ERRMEM:
		case LUA_ERRERR:
		{
			/*strcpy(errMsgSpace, lua_tostring(m_State, -1));
			lua_pop(m_State, 1);*/
			//TODO: throw lua_exception(*this);
			cerr << "Lua error: " << lua_tostring(m_State, -1) << endl;
		}
		break;
	}

}

void LuaContext::load(istream& stream, string const& table)
{
	int result = lua_load(m_State, istreamChunkReader, &stream, "chunk");
	
	if(result)
	{
		cerr << "Lua error: " << lua_tostring(m_State, -1) << endl;
		return;
	}
	
	lua_pushstring(m_State, table.c_str());
	lua_rawget(m_State, LUA_GLOBALSINDEX);
	lua_setfenv(m_State, -2);
	
	//TODO: Create an error handler function that returns additional
	//debug info.
	result = lua_pcall (m_State, 0, 0, 0);
	
	switch(result)
	{
		case LUA_ERRRUN:
		case LUA_ERRMEM:
		case LUA_ERRERR:
		{
			/*strcpy(errMsgSpace, lua_tostring(m_State, -1));
			lua_pop(m_State, 1);*/
			//TODO: throw lua_exception(*this);
			cerr << "Lua error: " << lua_tostring(m_State, -1) << endl;
		}
		break;
	}

}

int LuaContext::call(int params, int returns)
{
	int result = lua_pcall (m_State, params, returns, 0);
		
	switch(result)
	{
		case LUA_ERRRUN:
		case LUA_ERRMEM:
		case LUA_ERRERR:
		{
			/*strcpy(errMsgSpace, lua_tostring(m_State, -1));
			lua_pop(m_State, 1);*/
				//TODO: throw lua_exception(*this);
				cerr << "Lua error: " << lua_tostring(m_State, -1) << endl;
				lua_settop(m_State, -1);
				return -1;
		}
		break;
	}
	
	return returns;
}

void LuaContext::push(lua_Number v)
{
	lua_pushnumber(m_State, v);
}

void LuaContext::push(const char* v)
{
	lua_pushstring(m_State, v);
}

void LuaContext::push(int v)
{
	pushReference(v);
}

int LuaContext::callReference(int ref)
{
	pushReference(ref);
	int result = call(0, 0);
	
	return result;
}

void LuaContext::function(char const* name, lua_CFunction func)
{
	lua_pushstring(m_State, name);
	lua_pushcfunction(m_State, func);
	lua_settable(m_State, LUA_GLOBALSINDEX);
}

int LuaContext::createReference()
{
	/*
	int ref = m_FirstFreeRef;
	//lua_pushvalue(m_State, idx);
	lua_rawseti(m_State, LUA_REGISTRYINDEX, ref);
	++m_FirstFreeRef;
	return ref;*/
	
	int ref;
	int t = LUA_REGISTRYINDEX;
	lua_rawgeti(m_State, t, FREELIST_REF);
	ref = (int)lua_tonumber(m_State, -1);
	lua_settop(m_State, -2);
	if(ref != 0)
	{
		cerr << "Assigning reused reference " << ref << endl;
		lua_rawgeti(m_State, t, ref);
		lua_rawseti(m_State, t, FREELIST_REF);
	}
	else
	{
		ref = m_FirstFreeRef++;
		cerr << "Assigning reference " << ref << endl;
	}
	
	lua_rawseti(m_State, t, ref);
	return ref;
}

void LuaContext::destroyReference(int ref)
{
	if (ref >= 0)
	{
    	int t = LUA_REGISTRYINDEX;
		lua_rawgeti(m_State, t, FREELIST_REF);
		lua_rawseti(m_State, t, ref);  /* t[ref] = t[FREELIST_REF] */
		lua_pushnumber(m_State, (lua_Number)ref);
		lua_rawseti(m_State, t, FREELIST_REF);  /* t[FREELIST_REF] = ref */
	}
}

void LuaContext::pushReference(int ref)
{
	lua_rawgeti(m_State, LUA_REGISTRYINDEX, ref);
}


LuaContext::~LuaContext()
{
	if(m_State)
		lua_close(m_State);
}