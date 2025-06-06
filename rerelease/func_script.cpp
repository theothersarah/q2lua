#include <string>
#include <unordered_map>
#include <unordered_set>

#include "g_local.h"

#include <lua.hpp>

// =============================================================================
// String pool
// =============================================================================

static std::unordered_set<std::string> script_stringpool;

static const char* script_stringpool_add(const char* newstr)
{
	if (newstr == nullptr)
	{
		return "";
	}

	return script_stringpool.emplace(newstr).first->c_str();
}

// =============================================================================
// Entity functions
// =============================================================================

// Push a string, or an empty string if the original is null
static void script_push_string(lua_State* L, const char* str)
{
	if (str == nullptr)
	{
		lua_pushliteral(L, "");
	}
	else
	{
		lua_pushstring(L, str);
	}
}

// Valid entity keys for get and set operations
static const char* script_entity_keys[] =
{
	"classname",
	"targetname",
	"target",
	"killtarget",
	"pathtarget",
	"message",
	"delay",
	"script_arg",
	nullptr
};

// Get a value from the entity
static int script_entity_get(lua_State* L)
{
	edict_t* ent = *(edict_t**)luaL_checkudata(L, 1, "script_entity");
	int key = luaL_checkoption(L, 2, nullptr, script_entity_keys);

	switch (key)
	{
	case 0:
		script_push_string(L, ent->classname);
		break;

	case 1:
		script_push_string(L, ent->targetname);
		break;

	case 2:
		script_push_string(L, ent->target);
		break;

	case 3:
		script_push_string(L, ent->killtarget);
		break;

	case 4:
		script_push_string(L, ent->pathtarget);
		break;

	case 5:
		script_push_string(L, ent->message);
		break;

	case 6:
		lua_pushinteger(L, ent->delay);
		break;

	case 7:
		lua_pushinteger(L, ent->script_arg);
		break;
	}

	return 1;
}

// Set a value on the entity
static int script_entity_set(lua_State* L)
{
	edict_t* ent = *(edict_t**)luaL_checkudata(L, 1, "script_entity");
	int key = luaL_checkoption(L, 2, nullptr, script_entity_keys);

	switch (key)
	{
	case 2:
		ent->target = script_stringpool_add(luaL_checkstring(L, 3));
		break;

	case 3:
		ent->killtarget = script_stringpool_add(luaL_checkstring(L, 3));
		break;

	case 4:
		ent->pathtarget = script_stringpool_add(luaL_checkstring(L, 3));
		break;

	case 5:
		ent->message = script_stringpool_add(luaL_checkstring(L, 3));
		break;

	case 6:
		ent->delay = luaL_checknumber(L, 3);
		break;

	case 7:
		ent->script_arg = luaL_checkinteger(L, 3);
		break;

	default:
		return luaL_error(L, "attempt to set a read-only value");
	}

	return 0;
}

// Creates an entity object at the top of the stack
static void script_push_entity(lua_State* L, edict_t* ent)
{
	edict_t** userdata = (edict_t**)lua_newuserdata(L, sizeof(edict_t*));
	*userdata = ent;
	luaL_setmetatable(L, "script_entity");
}

// =============================================================================
// API functions
// =============================================================================

// Find all entities with a given targetname
static int script_find(lua_State* L)
{
	const char* targetname = luaL_checkstring(L, 1);

	lua_newtable(L);

	int n = 1;

	edict_t* ent = nullptr;

	while ((ent = G_FindByString<&edict_t::targetname>(ent, targetname)))
	{
		script_push_entity(L, ent);
		lua_seti(L, -2, n++);
	}

	return 1;
}

