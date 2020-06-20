/*
 * =====================================================================================
 *
 *       Filename:  main.cpp
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  04/15/2020 10:19:21 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

extern "C"
{
	#include "lua/lua.h"
	#include "lua/lauxlib.h"
	#include "lua/lualib.h"
};

#include <iostream>
#include <functional>

class LuaTest final
{
public:
	int Foo(lua_State* L)
	{
		std::cout << "CLuaTest::Foo called." << std::endl;
		return 0;
	}
};


template <typename T>
void register_member_func(lua_State* L, const char* class_name, std::function<int(T&, lua_State*)>)
{

}

static lua_State* __lua_state;

int main(int argc, char* argv[])
{
	int result;

	__lua_state = luaL_newstate();
	if(!__lua_state)
		return -1;

	luaL_openlibs(__lua_state);


	register_member_func<LuaTest>(__lua_state, "LuaTest", &LuaTest::Foo);


	std::cout << "lua init successed." << std::endl;

	result = luaL_dofile(__lua_state, "lua/test.lua");
	if(result)
	{
		std::cout << "lua file load failed." << lua_tostring(__lua_state, -1) << std::endl;
		lua_close(__lua_state);
		return -1;
	}

	lua_getglobal(__lua_state, "script_add");
	lua_pushinteger(__lua_state, 10);
	lua_pushinteger(__lua_state, 20);
	lua_call(__lua_state, 2, 1);

	result = lua_tointeger(__lua_state, -1);
	lua_pop(__lua_state, 1);

	std::cout << "result : " << result << std::endl;

	lua_close(__lua_state);
	return 0;
}

