#include "g_local.h"

#include "lua/lua.hpp"

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

static char** script_stringpool_list;
static size_t script_stringpool_size;
static size_t script_stringpool_count;

// Compare function for bsearch and qsort for sorting strings by strcmp order
static int script_stringpool_compare(const void* pa, const void* pb)
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
	char** found = (char**)bsearch(&str, script_stringpool_list, script_stringpool_count, sizeof(char*), script_stringpool_compare);

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

		qsort(script_stringpool_list, script_stringpool_count, sizeof(char*), script_stringpool_compare);

		return newstr;
	}
}

// =============================================================================
// Vector functions
// =============================================================================

// Creates a vector object at the top of the stack
static void script_push_vector(lua_State* L, vec3_t& vec)
{
	vec3_t* ud = (vec3_t*)lua_newuserdatauv(L, sizeof(vec3_t), 0);

	*ud = vec;

	luaL_setmetatable(L, "script_vector");
}

// Add vectors
static int script_vector_add(lua_State* L)
{
	vec3_t* vec1 = (vec3_t*)luaL_checkudata(L, 1, "script_vector");
	vec3_t* vec2 = (vec3_t*)luaL_checkudata(L, 2, "script_vector");

	vec3_t result = *vec1 + *vec2;

	script_push_vector(L, result);

	return 1;
}

// Subtract vectors
static int script_vector_sub(lua_State* L)
{
	vec3_t* vec1 = (vec3_t*)luaL_checkudata(L, 1, "script_vector");
	vec3_t* vec2 = (vec3_t*)luaL_checkudata(L, 2, "script_vector");

	vec3_t result = *vec1 - *vec2;

	script_push_vector(L, result);

	return 1;
}

// Multiply a vector by a scalar
static int script_vector_mul(lua_State* L)
{
	vec3_t* vec;
	float number;

	if (lua_type(L, 1) == LUA_TNUMBER)
	{
		number = luaL_checknumber(L, 1);
		vec = (vec3_t*)luaL_checkudata(L, 2, "script_vector");
	}
	else if (lua_type(L, 2) == LUA_TNUMBER)
	{
		vec = (vec3_t*)luaL_checkudata(L, 1, "script_vector");
		number = luaL_checknumber(L, 2);
	}
	else
	{
		return luaL_error(L, "vector can only be multiplied with a number");
	}

	vec3_t result = *vec * number;

	script_push_vector(L, result);

	return 1;
}

// Negate vector
static int script_vector_neg(lua_State* L)
{
	vec3_t* vec = (vec3_t*)luaL_checkudata(L, 1, "script_vector");

	vec3_t result = *vec * -1;

	script_push_vector(L, result);

	return 1;
}

// Unit vector representing direction from this vector to another one
static int script_vector_direction(lua_State* L)
{
	vec3_t* vec1 = (vec3_t*)luaL_checkudata(L, 1, "script_vector");
	vec3_t* vec2 = (vec3_t*)luaL_checkudata(L, 2, "script_vector");

	vec3_t result = (*vec2 - *vec1).normalized();

	script_push_vector(L, result);

	return 1;
}

// Distance between two vectors
static int script_vector_distance(lua_State* L)
{
	vec3_t* vec1 = (vec3_t*)luaL_checkudata(L, 1, "script_vector");
	vec3_t* vec2 = (vec3_t*)luaL_checkudata(L, 2, "script_vector");

	float result = (*vec2 - *vec1).length();

	lua_pushnumber(L, result);

	return 1;
}

// Linearly interpolate between two vectors
static int script_vector_lerp(lua_State* L)
{
	vec3_t* vec1 = (vec3_t*)luaL_checkudata(L, 1, "script_vector");
	vec3_t* vec2 = (vec3_t*)luaL_checkudata(L, 2, "script_vector");
	float fraction = luaL_checknumber(L, 3);

	vec3_t result = *vec1 + (*vec2 - *vec1) * fraction;

	script_push_vector(L, result);

	return 1;
}

