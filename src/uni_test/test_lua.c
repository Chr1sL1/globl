#include "lua/lauxlib.h"
#include "lua/lualib.h"

static lua_State* __lua_state;

int test_lua(void)
{
	int result;

	__lua_state = luaL_newstate();
	if(!__lua_state)
		return -1;

	luaL_openlibs(__lua_state);

	printf("lua init successed\n");

	result = luaL_dofile(__lua_state, "lua/test.lua");
	if(result)
	{
		printf("lua file load failed.\n");
		lua_close(__lua_state);
		return -1;
	}

	lua_getglobal(__lua_state, "script_add");
	lua_pushinteger(__lua_state, 10);
	lua_pushinteger(__lua_state, 20);
	lua_call(__lua_state, 2, 1);

	result = lua_tointeger(__lua_state, -1);
	lua_pop(__lua_state, 1);

	printf("result: %d\n", result);

	lua_close(__lua_state);
	return 0;
}