// Trigger a given entity, or all entities with a given targetname
static int script_trigger(lua_State* L)
{
	int type = lua_type(L, 1);

	if (type != LUA_TUSERDATA && type != LUA_TSTRING)
	{
		return luaL_argerror(L, 1, "type must be entity or string");
	}

	// Get reference to self and activator from the top of the trigger stack
	lua_getfield(L, LUA_REGISTRYINDEX, "script_triggerstack");
	int n = lua_rawlen(L, -1);
	lua_geti(L, -1, n);

	lua_getfield(L, -1, "self");
	edict_t* self = (edict_t*)lua_touserdata(L, -1);

	lua_getfield(L, -2, "activator");
	edict_t* activator = (edict_t*)lua_touserdata(L, -1);

	lua_pop(L, 4);

	// Trigger it
	if (type == LUA_TUSERDATA)
	{
		edict_t* ent = *(edict_t**)luaL_checkudata(L, 1, "script_entity");

		if (ent == self)
		{
			gi.Com_Print("func_script targeted itself\n");
		}
		else
		{
			if (ent->use)
			{
				ent->use(ent, self, activator);
			}
		}
	}
	else
	{
		const char* targetname = lua_tostring(L, 1);
		edict_t* ent = nullptr;

		while ((ent = G_FindByString<&edict_t::targetname>(ent, targetname)))
		{
			if (ent == self)
			{
				gi.Com_Print("func_script targeted itself\n");
			}
			else
			{
				if (ent->use)
				{
					ent->use(ent, self, activator);
				}
			}
		}
	}

	return 0;
}

// =============================================================================
// Global variables
// =============================================================================

// Storage for global variables
static std::unordered_map<std::string, int64_t> script_variables;

std::unordered_map<std::string, int64_t>* script_get_variables()
{
	return &script_variables;
}

// Get a script global variable, returning 0 if it is undefined
static int script_global_get(lua_State* L)
{
	const char* key = luaL_checkstring(L, 2);

	int64_t val;

	try
	{
		val = script_variables.at(key);
	}
	catch (const std::out_of_range& oor)
	{
		val = 0;
	}

	lua_pushinteger(L, val);

	return 1;
}

// Set a script global variable
static int script_global_set(lua_State* L)
{
	const char* key = luaL_checkstring(L, 2);
	int64_t val = luaL_checkinteger(L, 3);

	script_variables.insert_or_assign(key, val);

	return 0;
}

// =============================================================================
// 
// =============================================================================

// Metafunction __newindex: Allow only string/function pairs to be added
static int script_functionsonly(lua_State* L)
{
	luaL_checktype(L, 2, LUA_TSTRING);
	luaL_checktype(L, 3, LUA_TFUNCTION);
	lua_rawset(L, 1);

	return 0;
}

// Metafunction __newindex: Set an object's members to be read-only
static int script_readonly(lua_State* L)
{
	return luaL_error(L, "attempt to set a read-only value");
}

//
static lua_State* L = nullptr;

// Load and execute a script for a given map
void script_load(const char* mapname)
{
	// Reset script engine
	script_stringpool.clear();
	script_variables.clear();

	if (L != nullptr)
	{
		lua_close(L);
	}

	L = luaL_newstate();

	if (L == nullptr)
	{
		gi.Com_Print("Error initializing scripting engine\n");
		return;
	}

	// Add protection to global table to only allow functions with strings for names
	lua_geti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
	lua_newtable(L);
	lua_pushcfunction(L, script_functionsonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_pop(L, 1);

	// Load the script for the current map
	cvar_t* gamedir = gi.cvar("gamedir", "", CVAR_NOFLAGS);

	if (luaL_loadfile(L, G_Fmt("./{}/scripts/{}.lua", gamedir->string, mapname).data()) != LUA_OK)
	{
		const char* errstr = luaL_tolstring(L, -1, nullptr);
		gi.Com_PrintFmt("Error loading script for map {}: {}\n", mapname, errstr);
		lua_close(L);
		L = nullptr;
		return;
	}

	// Execute the script
	if (lua_pcall(L, 0, 0, 0) != LUA_OK)
	{
		const char* errstr = luaL_tolstring(L, -1, nullptr);
		gi.Com_PrintFmt("Error executing script for map {}: {}\n", mapname, errstr);
		lua_close(L);
		L = nullptr;
		return;
	}

	// Make sure there wasn't a function called script
	lua_getglobal(L, "script");

	if (lua_type(L, -1) != LUA_TNIL)
	{
		gi.Com_PrintFmt("User function named 'script' declared in script for map {}\n", mapname);
		lua_close(L);
		L = nullptr;
		return;
	}

	lua_pop(L, 1);

	// Remove protection on global table so the API can be added to it
	lua_geti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
	lua_newtable(L);
	lua_pushnil(L);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_pop(L, 1);

	// Table for API
	lua_newtable(L);

	// API functions
	lua_pushcfunction(L, script_find);
	lua_setfield(L, -2, "find");
	lua_pushcfunction(L, script_trigger);
	lua_setfield(L, -2, "trigger");

	// API table for globals
	lua_newtable(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_global_get);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_global_set);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "globals");

	// Metatable for API
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);

	// Add API to the global table
	lua_setglobal(L, "script");

	// Write protect locals
	lua_geti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_pop(L, 1);

	// Metatable for entity objects
	luaL_newmetatable(L, "script_entity");
	lua_pushcfunction(L, script_entity_get);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_entity_set);
	lua_setfield(L, -2, "__newindex");
	lua_pop(L, 1);

	// Stack for trigger context
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "script_triggerstack");

	gi.Com_PrintFmt("Loaded script for map {}\n", mapname);
}