// Get the values of a vector
static int script_vector_values(lua_State* L)
{
	vec3_t* vec = (vec3_t*)luaL_checkudata(L, 1, "script_vector");

	lua_pushnumber(L, vec->x);
	lua_pushnumber(L, vec->y);
	lua_pushnumber(L, vec->z);

	return 3;
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
// The enum and array of strings must be in precisely the same order;
// the enum values are equal to the array index of the corresponding string
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
	ENTITY_KEY_ORIGIN,
	ENTITY_KEY_ANGLES,
	ENTITY_KEY_DELAY,
	ENTITY_KEY_WAIT,
	ENTITY_KEY_SPEED,
	ENTITY_KEY_RANDOM,
	ENTITY_KEY_COUNT,
	ENTITY_KEY_DMG,
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
	"origin",
	"angles",
	"delay",
	"wait",
	"speed",
	"random",
	"count",
	"dmg",
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

	case ENTITY_KEY_ORIGIN:
		script_push_vector(L, ent->s.origin);
		break;

	case ENTITY_KEY_ANGLES:
		script_push_vector(L, ent->s.angles);
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

	case ENTITY_KEY_RANDOM:
		lua_pushnumber(L, ent->random);
		break;

	case ENTITY_KEY_COUNT:
		lua_pushinteger(L, ent->count);
		break;

	case ENTITY_KEY_DMG:
		lua_pushinteger(L, ent->dmg);
		break;

	case ENTITY_KEY_MAX_HEALTH:
		lua_pushinteger(L, ent->max_health);
		break;

	case ENTITY_KEY_HEALTH:
		lua_pushinteger(L, ent->health);
		break;

	default:
		return luaL_argerror(L, 2, "if you see this, Sarah fucked up");
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
	case ENTITY_KEY_TARGETNAME:
		ent->targetname = script_stringpool_add(lua_tostring(L, 3));
		break;

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

	case ENTITY_KEY_SCRIPT_FUNCTION:
		ent->script_function = script_stringpool_add(lua_tostring(L, 3));
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

	case ENTITY_KEY_RANDOM:
		ent->random = luaL_checknumber(L, 3);
		break;

	case ENTITY_KEY_COUNT:
		ent->count = luaL_checkinteger(L, 3);
		break;

	case ENTITY_KEY_DMG:
		ent->dmg = luaL_checkinteger(L, 3);
		break;

	default:
		return luaL_argerror(L, 2, "attempt to set a read-only value");
	}

	return 0;
}

// Function for delayed trigger temporary entity
THINK(script_entity_trigger_delay) (edict_t* self) -> void
{
	edict_t* ent = self->target_ent;

	if (ent->spawn_count != self->count)
	{
		gi.Com_Print("script delayed trigger target no longer exists\n");
	}
	else if (ent->use)
	{
		ent->use(ent, self, self->activator);
	}
	else
	{
		gi.Com_Print("script delayed trigger target no longer has a use function\n");
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

	// Check to make sure it can even be triggered
	if (!ent->use)
	{
		const char* errstr = lua_pushfstring(L, "entity of type %s has no trigger function", ent->classname);
		return luaL_argerror(L, 1, errstr);
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
	// in case this directly or indirectly triggers another script
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
	}
	else
	{
		// Try to prevent an infinite loop
		if (ent == self)
		{
			return luaL_argerror(L, 1, "script triggered itself with no delay");
		}

		ent->use(ent, self, activator);
	}

	return 0;
}

// Kills the entity, same as killtarget on a trigger, meaning it outright deletes the entity
// Note that monsters are sent directly to the shadow realm without playing death animations
// or leaving a corpse
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
		gi.Com_Print("script delayed kill target no longer exists\n");
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

	// Make sure it's a player
	if (ent->svflags & SVF_PLAYER)
	{
		return luaL_argerror(L, 1, "entity cannot be a player");
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
	}
	else
	{
		script_entity_do_kill(ent);
	}

	return 0;
}

// If the entity is a player, display a message on their screen instantly or after a delay
// This uses the same style and sound as trigger messages
THINK(script_entity_message_delay) (edict_t* self) -> void
{
	edict_t* ent = self->target_ent;

	if (ent->spawn_count != self->count)
	{
		gi.Com_Print("script delayed message target no longer exists\n");
	}
	else
	{
		gi.LocCenter_Print(ent, "{}", self->message);
		gi.sound(ent, CHAN_AUTO, gi.soundindex("misc/talk1.wav"), 1, ATTN_NORM, 0);
	}

	G_FreeEdict(self);
}

