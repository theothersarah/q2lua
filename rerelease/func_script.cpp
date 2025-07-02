#include "g_local.h"

#include <lua.hpp>

// =============================================================================
// Allocator for Lua memory
// =============================================================================

// These wrap the engine's memory allocation functions in a form that Lua can use
// Return values TagMalloc are not checked because it raises a fuss all by itself
// if memory runs out

// The game engine doesn't provide a reallocator so we have to do it manually
static void* script_realloc(void* ptr, size_t osize, size_t nsize)
{
	void* newptr = gi.TagMalloc(nsize, TAG_GAME);

	memcpy(newptr, ptr, (nsize < osize) ? nsize : osize);

	gi.TagFree(ptr);

	return newptr;
}

// Lua uses this for all its memory managements needs
static void* script_lua_allocator(void* ud, void* ptr, size_t osize, size_t nsize)
{
	if (nsize == 0)
	{
		gi.TagFree(ptr);

		// Lua expects null as the result of free
		return nullptr;
	}
	else
	{
		if (ptr == nullptr)
		{
			return gi.TagMalloc(nsize, TAG_GAME);
		}
		else
		{
			return script_realloc(ptr, osize, nsize);
		}
	}
}

// =============================================================================
// String pool
// =============================================================================

// Keeps copies of strings from Lua functions, because those strings only live until
// the function returns, but they may be needed for the entire duration of the level
// It also prevents duplicate strings from being allocated and will expand if it
// runs out of room

// The string list is allocated as TAG_GAME so it lasts as long as the scripting engine
// while the strings are allocated as TAG_LEVEL so they automatically free on a level
// transition

// Arbitrary but generous, it's only 2 kilobytes anyway
static const size_t script_stringpool_starter = 256;

static char** script_stringpool_list = NULL;
static size_t script_stringpool_size;
static size_t script_stringpool_count;

// Compare function for bsearch and qsort for sorting strings by strcmp order
static int script_stringpool_compare(void* ud, const void* pa, const void* pb)
{
	const char* str1 = *((const char**)pa);
	const char* str2 = *((const char**)pb);

	return strcmp(str1, str2);
}

