#include "g_local.h"

#include <lua.hpp>

// =============================================================================
// String pool
// =============================================================================

static std::unordered_set<std::string> script_stringpool;

static const char* script_stringpool_add(const char* newstr)
{
	return script_stringpool.emplace(newstr).first->c_str();
}

// =============================================================================
// Entity functions
// =============================================================================

// Valid entity keys for get and set operations
static const char* script_entity_keys[] =
{
	"classname",
	"team",
	"targetname",
	"target",
	"killtarget",
	"pathtarget",
	"script_arg",
	"message",
	"delay",
	"wait",
	"count",
	nullptr
};

// Push a string, or an empty string if the original is null
static void script_entity_get_string(lua_State* L, const char* str)
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

// Get a value from the entity
static int script_entity_get(lua_State* L)
{
	edict_t* ent = *(edict_t**)luaL_checkudata(L, 1, "script_entity");
	int key = luaL_checkoption(L, 2, nullptr, script_entity_keys);

	switch (key)
	{
	case 0:
		script_entity_get_string(L, ent->classname);
		break;

	case 1:
		script_entity_get_string(L, ent->team);
		break;

	case 2:
		script_entity_get_string(L, ent->targetname);
		break;

	case 3:
		script_entity_get_string(L, ent->target);
		break;

	case 4:
		script_entity_get_string(L, ent->killtarget);
		break;

	case 5:
		script_entity_get_string(L, ent->pathtarget);
		break;

	case 6:
		script_entity_get_string(L, ent->script_arg);
		break;

	case 7:
		script_entity_get_string(L, ent->message);
		break;

	case 8:
		lua_pushinteger(L, ent->delay);
		break;

	case 9:
		lua_pushinteger(L, ent->wait);
		break;

	case 10:
		lua_pushinteger(L, ent->count);
		break;

	default:
		return luaL_error(L, "if you see this, Sarah fucked up");
	}

	return 1;
}

// If the string is empty, return null instead
static const char* script_entity_set_string(const char* str)
{
	if (strlen(str) == 0)
	{
		return nullptr;
	}
	else
	{
		return script_stringpool_add(str);
	}
}

// Set a value on the entity
static int script_entity_set(lua_State* L)
{
	edict_t* ent = *(edict_t**)luaL_checkudata(L, 1, "script_entity");
	int key = luaL_checkoption(L, 2, nullptr, script_entity_keys);

	switch (key)
	{
	case 3:
		ent->target = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case 4:
		ent->killtarget = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case 5:
		ent->pathtarget = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case 6:
		ent->script_arg = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case 7:
		ent->message = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case 8:
		ent->delay = luaL_checknumber(L, 3);
		break;

	case 9:
		ent->wait = luaL_checknumber(L, 3);
		break;

	case 10:
		ent->count = luaL_checkinteger(L, 3);
		break;

	default:
		return luaL_error(L, "attempt to set a read-only value");
	}

	return 0;
}

// Triggers the entity
static int script_entity_trigger(lua_State* L)
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

	// Also pop the entity off the stack so the stack is empty during the trigger
	// in case this directly or indirectly triggers another func_script
	lua_pop(L, 5);

	// Try to prevent an infinite loop
	if (ent == self)
	{
		return luaL_error(L, "func_script targeted itself");
	}

	// Make sure we didn't killtarget it before
	if (!ent->inuse)
	{
		return luaL_error(L, "entity was killed at some point");
	}

	// Trigger it
	if (ent->use)
	{
		ent->use(ent, self, activator);
	}
	else
	{
		return luaL_error(L, "entity has no trigger function");
	}

	return 0;
}