static int script_entity_message(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);
	const char* message = luaL_checkstring(L, 2);

	int nargs = lua_gettop(L);

	// Third argument is an optional delay
	float delay = -1;

	if (nargs > 2 && lua_type(L, 3) != LUA_TNIL)
	{
		delay = luaL_checknumber(L, 3);
	}

	// Make sure it's a player
	if (!(ent->svflags & SVF_PLAYER))
	{
		const char* errstr = lua_pushfstring(L, "entity must be a player - is %s", ent->classname);
		return luaL_argerror(L, 1, errstr);
	}

	if (delay > 0)
	{
		// Spawn a temporary entity to kill it later
		edict_t* t = G_Spawn();
		t->classname = "DelayedMessage";
		t->nextthink = level.time + gtime_t::from_sec(delay);
		t->think = script_entity_message_delay;
		t->message = script_stringpool_add(message);
		t->target_ent = ent;
		t->count = ent->spawn_count;
	}
	else
	{
		gi.LocCenter_Print(ent, "{}", message);
		gi.sound(ent, CHAN_AUTO, gi.soundindex("misc/talk1.wav"), 1, ATTN_NORM, 0);
	}

	return 0;
}

// Give an item to a player, returning true if it was accepted and false if it wasn't due to no inventory space
static int script_entity_give(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);
	const char* name = luaL_checkstring(L, 2);

	// Make sure it's a player
	if (!(ent->svflags & SVF_PLAYER))
	{
		const char* errstr = lua_pushfstring(L, "entity must be a player - is %s", ent->classname);
		return luaL_argerror(L, 1, errstr);
	}

	// Find the item
	gitem_t* item = FindItemByClassname(name);

	if (item == nullptr)
	{
		const char* errstr = lua_pushfstring(L, "invalid item classname %s", name);
		return luaL_argerror(L, 2, errstr);
	}

	// Spawn it and give it to the player
	edict_t* item_ent = G_Spawn();
	item_ent->classname = item->classname;
	SpawnItem(item_ent, item);

	if (item_ent->inuse)
	{
		Touch_Item(item_ent, ent, null_trace, true);

		// Check if it was accepted
		if (item_ent->inuse)
		{
			G_FreeEdict(item_ent);
			lua_pushboolean(L, 0);
		}
		else
		{
			lua_pushboolean(L, 1);
		}
	}

	return 1;
}

//
static int script_entity_heal(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);

	return 0;
}

//
static int script_entity_damage(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);

	return 0;
}

// Change the noise of a target_speaker
static int script_entity_setnoise(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);
	const char* sound = luaL_checkstring(L, 2);

	// Make sure it's actually a target_speaker
	if (Q_strcasecmp(ent->classname, "target_speaker"))
	{
		const char* errstr = lua_pushfstring(L, "entity must be a target_speaker - is %s", ent->classname);
		return luaL_argerror(L, 1, errstr);
	}

	// Set the noise
	ent->noise_index = gi.soundindex(sound);

	// If it's an ambient sound that's currently active, change it
	if (ent->s.sound)
	{
		ent->s.sound = ent->noise_index;
	}

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
		const char* errstr = lua_pushfstring(L, "entity must be a target_string - is %s", ent->classname);
		return luaL_argerror(L, 1, errstr);
	}

	// Setting the string to an empty string afterward does two things:
	// First: it stops problems when str is inevitable garbage collected by Lua
	// Second: it adds the behavior that triggering the target_string again clears it
	ent->message = str;
	ent->use(ent, nullptr, nullptr);
	ent->message = "";

	return 0;
}