USE(func_script_use) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	// Make sure the entity is set up correctly
	if (!self->script_function)
	{
		gi.Com_Print("func_script triggered but has no function\n");
		return;
	}

	// Make sure the scripting engine is actually active
	if (L == nullptr)
	{
		gi.Com_Print("func_script triggered but script not loaded\n");
		return;
	}

	// Try to get a function by the given name
	lua_getglobal(L, self->script_function);

	if (lua_type(L, -1) != LUA_TFUNCTION)
	{
		gi.Com_PrintFmt("func_script attempting to call nonexistent function {}\n", self->script_function);
		lua_pop(L, 1);
		return;
	}

	// Add trigger context to stack
	lua_getfield(L, LUA_REGISTRYINDEX, "script_triggerstack");
	int n = luaL_len(L, -1);

	lua_newtable(L);

	lua_pushlightuserdata(L, self);
	lua_setfield(L, -2, "self");

	lua_pushlightuserdata(L, other);
	lua_setfield(L, -2, "other");

	lua_pushlightuserdata(L, activator);
	lua_setfield(L, -2, "activator");

	lua_seti(L, -2, n + 1);
	lua_pop(L, 1);

	// Call the function
	script_push_entity(L, self);
	script_push_entity(L, other);
	script_push_entity(L, activator);

	if (lua_pcall(L, 3, 0, 0) != LUA_OK)
	{
		const char* errstr = luaL_tolstring(L, -1, nullptr);
		gi.Com_PrintFmt("Func_script error calling function {}: {}\n", self->script_function, errstr);
		lua_pop(L, 1);
	}

	// Remove top of trigger stack
	lua_getfield(L, LUA_REGISTRYINDEX, "script_triggerstack");
	lua_pushnil(L);
	lua_seti(L, -2, n + 1);
	lua_pop(L, 1);
}

void SP_func_script(edict_t* self)
{
	self->use = func_script_use;
}

// =============================================================================
// func_button_scripted
// =============================================================================

void button_scripted_touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);
void button_scripted_killed(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);

USE(button_scripted_use) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	if (!self->bmodel_anim.enabled)
	{
		if (level.is_n64)
		{
			self->s.frame = 0;
		}
		else
		{
			self->s.effects &= ~EF_ANIM23;
		}

		self->s.effects |= EF_ANIM01;
	}
	else
	{
		self->bmodel_anim.alternate = false;
	}

	if (self->health > 0)
	{
		self->die = button_scripted_killed;
		self->takedamage = true;
	}
	else
	{
		self->touch = button_scripted_touch;
	}

	self->use = nullptr;
}

static void button_scripted_fire(edict_t* self)
{
	if (!self->bmodel_anim.enabled)
	{
		self->s.effects &= ~EF_ANIM01;
		if (level.is_n64)
		{
			self->s.frame = 2;
		}
		else
		{
			self->s.effects |= EF_ANIM23;
		}
	}
	else
	{
		self->bmodel_anim.alternate = true;
	}

	// Using this button will now reset it
	self->use = button_scripted_use;

	G_UseTargets(self, self->activator);
}

TOUCH(button_scripted_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (!other->client)
	{
		return;
	}

	self->touch = nullptr;

	self->activator = other;
	button_scripted_fire(self);
}

DIE(button_scripted_killed) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	self->die = nullptr;

	self->activator = attacker;
	self->health = self->max_health;
	self->takedamage = false;
	button_scripted_fire(self);
}

void SP_func_button_scripted(edict_t* ent)
{
	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);

	if (!ent->bmodel_anim.enabled)
	{
		ent->s.effects |= EF_ANIM01;
	}

	if (ent->health > 0)
	{
		ent->max_health = ent->health;
		ent->die = button_scripted_killed;
		ent->takedamage = true;
	}
	else
	{
		ent->touch = button_scripted_touch;
	}

	gi.linkentity(ent);
}