// Adds a string to the string pool and returns a pointer to either the copy made of it or
// an identical string already found in the pool
static const char* script_stringpool_add(const char* str)
{
	// It's valid to pass null pointers to this function but there's no point trying to fit them into the pool
	if (str == nullptr)
	{
		return nullptr;
	}

	// Search for the string
	char** found = (char**)bsearch_s(&str, script_stringpool_list, script_stringpool_count, sizeof(char*), script_stringpool_compare, nullptr);

	if (found != nullptr)
	{
		// String found, so return the one already in the pool
		return *found;
	}
	else
	{
		// String not found, so make a copy of it
		char* newstr = G_CopyString(str, TAG_LEVEL);

		// Expand the pool to fit it if necessary
		script_stringpool_count++;

		if (script_stringpool_count > script_stringpool_size)
		{
			size_t new_size = script_stringpool_size + script_stringpool_starter;

			script_stringpool_list = (char**)script_realloc(script_stringpool_list, script_stringpool_size * sizeof(char*), new_size * sizeof(char*));

			script_stringpool_size = new_size;
		}

		// Add the string to the end othe list and sort it so it's in the order bsearch expects next time
		script_stringpool_list[script_stringpool_count - 1] = newstr;

		qsort_s(script_stringpool_list, script_stringpool_count, sizeof(char*), script_stringpool_compare, nullptr);

		return newstr;
	}
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
// Comparing a stored spawn_count to the entity's is a pretty good test,
// because it takes 68 years to wrap the spawn_count if it gets recycled at its
// maximum rate of once every 0.5 seconds
static edict_t* script_check_entity(lua_State* L, int arg, bool error = true)
{
	struct script_ud_ent_t* ud = (struct script_ud_ent_t*)luaL_checkudata(L, arg, "script_entity");

	edict_t* ent = ud->ent;

	if (ent->spawn_count != ud->spawn_count)
	{
		if (error)
		{
			luaL_argerror(L, arg, "entity reference is no longer valid");
		}

		return nullptr;
	}

	return ent;
}

// Valid entity keys for get, set, and find operations
// All are valid for get, not all are valid for set or find
enum script_entity_keys_index
{
	ENTITY_KEY_CLASSNAME,
	ENTITY_KEY_TEAM,
	ENTITY_KEY_TARGETNAME,
	ENTITY_KEY_TARGET,
	ENTITY_KEY_KILLTARGET,
	ENTITY_KEY_PATHTARGET,
	ENTITY_KEY_DEATHTARGET,
	ENTITY_KEY_HEALTHTARGET,
	ENTITY_KEY_ITEMTARGET,
	ENTITY_KEY_COMBATTARGET,
	ENTITY_KEY_SCRIPT_FUNCTION,
	ENTITY_KEY_SCRIPT_ARG,
	ENTITY_KEY_MESSAGE,
	ENTITY_KEY_DELAY,
	ENTITY_KEY_WAIT,
	ENTITY_KEY_SPEED,
	ENTITY_KEY_COUNT,
	ENTITY_KEY_MAX_HEALTH,
	ENTITY_KEY_HEALTH
};

static const char* script_entity_keys[] =
{
	"classname",
	"team",
	"targetname",
	"target",
	"killtarget",
	"pathtarget",
	"deathtarget",
	"healthtarget",
	"itemtarget",
	"combattarget",
	"script_function",
	"script_arg",
	"message",
	"delay",
	"wait",
	"speed",
	"count",
	"max_health",
	"health",
	nullptr
};

// Get a value of a given type from the entity
static int script_entity_get(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);
	int key = luaL_checkoption(L, 2, nullptr, script_entity_keys);

	switch (key)
	{
	case ENTITY_KEY_CLASSNAME:
		lua_pushstring(L, ent->classname);
		break;

	case ENTITY_KEY_TEAM:
		lua_pushstring(L, ent->team);
		break;

	case ENTITY_KEY_TARGETNAME:
		lua_pushstring(L, ent->targetname);
		break;

	case ENTITY_KEY_TARGET:
		lua_pushstring(L, ent->target);
		break;

	case ENTITY_KEY_KILLTARGET:
		lua_pushstring(L, ent->killtarget);
		break;

	case ENTITY_KEY_PATHTARGET:
		lua_pushstring(L, ent->pathtarget);
		break;

	case ENTITY_KEY_DEATHTARGET:
		lua_pushstring(L, ent->deathtarget);
		break;

	case ENTITY_KEY_HEALTHTARGET:
		lua_pushstring(L, ent->healthtarget);
		break;

	case ENTITY_KEY_ITEMTARGET:
		lua_pushstring(L, ent->itemtarget);
		break;

	case ENTITY_KEY_COMBATTARGET:
		lua_pushstring(L, ent->combattarget);
		break;

	case ENTITY_KEY_SCRIPT_FUNCTION:
		lua_pushstring(L, ent->script_function);
		break;

	case ENTITY_KEY_SCRIPT_ARG:
		lua_pushstring(L, ent->script_arg);
		break;

	case ENTITY_KEY_MESSAGE:
		lua_pushstring(L, ent->message);
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

	case ENTITY_KEY_MAX_HEALTH:
		lua_pushinteger(L, ent->max_health);
		break;

	case ENTITY_KEY_HEALTH:
		lua_pushinteger(L, ent->health);
		break;

	default:
		return luaL_error(L, "if you see this, Sarah fucked up");
	}

	return 1;
}

// Set a value of the given type on the entity
static int script_entity_set(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);
	int key = luaL_checkoption(L, 2, nullptr, script_entity_keys);

	switch (key)
	{
	case ENTITY_KEY_TARGET:
		ent->target = script_stringpool_add(lua_tostring(L, 3));
		break;

	case ENTITY_KEY_KILLTARGET:
		ent->killtarget = script_stringpool_add(lua_tostring(L, 3));
		break;

	case ENTITY_KEY_PATHTARGET:
		ent->pathtarget = script_stringpool_add(lua_tostring(L, 3));
		break;

	case ENTITY_KEY_DEATHTARGET:
		ent->deathtarget = script_stringpool_add(lua_tostring(L, 3));
		break;

	case ENTITY_KEY_HEALTHTARGET:
		ent->healthtarget = script_stringpool_add(lua_tostring(L, 3));
		break;

	case ENTITY_KEY_ITEMTARGET:
		ent->itemtarget = script_stringpool_add(lua_tostring(L, 3));
		break;

	case ENTITY_KEY_COMBATTARGET:
		ent->combattarget = script_stringpool_add(lua_tostring(L, 3));
		break;

	case ENTITY_KEY_SCRIPT_ARG:
		ent->script_arg = script_stringpool_add(lua_tostring(L, 3));
		break;

	case ENTITY_KEY_MESSAGE:
		ent->message = script_stringpool_add(lua_tostring(L, 3));
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
	struct script_ud_ent_t* ud = (struct script_ud_ent_t*)lua_newuserdatauv(L, sizeof(struct script_ud_ent_t), 0);

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

// Returns a list of all entities with a given value for a string key (defaulting to targetname)
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

	case ENTITY_KEY_DEATHTARGET:
		search_function = G_FindByString<&edict_t::deathtarget>;
		break;

	case ENTITY_KEY_HEALTHTARGET:
		search_function = G_FindByString<&edict_t::healthtarget>;
		break;

	case ENTITY_KEY_ITEMTARGET:
		search_function = G_FindByString<&edict_t::itemtarget>;
		break;

	case ENTITY_KEY_COMBATTARGET:
		search_function = G_FindByString<&edict_t::combattarget>;
		break;

	case ENTITY_KEY_SCRIPT_FUNCTION:
		search_function = G_FindByString<&edict_t::script_function>;
		break;

	case ENTITY_KEY_SCRIPT_ARG:
		search_function = G_FindByString<&edict_t::script_arg>;
		break;

	case ENTITY_KEY_MESSAGE:
		search_function = G_FindByString<&edict_t::message>;
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

// For each element in a list, calls a function with that element as the argument
static int script_foreach(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TFUNCTION);

	int n = lua_rawlen(L, 1);

	for (int i = 1; i <= n; i++)
	{
		lua_pushvalue(L, 2);
		lua_rawgeti(L, 1, i);
		lua_call(L, 1, 0);
	}

	return 0;
}

