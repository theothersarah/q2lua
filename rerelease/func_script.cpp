#include "g_local.h"

#include <lua.hpp>

// =============================================================================
// String pool
// =============================================================================

// Strings from Lua won't exist outside of the function they are on the stack of,
// so they are copied into this string pool when they need to exist for longer.
// If a string with duplicate contents is added again, instead of adding a copy,
// it returns the original. The pool is cleared before a level's script is
// loaded.

static std::unordered_set<std::string> script_stringpool;

static const char* script_stringpool_add(const char* newstr)
{
	return script_stringpool.emplace(newstr).first->c_str();
}

// =============================================================================
// Entity functions
// =============================================================================

// Entities are represented by userdatas, and their member functions are added to
// a metatable named script_entity. Every function assumes the first argument will
// be an entity object, so they work as member functions with colon notation. When
// an entity is acquired, it checks the entity for validity by making sure the slot
// hasn't been freed since the entity object was created.

// Userdata structure for entity
// spawn_count is incremented for a given slot whenever the entity in that slot is freed,
// so this helps us make sure that the reference is still valid or not
struct script_ud_ent_t
{
	edict_t* ent;
	int32_t spawn_count;
};

// Check an entity argument for validity and return the entity
static edict_t* script_check_entity(lua_State* L, int arg, bool error = true)
{
	struct script_ud_ent_t* ud = (struct script_ud_ent_t*)luaL_checkudata(L, arg, "script_entity");

	edict_t* ent = ud->ent;

	if (ent->spawn_count != ud->spawn_count)
	{
		if (error)
		{
			luaL_error(L, "entity reference is no longer valid");
		}

		return nullptr;
	}

	return ent;
}

// Valid entity keys for get and set operations
enum script_entity_keys_index
{
	ENTITY_KEY_CLASSNAME,
	ENTITY_KEY_TEAM,
	ENTITY_KEY_TARGETNAME,
	ENTITY_KEY_TARGET,
	ENTITY_KEY_KILLTARGET,
	ENTITY_KEY_PATHTARGET,
	ENTITY_KEY_SCRIPT_ARG,
	ENTITY_KEY_MESSAGE,
	ENTITY_KEY_DELAY,
	ENTITY_KEY_WAIT,
	ENTITY_KEY_SPEED,
	ENTITY_KEY_COUNT

};

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
	"speed",
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
	edict_t* ent = script_check_entity(L, 1);
	int key = luaL_checkoption(L, 2, nullptr, script_entity_keys);

	switch (key)
	{
	case ENTITY_KEY_CLASSNAME:
		script_entity_get_string(L, ent->classname);
		break;

	case ENTITY_KEY_TEAM:
		script_entity_get_string(L, ent->team);
		break;

	case ENTITY_KEY_TARGETNAME:
		script_entity_get_string(L, ent->targetname);
		break;

	case ENTITY_KEY_TARGET:
		script_entity_get_string(L, ent->target);
		break;

	case ENTITY_KEY_KILLTARGET:
		script_entity_get_string(L, ent->killtarget);
		break;

	case ENTITY_KEY_PATHTARGET:
		script_entity_get_string(L, ent->pathtarget);
		break;

	case ENTITY_KEY_SCRIPT_ARG:
		script_entity_get_string(L, ent->script_arg);
		break;

	case ENTITY_KEY_MESSAGE:
		script_entity_get_string(L, ent->message);
		break;

	case ENTITY_KEY_DELAY:
		lua_pushnumber(L, ent->delay);
		break;

	case ENTITY_KEY_WAIT:
		lua_pushnumber(L, ent->wait);
		break;

	case ENTITY_KEY_SPEED:
		lua_pushnumber(L, ent->speed);
		break;

	case ENTITY_KEY_COUNT:
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
	edict_t* ent = script_check_entity(L, 1);
	int key = luaL_checkoption(L, 2, nullptr, script_entity_keys);

	switch (key)
	{
	case ENTITY_KEY_TARGET:
		ent->target = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case ENTITY_KEY_KILLTARGET:
		ent->killtarget = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case ENTITY_KEY_PATHTARGET:
		ent->pathtarget = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case ENTITY_KEY_SCRIPT_ARG:
		ent->script_arg = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case ENTITY_KEY_MESSAGE:
		ent->message = script_entity_set_string(luaL_checkstring(L, 3));
		break;

	case ENTITY_KEY_DELAY:
		ent->delay = luaL_checknumber(L, 3);
		break;

	case ENTITY_KEY_WAIT:
		ent->wait = luaL_checknumber(L, 3);
		break;

	case ENTITY_KEY_SPEED:
		ent->speed = luaL_checknumber(L, 3);
		break;

	case ENTITY_KEY_COUNT:
		ent->count = luaL_checkinteger(L, 3);
		break;

	default:
		return luaL_error(L, "attempt to set a read-only value");
	}

	return 0;
}

