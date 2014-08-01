#import <stdio.h>
#import <assert.h>
#import <stdlib.h>
#define LUA_LIB
#include "lua.h"
#include "lauxlib.h"

char* sayHello(char* myName)
{
  	const char* prefix = "Why hello there, ";
	char* responseString;
  	assert(myName);
	fprintf(stdout, "Running C function...\n");
	responseString = (char*) malloc(sizeof(prefix) + sizeof(myName) + 2);
	sprintf(responseString, "%s%s\n", prefix, myName);
	return responseString;
}

int add(int a, int b){
	return a + b;
}

static int l_sayHello(lua_State *L)
{
	char* input = luaL_checkstring(L, 1);
	/* or lua_tostring(L, 1) */
	lua_pushstring(L, sayHello(input));
	return 1; /* number of results */
}

static int l_add(lua_State *L){
	int a = luaL_checknumber(L, 1);
	int b = luaL_checknumber(L,2);
	lua_pushnumber(L, a+b);
	return 1;
}


static const struct luaL_reg simplelib[] = {
{"sayhello", l_sayHello},
 {"add", l_add},
{NULL, NULL}
};

int luaopen_simplelib (lua_State *L){
	luaL_register(L, "simple", simplelib);
  	return 1;
}