// Returns true if the entity can be damaged
static int script_entity_damageable(lua_State* L)
{
	edict_t* ent = script_check_entity(L, 1);

	lua_pushboolean(L, ent->takedamage);

	return 1;
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

// Create a new vector
static int script_vector(lua_State* L)
{
	vec3_t vec = {};

	vec.x = luaL_checknumber(L, 1);
	vec.y = luaL_checknumber(L, 2);
	vec.z = luaL_checknumber(L, 3);

	script_push_vector(L, vec);

	return 1;
}

// Spawn an entity with a given position and optionally angle
static int script_spawn(lua_State* L)
{
	const char* classname = luaL_checkstring(L, 1);
	vec3_t* origin = (vec3_t*)luaL_checkudata(L, 2, "script_vector");

	int nargs = lua_gettop(L);

	vec3_t angles = {};

	if (nargs > 2 && lua_type(L, 3) != LUA_TNIL)
	{
		angles = *(vec3_t*)luaL_checkudata(L, 3, "script_vector");
	}

	edict_t* ent = G_Spawn();
	ent->classname = classname;
	ent->s.origin = *origin;
	ent->s.angles = angles;
	st = {};

	ent->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	ED_CallSpawn(ent);
	gi.linkentity(ent);

	KillBox(ent, false);

	ent->s.renderfx |= RF_IR_VISIBLE;

	script_push_entity(L, ent);

	return 1;
}

// This is equivalent to the old G_Find except the logic skips worldspawn,
// not as a purposeful feature but because it worked out like that and I
// like this logic better
static edict_t* script_find_offset(edict_t* from, size_t value_offset, const char* value)
{
	// If from is null, start from the beginning
	if (from == nullptr)
	{
		from = g_edicts;
	}

	while (++from < &g_edicts[globals.num_edicts])
	{
		if (!from->inuse)
		{
			continue;
		}

		const char* str = *(const char**)((const char*)from + value_offset);

		if (str == nullptr)
		{
			continue;
		}

		if (Q_strcasecmp(str, value) == 0)
		{
			return from;
		}
	}

	return nullptr;
}

// Returns a list of all entities with a given value for a string key (defaulting to targetname)
static int script_find(lua_State* L)
{
	const char* value = luaL_checkstring(L, 1);
	int key = luaL_checkoption(L, 2, "targetname", script_entity_keys);

	// Get the offset for the given key which is used to identify it by the search function
	size_t value_offset;

	switch (key)
	{
	case ENTITY_KEY_CLASSNAME:
		value_offset = offsetof(edict_t, classname);
		break;

	case ENTITY_KEY_TEAM:
		value_offset = offsetof(edict_t, team);
		break;

	case ENTITY_KEY_TARGETNAME:
		value_offset = offsetof(edict_t, targetname);
		break;

	case ENTITY_KEY_TARGET:
		value_offset = offsetof(edict_t, target);
		break;

	case ENTITY_KEY_KILLTARGET:
		value_offset = offsetof(edict_t, killtarget);
		break;

	case ENTITY_KEY_PATHTARGET:
		value_offset = offsetof(edict_t, pathtarget);
		break;

	case ENTITY_KEY_DEATHTARGET:
		value_offset = offsetof(edict_t, deathtarget);
		break;

	case ENTITY_KEY_HEALTHTARGET:
		value_offset = offsetof(edict_t, healthtarget);
		break;

	case ENTITY_KEY_ITEMTARGET:
		value_offset = offsetof(edict_t, itemtarget);
		break;

	case ENTITY_KEY_COMBATTARGET:
		value_offset = offsetof(edict_t, combattarget);
		break;

	case ENTITY_KEY_SCRIPT_FUNCTION:
		value_offset = offsetof(edict_t, script_function);
		break;

	case ENTITY_KEY_SCRIPT_ARG:
		value_offset = offsetof(edict_t, script_arg);
		break;

	case ENTITY_KEY_MESSAGE:
		value_offset = offsetof(edict_t, message);
		break;

	default:
		return luaL_argerror(L, 2, "attempt to search by non-string key");
	}

	// Table for results
	lua_newtable(L);

	int n = 1;

	edict_t* ent = nullptr;

	while ((ent = script_find_offset(ent, value_offset, value)))
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
// Table metamethods
// =============================================================================

// These table metamethods place restrictions on what can be added to the
// table they are assigned to. It isn't enough to use a __newindex metamethod
// because it will only be called for a new addition, not a replacement. So you
// have to use it as a gatekeeper for the real table, and add that table or a
// function that accesses it to the metatable as the table's __index metamethod
// so the user-accessible table stays empty.

// __index metamethod for vars
static int script_vars_get(lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "script_vars");
	lua_rotate(L, -2, 1);
	lua_rawget(L, -2);
	return 1;
}

// __newindex metamethod for vars
static int script_vars_set(lua_State* L)
{
	if (lua_type(L, 2) != LUA_TSTRING)
	{
		return luaL_error(L, "invalid key for script variable: must be string");
	}

	int type = lua_type(L, 3);

	if (type == LUA_TNIL || type == LUA_TNUMBER || type == LUA_TBOOLEAN || type == LUA_TSTRING || type == LUA_TUSERDATA)
	{
		lua_getfield(L, LUA_REGISTRYINDEX, "script_vars");
		lua_rotate(L, -3, 1);
		lua_rawset(L, -3);
	}
	else
	{
		return luaL_error(L, "invalid type for script variable: must be nil, number, boolean, string, vector, or entity");
	}

	return 0;
}

// __newindex metamethod for persistent (doesn't need a function for __index because it will be a table)
static int script_persistent_set(lua_State* L)
{
	if (lua_type(L, 2) != LUA_TSTRING)
	{
		return luaL_error(L, "invalid key for persistent variable: must be string");
	}

	int type = lua_type(L, 3);

	if (type == LUA_TNIL || type == LUA_TNUMBER || type == LUA_TBOOLEAN || type == LUA_TSTRING)
	{
		lua_getfield(L, LUA_REGISTRYINDEX, "script_persistent");
		lua_rotate(L, -3, 1);
		lua_rawset(L, -3);
	}
	else
	{
		return luaL_error(L, "invalid type for persistent variable: must be nil, number, boolean, or string");
	}

	return 0;
}

// __newindex metamethod for read-only tables
static int script_readonly(lua_State* L)
{
	return luaL_error(L, "attempt to set a read-only value");
}

// Recursively sets read-only the table at the top of the stack, and optionally, and all tables within it
// This takes some doing since we actually need to replace the table with an empty one that has __index
// and __newindex metamethods, we can't just add a metamethod to the existing table
static void script_table_readonly(lua_State* L, bool recursive = false)
{
	// Make sure there's space since this can be recursive and puts a lot on the stack
	lua_checkstack(L, 2);

	// Walk the table to find tables within it
	if (recursive)
	{
		lua_pushnil(L);

		while (lua_next(L, -2) != 0)
		{
			int type = lua_type(L, -1);

			if (type == LUA_TTABLE)
			{
				script_table_readonly(L, true);
				lua_pushvalue(L, -2);
				lua_rotate(L, -3, 1);
				lua_rawset(L, -4);
			}
			else
			{
				lua_pop(L, 1);
			}
		}
	}

	// Set the table to read only
	lua_newtable(L);
	lua_newtable(L);
	lua_rotate(L, -3, -1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
}

// __index metamethod for globals
static int script_globals_get(lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "script_globals");
	lua_rotate(L, -2, 1);
	lua_rawget(L, -2);
	return 1;
}

// __newindex metamethod for globals during setup
static int script_globals_set(lua_State* L)
{
	if (lua_type(L, 2) != LUA_TSTRING)
	{
		return luaL_error(L, "invalid key for global variable: must be string");
	}

	lua_pushliteral(L, "script");

	// Make sure the object doesn't have the name "script"
	if (lua_rawequal(L, 2, -1))
	{
		return luaL_error(L, "name for global variable cannot be 'script'");
	}

	lua_pop(L, 1);

	// Make sure the name hasn't been used yet
	lua_getfield(L, LUA_REGISTRYINDEX, "script_globals");
	lua_pushvalue(L, 2);

	if (lua_rawget(L, -2) != LUA_TNIL)
	{
		return luaL_error(L, "global variable being added has the same name as an existing variable");
	}

	lua_pop(L, 2);

	// If it's a table, set it and all tables within it to read-only
	if (lua_type(L, 3) == LUA_TTABLE)
	{
		script_table_readonly(L, true);
	}

	// Now actually add the object to the table
	lua_getfield(L, LUA_REGISTRYINDEX, "script_globals");
	lua_rotate(L, -3, 1);
	lua_rawset(L, -3);

	return 0;
}

// =============================================================================
// Script initialization and loading
// =============================================================================

// Vector metamethods
static const luaL_Reg script_vector_metamethods[] =
{
	{"__add", script_vector_add},
	{"__sub", script_vector_sub},
	{"__mul", script_vector_mul},
	{"__unm", script_vector_neg},
	{"__index", nullptr},
	{"__newindex", script_readonly},
	{nullptr, nullptr}
};

// Vector member functions
static const luaL_Reg script_vector_functions[] =
{
	{"direction", script_vector_direction},
	{"distance", script_vector_distance},
	{"lerp", script_vector_lerp},
	{"values", script_vector_values},
	{nullptr, nullptr}
};

// Entity member functions
static const luaL_Reg script_entity_functions[] =
{
	{"get", script_entity_get},
	{"set", script_entity_set},
	{"trigger", script_entity_trigger},
	{"kill", script_entity_kill},
	{"message", script_entity_message},
	{"give", script_entity_give},
	{"setnoise", script_entity_setnoise},
	{"setstring", script_entity_setstring},
	{"damageable", script_entity_damageable},
	{"player", script_entity_player},
	{"monster", script_entity_monster},
	{"valid", script_entity_valid},
	{nullptr, nullptr}
};

// API functions
static const luaL_Reg script_functions[] =
{
	{"vector", script_vector},
	{"spawn", script_spawn},
	{"find", script_find},
	{"foreach", script_foreach},
	{"filter", script_filter},
	{"pick", script_pick},
	{"values", script_values},
	{nullptr, nullptr}
};

// Panic function which should hopefully never happen
static int script_panic(lua_State* L)
{
	const char* errstr = lua_tostring(L, -1);
	gi.Com_ErrorFmt("Script panic: {}\n", errstr);
	return 0;
}

// Lua instance has a lifetime of TAG_GAME
static lua_State* L;
static bool script_loaded;

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

	// Set panic function
	lua_atpanic(L, script_panic);

	// Create table for API and assign functions to it
	luaL_newlib(L, script_functions);

	// Add table for script variables, which get cleared when changing levels
	lua_newtable(L);
	lua_newtable(L);
	lua_pushcfunction(L, script_vars_get);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_vars_set);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "vars");

	// Add table for persistent variables, this remains valid for the lifetime of the scripting engine
	lua_newtable(L);
	lua_newtable(L);
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setfield(L, LUA_REGISTRYINDEX, "script_persistent");
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_persistent_set);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "persistent");

	// Add table for math library
	luaopen_math(L);
	script_table_readonly(L);
	lua_setfield(L, -2, "math");

	// Add table for string library
	luaopen_string(L);
	script_table_readonly(L);
	lua_setfield(L, -2, "string");

	// Add table for table library
	luaopen_table(L);
	script_table_readonly(L);
	lua_setfield(L, -2, "table");

	// Write protect API
	script_table_readonly(L);

	// Add API to the registry so it can be retrieved after a script is loaded
	lua_setfield(L, LUA_REGISTRYINDEX, "script_api");

	// Create metatable for vector objects
	luaL_newmetatable(L, "script_vector");
	luaL_setfuncs(L, script_vector_metamethods, 0);
	luaL_newlib(L, script_vector_functions);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

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
	script_loaded = false;

	// Strings are all tagged TAG_LEVEL so they're freed by now
	script_stringpool_count = 0;

	// Create the trigger stack table, or replace it with an empty one
	// If it exists already it should probably be empty by the time a level transition happens,
	// but it's probably safest to not assume that
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "script_triggerstack");

	// Clear script variables by overwriting the table with a fresh one
	// If this is the first attempt at loading a script, it doesn't exist yet
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "script_vars");

	// Clear global variables by overwriting the table with a fresh one
	lua_newtable(L);
	lua_getfield(L, LUA_REGISTRYINDEX, "script_api");
	lua_setfield(L, -2, "script");
	lua_setfield(L, LUA_REGISTRYINDEX, "script_globals");

	// May as well run a full garbage-collection cycle here
	lua_gc(L, LUA_GCCOLLECT);

	// Add a setup metatable to the global proxy table
	lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
	lua_newtable(L);
	lua_pushcfunction(L, script_globals_set);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);

	// Attempt to load the script for the current map
	cvar_t* gamedir = gi.cvar("gamedir", "", CVAR_NOFLAGS);

	if (luaL_loadfile(L, G_Fmt("./{}/scripts/{}.lua", gamedir->string, mapname).data()) != LUA_OK)
	{
		const char* errstr = lua_tostring(L, -1);
		gi.Com_PrintFmt("Error loading script for map {}: {}\n", mapname, errstr);
		lua_pop(L, 2);
		return;
	}

	// Attempt to execute the script
	if (lua_pcall(L, 0, 0, 0) != LUA_OK)
	{
		const char* errstr = lua_tostring(L, -1);
		gi.Com_PrintFmt("Error executing script for map {}: {}\n", mapname, errstr);
		lua_pop(L, 2);
		return;
	}

	// Write protect globals now that the map's functions have been added to it
	lua_newtable(L);
	lua_pushcfunction(L, script_globals_get);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, script_readonly);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_pop(L, 1);

	gi.Com_PrintFmt("Loaded script for map {}\n", mapname);

	script_loaded = true;
}