// Function for delayed trigger temporary entity
THINK(script_entity_trigger_delay) (edict_t* self) -> void
{
	edict_t* ent = self->target_ent;

	if (ent->spawn_count != self->count)
	{
		gi.Com_Print("func_script delayed trigger target no longer exists\n");
	}
	else if (ent->use)
	{
		ent->use(ent, self, self->activator);
	}
	else
	{
		gi.Com_Print("func_script delayed trigger target has no use function\n");
	}

	G_FreeEdict(self);
}

// Triggers the entity, spawning a temporary entity to do it later if a delay is specified
static int script_entity_trigger(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);

	int nargs = lua_gettop(L);

	// Second argument is an optional delay
	float delay = -1;

	if (nargs > 1 && lua_type(L, 2) != LUA_TNIL)
	{
		delay = luaL_checknumber(L, 2);
	}

	// Get reference to self and activator from the top of the trigger stack
	lua_getfield(L, LUA_REGISTRYINDEX, "script_triggerstack");
	int n = luaL_len(L, -1);
	lua_geti(L, -1, n);

	lua_getfield(L, -1, "self");
	edict_t* self = (edict_t*)lua_touserdata(L, -1);

	lua_getfield(L, -2, "activator");
	edict_t* activator = (edict_t*)lua_touserdata(L, -1);

	// Also pop the arguments off the stack so the stack is empty during the trigger
	// in case this directly or indirectly triggers another func_script
	lua_pop(L, 4 + nargs);

	if (delay > 0)
	{
		// Spawn a temporary entity to trigger it later
		edict_t* t = G_Spawn();
		t->classname = "DelayedTrigger";
		t->nextthink = level.time + gtime_t::from_sec(delay);
		t->think = script_entity_trigger_delay;
		t->activator = activator;
		t->target_ent = ent;
		t->count = ent->spawn_count;
		t->script_arg = ent->script_arg;

		return 0;
	}
	else
	{
		// Try to prevent an infinite loop
		if (ent == self)
		{
			return luaL_error(L, "func_script triggered itself with no delay");
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
	}

	return 0;
}

// Kills the entity, same as killtarget on a trigger, meaning it outright deletes the entity
// Note that monsters are sent directly to the shadow realm without playing death animations

void G_MonsterKilled(edict_t* self);

static void script_entity_do_kill(edict_t* ent)
{
	if (ent->teammaster)
	{
		if (ent->flags & FL_TEAMSLAVE)
		{
			for (edict_t* master = ent->teammaster; master; master = master->teamchain)
			{
				if (master->teamchain == ent)
				{
					master->teamchain = ent->teamchain;
					break;
				}
			}
		}
		else if (ent->flags & FL_TEAMMASTER)
		{
			ent->teammaster->flags &= ~FL_TEAMMASTER;

			edict_t* new_master = ent->teammaster->teamchain;

			if (new_master)
			{
				new_master->flags |= FL_TEAMMASTER;
				new_master->flags &= ~FL_TEAMSLAVE;

				for (edict_t* m = new_master; m; m = m->teamchain)
				{
					m->teammaster = new_master;
				}
			}
		}
	}

	if (ent->svflags & SVF_MONSTER)
	{
		if (!ent->deadflag && !(ent->monsterinfo.aiflags & AI_DO_NOT_COUNT) && !(ent->spawnflags & SPAWNFLAG_MONSTER_DEAD))
		{
			G_MonsterKilled(ent);
		}
	}

	G_FreeEdict(ent);
}

// Function for delayed kill temporary entity
THINK(script_entity_kill_delay) (edict_t* self) -> void
{
	edict_t* ent = self->target_ent;

	if (ent->spawn_count != self->count)
	{
		gi.Com_Print("func_script delayed kill target no longer exists\n");
	}
	else
	{
		script_entity_do_kill(ent);
	}

	G_FreeEdict(self);
}

// Kills a target, spawning a temporary entity to do it later if a delay is specified
static int script_entity_kill(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);

	int nargs = lua_gettop(L);

	// Second argument is an optional delay
	float delay = -1;

	if (nargs > 1 && lua_type(L, 2) != LUA_TNIL)
	{
		delay = luaL_checknumber(L, 2);
	}

	if (delay > 0)
	{
		// Spawn a temporary entity to kill it later
		edict_t* t = G_Spawn();
		t->classname = "DelayedKill";
		t->nextthink = level.time + gtime_t::from_sec(delay);
		t->think = script_entity_kill_delay;
		t->target_ent = ent;
		t->count = ent->spawn_count;

		return 0;
	}
	else
	{
		script_entity_do_kill(ent);
	}

	return 0;
}

// If the entity is a player, display a message on their screen
// This uses the same style and sound as trigger messages
static int script_entity_message(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);
	const char* message = luaL_checkstring(L, 2);

	gi.LocCenter_Print(ent, "{}", message);
	gi.sound(ent, CHAN_AUTO, gi.soundindex("misc/talk1.wav"), 1, ATTN_NORM, 0);

	return 0;
}

// Special function for target_strings - set the displayed string without adding the value to the string pool
// This is potentially beneficial because the string has no need to be stored and theoretically a lot of strings
// could be generated if the number is changed often to a lot of different values
static int script_entity_setstring(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);
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

