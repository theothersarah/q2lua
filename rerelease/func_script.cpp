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
	"team",
	"targetname",
	"target",
	"killtarget",
	"pathtarget",
	"message",
	"delay",
	"count",
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
		script_push_string(L, ent->team);
		break;

	case 2:
		script_push_string(L, ent->targetname);
		break;

	case 3:
		script_push_string(L, ent->target);
		break;

	case 4:
		script_push_string(L, ent->killtarget);
		break;

	case 5:
		script_push_string(L, ent->pathtarget);
		break;

	case 6:
		script_push_string(L, ent->message);
		break;

	case 7:
		lua_pushinteger(L, ent->delay);
		break;

	case 8:
		lua_pushinteger(L, ent->count);
		break;

	case 9:
		lua_pushinteger(L, ent->script_arg);
		break;

	default:
		return luaL_error(L, "if you see this, Sarah fucked up");
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
	case 3:
		ent->target = script_stringpool_add(luaL_checkstring(L, 3));
		break;

	case 4:
		ent->killtarget = script_stringpool_add(luaL_checkstring(L, 3));
		break;

	case 5:
		ent->pathtarget = script_stringpool_add(luaL_checkstring(L, 3));
		break;

	case 6:
		ent->message = script_stringpool_add(luaL_checkstring(L, 3));
		break;

	case 7:
		ent->delay = luaL_checknumber(L, 3);
		break;

	case 8:
		ent->count = luaL_checkinteger(L, 3);
		break;

	case 9:
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

// Valid entity keys for get and set operations
static const char* script_find_keys[] =
{
	"classname",
	"team",
	"targetname",
	"target",
	"killtarget",
	"pathtarget",
	nullptr
};

// Find all entities with a given value for a string key (defaulting to targetname)
static int script_find(lua_State* L)
{
	const char* value = luaL_checkstring(L, 1);
	int key = luaL_checkoption(L, 2, "targetname", script_find_keys);

	// Get a search function for the given key
	edict_t* (*search_function)(edict_t*, const std::string_view&) = nullptr;

	switch (key)
	{
	case 0:
		search_function = G_FindByString<&edict_t::classname>;
		break;

	case 1:
		search_function = G_FindByString<&edict_t::team>;
		break;

	case 2:
		search_function = G_FindByString<&edict_t::targetname>;
		break;

	case 3:
		search_function = G_FindByString<&edict_t::target>;
		break;

	case 4:
		search_function = G_FindByString<&edict_t::killtarget>;
		break;

	case 5:
		search_function = G_FindByString<&edict_t::pathtarget>;
		break;

	default:
		return luaL_error(L, "if you see this, Sarah fucked up");
	}

	// Table for results
	lua_newtable(L);

	int n = 1;

	edict_t* ent = nullptr;

	while ((ent = search_function(ent, value)))
	{
		script_push_entity(L, ent);
		lua_seti(L, -2, n++);
	}

	return 1;
}

// Triggers a given entity
static int script_trigger(lua_State* L)
{
	edict_t* ent = *(edict_t**)luaL_checkudata(L, 1, "script_entity");

	// Get reference to self and activator from the top of the trigger stack
	lua_getfield(L, LUA_REGISTRYINDEX, "script_triggerstack");
	int n = luaL_len(L, -1);
	lua_geti(L, -1, n);

	lua_getfield(L, -1, "self");
	edict_t* self = (edict_t*)lua_touserdata(L, -1);

	lua_getfield(L, -2, "activator");
	edict_t* activator = (edict_t*)lua_touserdata(L, -1);

	lua_pop(L, 4);

	// Trigger it
	if (ent == self)
	{
		return luaL_error(L, "func_script targeted itself");
	}
	else
	{
		if (ent->use)
		{
			ent->use(ent, self, activator);
		}
	}

	return 0;
}

int script_setstring(lua_State* L)
{
	edict_t* ent = *(edict_t**)luaL_checkudata(L, 1, "script_entity");
	const char* str = luaL_checkstring(L, 2);

	// Make sure it's actually a target_string
	if (Q_strcasecmp(ent->classname, "target_string"))
	{
		return luaL_error(L, "entity must be a target_string");
	}

	// Setting the string to an empty string afterward does two things:
	// First: it stops problems when str is inevitable garbage collected by Lua
	// Second: it adds the behavior that triggering the target_string again clears it
	ent->message = str;
	ent->use(ent, nullptr, nullptr);
	ent->message = "";

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
// Scripting core
// =============================================================================

// Metafunction __newindex: Allow only string/function pairs to be added
// Also checks for restricted names
// Used during startup to ensure that 
static int script_functionsonly(lua_State* L)
{
	luaL_checktype(L, 2, LUA_TSTRING);
	luaL_checktype(L, 3, LUA_TFUNCTION);

	lua_pushliteral(L, "script");

	if (lua_compare(L, 2, -1, LUA_OPEQ))
	{
		return luaL_argerror(L, 2, "name cannot be 'script'");
	}

	lua_pop(L, 1);

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
		gi.Com_Print("Error initializing scripting engine: probably out of memory\n");
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
	lua_pushcfunction(L, script_setstring);
	lua_setfield(L, -2, "setstring");

	// API table for globals
	lua_newtable(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_global_get);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_global_set);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "globals");

	// Standard libraries
	luaopen_math(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "math");

	luaopen_string(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "string");

	luaopen_table(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "table");

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

	gi.Com_PrintFmt("Loaded script for map {} {}\n", mapname, lua_gettop(L));
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

// =============================================================================
// trigger_enter_level
// =============================================================================

THINK(trigger_level_enter_think) (edict_t* self) -> void
{
	G_UseTargets(self, self);
}

void SP_trigger_enter_level(edict_t* self)
{
	if (!self->delay)
	{
		self->delay = 1;
	}

	self->think = trigger_level_enter_think;
	self->nextthink = level.time + gtime_t::from_sec(self->delay);
}