// Same as foreach, but returns a list containing every element for which the function returned anything but nil or false
// Also returns an integer containing the count as a second return value
static int script_filter(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TFUNCTION);

	int n = lua_rawlen(L, 1);

	int count = 0;

	lua_newtable(L);

	for (int i = 1; i <= n; i++)
	{
		lua_pushvalue(L, 2);
		lua_rawgeti(L, 1, i);
		lua_call(L, 1, 1);

		int keep = lua_toboolean(L, -1);
		lua_pop(L, 1);

		if (keep)
		{
			lua_rawgeti(L, 1, i);
			lua_rawseti(L, -2, ++count);
		}
	}

	lua_pushinteger(L, count);

	return 2;
}

// Returns a randomly-selected element from the list
static int script_pick(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	int n = lua_rawlen(L, 1);

	if (n == 0)
	{
		lua_pushnil(L);
	}
	else
	{
		lua_rawgeti(L, 1, 1 + irandom(n - 1));
	}

	return 1;
}

// Iterator function equivalent to the values iterator from Programming in Lua
static int script_values_iterator(lua_State* L)
{
	// Increment the index
	lua_pushvalue(L, lua_upvalueindex(2));
	lua_pushinteger(L, 1);
	lua_arith(L, LUA_OPADD);

	// Get the value of the index and overwrite the old upvalue
	int i = lua_tointeger(L, -1);
	lua_replace(L, lua_upvalueindex(2));

	// Return the element at that index
	lua_rawgeti(L, lua_upvalueindex(1), i);

	return 1;
}

// Factory for the values iterator
static int script_values(lua_State* L)
{
	// Argument must be a table and is the iterator's first upvalue
	luaL_checktype(L, 1, LUA_TTABLE);

	// Pop excess arguments
	int n = lua_gettop(L);

	if (n > 1)
	{
		lua_pop(L, n - 1);
	}

	// An index is the iterator's second upvalue
	lua_pushinteger(L, 0);

	// Push the function as the return value
	lua_pushcclosure(L, script_values_iterator, 2);

	return 1;
}

// =============================================================================
// Persistent variables
// =============================================================================

// Metamethod __newindex for the global table
// Make sure key is a string and prevent values of invalid types from being added to the global table
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
		return luaL_argerror(L, 3, "invalid type: must be nil, number, boolean, string, or entity");
	}

	return 0;
}

// Metamethod __newindex for the crosslevel table
// Make sure key is a string and prevent values of invalid types from being added to the crosslevel table
static int script_crosslevel_set(lua_State* L)
{
	luaL_checktype(L, 2, LUA_TSTRING);
	int type = lua_type(L, 3);

	if (type == LUA_TNIL || type == LUA_TNUMBER || type == LUA_TBOOLEAN || type == LUA_TSTRING)
	{
		lua_rawset(L, 1);
	}
	else
	{
		return luaL_argerror(L, 3, "invalid type: must be nil, number, boolean, or string");
	}

	return 0;
}

// =============================================================================
// Script initialization and loading
// =============================================================================

// Entity member functions
static const luaL_Reg script_entity_functions[] =
{
	{"get", script_entity_get},
	{"set", script_entity_set},
	{"trigger", script_entity_trigger},
	{"setstring", script_entity_setstring},
	{"kill", script_entity_kill},
	{"message", script_entity_message},
	{"player", script_entity_player},
	{"monster", script_entity_monster},
	{"valid", script_entity_valid},
	{nullptr, nullptr}
};

// API functions
static const luaL_Reg script_functions[] =
{
	{"find", script_find},
	{"foreach", script_foreach},
	{"filter", script_filter},
	{"pick", script_pick},
	{"values", script_values},
	{nullptr, nullptr}
};

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