// Returns true if the entity is a player
static int script_entity_player(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);

	lua_pushboolean(L, ent->svflags & SVF_PLAYER);

	return 1;
}

// Returns true if the entity is a monster
static int script_entity_monster(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);

	lua_pushboolean(L, ent->svflags & SVF_MONSTER);

	return 1;
}

// Returns true if an entity reference is still valid, or false if it has gone stale
static int script_entity_valid(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1, false);

	lua_pushboolean(L, ent != nullptr);

	return 1;
}

// Creates an entity object at the top of the stack
static void script_push_entity(lua_State* L, edict_t* ent)
{
	struct script_ud_ent_t* ud = (struct script_ud_ent_t*)lua_newuserdata(L, sizeof(struct script_ud_ent_t*));

	ud->ent = ent;
	ud->spawn_count = ent->spawn_count;

	// If the entity pointer points to an empty slot, make sure it counts as an invalid reference even if the slot is filled later
	// This should only happen if the part of the trigger chain has been killtargeted before triggering a function
	if (!ent->inuse)
	{
		ud->spawn_count--;
	}

	luaL_setmetatable(L, "script_entity");
}

// =============================================================================
// API functions
// =============================================================================

// Find all entities with a given value for a string key (defaulting to targetname)
static int script_find(lua_State* L)
{
	const char* value = luaL_checkstring(L, 1);
	int key = luaL_checkoption(L, 2, "targetname", script_entity_keys);

	// Get a search function for the given key
	// Using a function pointer here is a bit weird, but this is the only way I could think of
	// to use different versions of the templated function in one place
	edict_t* (*search_function)(edict_t*, const std::string_view&) = nullptr;

	switch (key)
	{
	case ENTITY_KEY_CLASSNAME:
		search_function = G_FindByString<&edict_t::classname>;
		break;

	case ENTITY_KEY_TEAM:
		search_function = G_FindByString<&edict_t::team>;
		break;

	case ENTITY_KEY_TARGETNAME:
		search_function = G_FindByString<&edict_t::targetname>;
		break;

	case ENTITY_KEY_TARGET:
		search_function = G_FindByString<&edict_t::target>;
		break;

	case ENTITY_KEY_KILLTARGET:
		search_function = G_FindByString<&edict_t::killtarget>;
		break;

	case ENTITY_KEY_PATHTARGET:
		search_function = G_FindByString<&edict_t::pathtarget>;
		break;

	case ENTITY_KEY_SCRIPT_ARG:
		search_function = G_FindByString<&edict_t::script_arg>;
		break;

	default:
		return luaL_error(L, "attempt to search by non-string key");
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

	if (type == LUA_TNIL || type == LUA_TNUMBER || type == LUA_TBOOLEAN || type == LUA_TSTRING || type == LUA_TUSERDATA)
	{
		lua_rawset(L, 1);
	}
	else
	{
		return luaL_argerror(L, 2, "invalid type: must be nil, number, boolean, string, or entity");
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

	size_t loc = str.find_first_of(':');
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
		val += ':';
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
	lua_pushcfunction(L, script_entity_kill);
	lua_setfield(L, -2, "kill");
	lua_pushcfunction(L, script_entity_message);
	lua_setfield(L, -2, "message");
	lua_pushcfunction(L, script_entity_player);
	lua_setfield(L, -2, "player");
	lua_pushcfunction(L, script_entity_monster);
	lua_setfield(L, -2, "monster");
	lua_pushcfunction(L, script_entity_valid);
	lua_setfield(L, -2, "valid");
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

		int type = lua_type(L, -2);

		if (type == LUA_TUSERDATA)
		{
			val += "entity:";

			struct script_ud_ent_t* ud = (script_ud_ent_t*)lua_touserdata(L, -2);

			if (!ud->ent->inuse || ud->ent->spawn_count != ud->spawn_count)
			{
				val += "-1";
			}
			else
			{
				val += std::to_string(ud->ent - g_edicts);
			}
		}
		else
		{
			val += lua_typename(L, type);
			val += ':';
			val += luaL_tolstring(L, -2, nullptr);
			lua_pop(L, 1);
		}

		globals.insert_or_assign(key, val);

		lua_pop(L, 2);
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
		size_t loc = str.find_first_of(':');
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
		else if (str_type == "entity")
		{
			int32_t n = std::stoi(str_val);

			if (n < 0)
			{
				// Invalid entities are referenced to worldspawn but with a spawn_count of -1
				// Since worldspawn's slot is never recycled, let alone over 4 billion times,
				// this should be completely safe
				struct script_ud_ent_t* ud = (struct script_ud_ent_t*)lua_newuserdata(L, sizeof(struct script_ud_ent_t*));

				ud->ent = g_edicts;
				ud->spawn_count = -1;

				luaL_setmetatable(L, "script_entity");
			}
			else
			{
				script_push_entity(L, g_edicts + n);
			}
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
// These live in the unordered_map all the time so this function can be used for both
// saving and loading the variables
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