// =============================================================================
// Save support
// =============================================================================

// Script variables and persistent variables are both stored in tables and are saved or loaded the same way
// Script variables will be saved and loaded during level transitions to support crosslevel
// units, while crosslevel variables persist until a new game is started (via menu or map
// command) or a save game is loaded.

// During saving, each variable is encoded as a text string containing its type and a text
// representation of its value, separated by a colon. Booleans and strings are probably
// self-explanatory. Numbers are not marked as float or integer; their subtype is implied
// by their string representation (whether or not it contains a decimal point.) Entities are
// stored as their offset in the entity array, but entities that are invalid by the time
// the save occurs are saved with an offset of -1.

// During loading, the process is reversed, creating objects from the string representations
// and adding them to the table in question. Invalid entities will still be invalid; they will
// no longer point to the original slot, but that is entirely immaterial and all that matters
// is that they know they are invalid.

// Get variables for saving
void script_get_variables(std::unordered_map<std::string, std::string>& variables, bool persistent)
{
	// Get the specified table
	if (persistent)
	{
		lua_getfield(L, LUA_REGISTRYINDEX, "script_persistent");
	}
	else
	{
		lua_getfield(L, LUA_REGISTRYINDEX, "script_vars");
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
			luaL_getmetafield(L, -2, "__name");
			std::string_view name = lua_tostring(L, -1);

			if (name == "script_vector")
			{
				vec3_t* vector = (vec3_t*)lua_touserdata(L, -3);

				val += "vector:";

				val += std::to_string(vector->x);
				val += ",";
				val += std::to_string(vector->y);
				val += ",";
				val += std::to_string(vector->z);
			}
			else if (name == "script_entity")
			{
				struct script_ud_ent_t* ud = (script_ud_ent_t*)lua_touserdata(L, -3);

				val += "entity:";

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
				gi.Com_ErrorFmt("script attempting to save unknown uservalue type {}\n", name);
			}

			lua_pop(L, 1);
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

	lua_pop(L, 1);
}

// Set variables after loading
void script_set_variables(std::unordered_map<std::string, std::string>& variables, bool persistent)
{
	// Get the specified table
	if (persistent)
	{
		lua_getfield(L, LUA_REGISTRYINDEX, "script_persistent");
	}
	else
	{
		lua_getfield(L, LUA_REGISTRYINDEX, "script_vars");
	}

	for (auto it = variables.begin(); it != variables.end(); ++it)
	{
		std::string key = it->first;
		std::string str = it->second;

		//
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
		else if (str_type == "vector")
		{
			size_t loc1 = str_val.find_first_of(',');
			size_t loc2 = str_val.find_first_of(',', loc1 + 1);

			vec3_t vec = {};
			vec.x = std::stof(str_val.substr(0, loc1));
			vec.y = std::stof(str_val.substr(loc1 + 1, loc2));
			vec.z = std::stof(str_val.substr(loc2 + 1));

			script_push_vector(L, vec);
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

	lua_pop(L, 1);
}

// =============================================================================
// script entity
// =============================================================================

USE(script_use) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	// Make sure script has been loaded for this level
	if (!script_loaded)
	{
		gi.Com_PrintFmt("{} triggered but script not loaded\n", *self);
		return;
	}

	if (!self->script_function)
	{
		gi.Com_PrintFmt("{} has no function set\n", *self);
		return;
	}

	// Try to get a function by the given name
	lua_getglobal(L, self->script_function);

	int type = lua_type(L, -1);

	if (type != LUA_TFUNCTION)
	{
		if (type == LUA_TNIL)
		{
			gi.Com_PrintFmt("{} attempting to call nonexistent function {}\n", *self, self->script_function);
		}
		else
		{
			gi.Com_PrintFmt("{} attempting to call non-function object {} ({})\n", *self, self->script_function, lua_typename(L, type));
		}

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

void SP_script(edict_t* self)
{
	self->use = script_use;
}