// Lua instance has a lifetime of TAG_GAME
static lua_State* L;

// Initialize the scripting engine
void script_init()
{
	// Initialize the string pool, which has a lifetime of TAG_GAME
	script_stringpool_list = (char**)gi.TagMalloc(script_stringpool_starter * sizeof(char*), TAG_GAME);

	script_stringpool_size = script_stringpool_starter;
	script_stringpool_count = 0;

	// Initialize the Lua state
	// It's okay not to check if this fails, because that would only be caused by an out of memory error
	// and the allocators have error handling
	L = lua_newstate(script_lua_allocator, nullptr);

	// Create table for API
	lua_newtable(L);
	luaL_setfuncs(L, script_functions, 0);

	// Add table for crosslevel variables, this remains valid for the lifetime of the scripting engine
	lua_newtable(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_crosslevel_set);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "crosslevel");

	// Add math library
	luaopen_math(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "math");

	// Add string library
	luaopen_string(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "string");

	// Add table library
	luaopen_table(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "table");

	// Write protect API
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);

	// Add API to the registry
	lua_setfield(L, LUA_REGISTRYINDEX, "script_api");

	// Create metatable for entity objects
	luaL_newmetatable(L, "script_entity");
	luaL_newlib(L, script_entity_functions);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_pop(L, 1);
}

// Load and execute a script for a given map
void script_load(const char* mapname)
{
	// Strings are all tagged TAG_LEVEL so they're freed by now
	script_stringpool_count = 0;

	// Clear script global variables by overwriting the table with a fresh one
	lua_getfield(L, LUA_REGISTRYINDEX, "script_api");
	lua_pushliteral(L, "globals");
	lua_newtable(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_global_set);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_rawset(L, -3);
	lua_pop(L, 1);

	// Clear Lua global variables by overwriting the table with a fresh one
	lua_newtable(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_functionsonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_seti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);

	// Load the script for the current map
	cvar_t* gamedir = gi.cvar("gamedir", "", CVAR_NOFLAGS);

	if (luaL_loadfile(L, G_Fmt("./{}/scripts/{}.lua", gamedir->string, mapname).data()) != LUA_OK)
	{
		const char* errstr = lua_tostring(L, -1);
		gi.Com_PrintFmt("Error loading script for map {}: {}\n", mapname, errstr);
		lua_pop(L, 1);
		return;
	}

	// Execute the script
	if (lua_pcall(L, 0, 0, 0) != LUA_OK)
	{
		const char* errstr = lua_tostring(L, -1);
		gi.Com_PrintFmt("Error executing script for map {}: {}\n", mapname, errstr);
		lua_pop(L, 1);
		return;
	}

	// Write protect lua globals now that functions are added
	lua_geti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
	lua_newtable(L);
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_pop(L, 1);

	// Add API table to the global space
	lua_geti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
	lua_pushliteral(L, "script");
	lua_getfield(L, LUA_REGISTRYINDEX, "script_api");
	lua_rawset(L, -3);
	lua_pop(L, 1);

	// The trigger stack should probably be empty by the time a level transition happens,
	// but clear it just in case
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "script_triggerstack");

	gi.Com_PrintFmt("Loaded script for map {}\n", mapname);
}

// =============================================================================
// Save support
// =============================================================================

// Get global or crosslevel variables for saving
void script_get_variables(std::unordered_map<std::string, std::string>& variables, bool crosslevel)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "script_api");

	if (crosslevel)
	{
		lua_getfield(L, -1, "crosslevel");
	}
	else
	{
		lua_getfield(L, -1, "globals");
	}

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

		variables.insert_or_assign(key, val);

		lua_pop(L, 2);
	}

	lua_pop(L, 2);
}

// Set global or crosslevel variables after loading
void script_set_variables(std::unordered_map<std::string, std::string>& variables, bool crosslevel)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "script_api");

	if (crosslevel)
	{
		lua_getfield(L, -1, "crosslevel");
	}
	else
	{
		lua_getfield(L, -1, "globals");
	}

	for (auto it = variables.begin(); it != variables.end(); ++it)
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

// =============================================================================
// func_script entity
// =============================================================================

USE(func_script_use) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	// Try to get a function by the given name
	lua_getglobal(L, self->script_function);

	if (lua_type(L, -1) != LUA_TFUNCTION)
	{
		gi.Com_PrintFmt("{} attempting to call nonexistent function {}\n", *self, self->script_function);
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
		gi.Com_PrintFmt("{} error calling function {}: {}\n", *self, self->script_function, errstr);
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
	// Make sure the entity is set up correctly
	if (!self->script_function)
	{
		gi.Com_PrintFmt("{} has no function set\n", *self);
		G_FreeEdict(self);
		return;
	}

	self->use = func_script_use;
}