int script_entity_setstring(lua_State* L)
{
	edict_t* ent = *(edict_t**)luaL_checkudata(L, 1, "script_entity");
	const char* str = luaL_checkstring(L, 2);

	// Make sure we didn't killtarget it before
	if (!ent->inuse)
	{
		return luaL_error(L, "entity was killed at some point");
	}

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

// Valid entity keys for find
static const char* script_find_keys[] =
{
	"classname",
	"team",
	"targetname",
	"target",
	"killtarget",
	"pathtarget",
	"script_arg",
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

	case 6:
		search_function = G_FindByString<&edict_t::script_arg>;
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

// =============================================================================
// Global variables
// =============================================================================

// <ake sure key is a string and prevent values of invalid types from being added to the global table
static int script_global_set(lua_State* L)
{
	luaL_checktype(L, 2, LUA_TSTRING);
	int type = lua_type(L, 3);

	if (type == LUA_TNIL || type == LUA_TNUMBER || type == LUA_TBOOLEAN || type == LUA_TSTRING)
	{
		lua_rawset(L, 1);
	}
	else
	{
		return luaL_argerror(L, 2, "invalid type: must number, boolean, or string");
	}

	return 0;
}

// =============================================================================
// Crosslevel variables
// =============================================================================

// Storage for crosslevel variables
// This has to be outside of the scripting engine so that it survives entering new levels
static std::unordered_map<std::string, std::string> script_crosslevel_variables;

// Get a crosslevel variable, returning nil if it hasn't been set
static int script_crosslevel_get(lua_State* L)
{
	const char* key = luaL_checkstring(L, 2);

	std::string str;

	try
	{
		str = script_crosslevel_variables.at(key);
	}
	catch (const std::out_of_range& oor)
	{
		lua_pushnil(L);
		return 1;
	}

	size_t loc = str.find_first_of(';');
	std::string str_type = str.substr(0, loc);
	std::string str_val = str.substr(loc + 1);

	if (str_type == "number")
	{
		lua_stringtonumber(L, str_val.c_str());
	}
	else if (str_type == "boolean")
	{
		lua_pushboolean(L, str_val == "true");
	}
	else
	{
		lua_pushstring(L, str_val.c_str());
	}

	return 1;
}

// Set a crosslevel variable, ensuring key is a string and the value is of a valid type
static int script_crosslevel_set(lua_State* L)
{
	const char* key = luaL_checkstring(L, 2);
	int type = lua_type(L, 3);

	if (type == LUA_TNIL)
	{
		script_crosslevel_variables.erase(key);
	}
	else if (type == LUA_TNUMBER || type == LUA_TBOOLEAN || type == LUA_TSTRING)
	{
		std::string val;

		val += lua_typename(L, type);
		val += ';';
		val += lua_tostring(L, 3);

		script_crosslevel_variables.insert_or_assign(key, val);
	}
	else
	{
		return luaL_argerror(L, 2, "invalid type: must number, boolean, or string");
	}

	return 0;
}

// =============================================================================
// Script loading
// =============================================================================

// Metafunction __newindex: Allow only string/function pairs to be added
// Also checks for restricted names
// Used during startup to ensure that only functions are added to the global space
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

// Lua state persists for the length of an entire map and is killed and recreated when a new map is loaded
static lua_State* L = nullptr;

// Load and execute a script for a given map
void script_load(const char* mapname)
{
	// Reset script engine
	script_stringpool.clear();

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
		const char* errstr = lua_tostring(L, -1);
		gi.Com_PrintFmt("Error loading script for map {}: {}\n", mapname, errstr);
		lua_close(L);
		L = nullptr;
		return;
	}

	// Execute the script
	if (lua_pcall(L, 0, 0, 0) != LUA_OK)
	{
		const char* errstr = lua_tostring(L, -1);
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

	// API table for globals
	lua_newtable(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_global_set);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "globals");

	// API table for crosslevel variables
	lua_newtable(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_crosslevel_get);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_crosslevel_set);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "crosslevel");

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
	lua_newtable(L);
	lua_pushcfunction(L, script_entity_get);
	lua_setfield(L, -2, "get");
	lua_pushcfunction(L, script_entity_set);
	lua_setfield(L, -2, "set");
	lua_pushcfunction(L, script_entity_trigger);
	lua_setfield(L, -2, "trigger");
	lua_pushcfunction(L, script_entity_setstring);
	lua_setfield(L, -2, "setstring");
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_pop(L, 1);

	// Stack for trigger context
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "script_triggerstack");

	gi.Com_PrintFmt("Loaded script for map {}\n", mapname);
}

// =============================================================================
// Save support
// =============================================================================

// Get global variables for saving
std::unordered_map<std::string, std::string> script_get_global_variables()
{
	std::unordered_map<std::string, std::string> globals;

	if (L == nullptr)
	{
		return globals;
	}

	// Get the global table
	lua_getglobal(L, "script");
	lua_getfield(L, -1, "globals");

	// Walk the table
	lua_pushnil(L);

	while (lua_next(L, -2) != 0)
	{
		// luaL_tolstring puts the converted string on the stack as a new object
		// There's no need to interact with this copy on the stack, but it stops
		// the key and value from getting mangled
		const char* key = luaL_tolstring(L, -2, nullptr);

		std::string val;

		val += lua_typename(L, lua_type(L, -2));
		val += ';';
		val += luaL_tolstring(L, -2, nullptr);

		globals.insert_or_assign(key, val);

		lua_pop(L, 3);
	}

	lua_pop(L, 2);

	return globals;
}

// Set global variables after loading
void script_set_global_variables(std::unordered_map<std::string, std::string> globals)
{
	if (L == nullptr)
	{
		return;
	}

	lua_getglobal(L, "script");
	lua_getfield(L, -1, "globals");

	for (auto it = globals.begin(); it != globals.end(); ++it)
	{
		std::string key = it->first;
		std::string str = it->second;
		size_t loc = str.find_first_of(';');
		std::string str_type = str.substr(0, loc);
		std::string str_val = str.substr(loc + 1);

		lua_pushstring(L, key.c_str());

		if (str_type == "number")
		{
			lua_stringtonumber(L, str_val.c_str());
		}
		else if (str_type == "boolean")
		{
			lua_pushboolean(L, str_val == "true");
		}
		else
		{
			lua_pushstring(L, str_val.c_str());
		}

		lua_rawset(L, -3);
	}

	lua_pop(L, 2);
}

// Get a pointer to the crosslevel variables for clearing, saving, and loading
std::unordered_map<std::string, std::string>* script_get_crosslevel_variables()
{
	return &script_crosslevel_variables;
}

// =============================================================================
// func_script entity
// =============================================================================

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
		const char* errstr = lua_tostring(L, -1);
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
// func_button_scripted entity
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
// trigger_enter_level entity
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
