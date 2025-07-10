// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_utils.c -- misc utility functions for game module

#include "g_local.h"

/*
=============
G_Find

Searches all active entities for the next one that validates the given callback.

Searches beginning at the edict after from, or the beginning if nullptr
nullptr will be returned if the end of the list is reached.
=============
*/
edict_t *G_Find(edict_t *from, std::function<bool(edict_t *e)> matcher)
{
	if (!from)
		from = g_edicts;
	else
		from++;

	for (; from < &g_edicts[globals.num_edicts]; from++)
	{
		if (!from->inuse)
			continue;
		if (matcher(from))
			return from;
	}

	return nullptr;
}

/*
=================
findradius

Returns entities that have origins within a spherical area

findradius (origin, radius)
=================
*/
edict_t *findradius(edict_t *from, const vec3_t &org, float rad)
{
	vec3_t eorg;
	int	   j;

	if (!from)
		from = g_edicts;
	else
		from++;
	for (; from < &g_edicts[globals.num_edicts]; from++)
	{
		if (!from->inuse)
			continue;
		if (from->solid == SOLID_NOT)
			continue;
		for (j = 0; j < 3; j++)
			eorg[j] = org[j] - (from->s.origin[j] + (from->mins[j] + from->maxs[j]) * 0.5f);
		if (eorg.length() > rad)
			continue;
		return from;
	}

	return nullptr;
}

/*
=============
G_PickTarget

Searches all active entities for the next one that holds
the matching string at fieldofs in the structure.

Searches beginning at the edict after from, or the beginning if nullptr
nullptr will be returned if the end of the list is reached.

=============
*/
constexpr size_t MAXCHOICES = 8;

edict_t *G_PickTarget(const char *targetname)
{
	edict_t *ent = nullptr;
	int		 num_choices = 0;
	edict_t *choice[MAXCHOICES];

	if (!targetname)
	{
		gi.Com_Print("G_PickTarget called with nullptr targetname\n");
		return nullptr;
	}

	while (1)
	{
		ent = G_FindByString<&edict_t::targetname>(ent, targetname);
		if (!ent)
			break;
		choice[num_choices++] = ent;
		if (num_choices == MAXCHOICES)
			break;
	}

	if (!num_choices)
	{
		gi.Com_PrintFmt("G_PickTarget: target {} not found\n", targetname);
		return nullptr;
	}

	return choice[irandom(num_choices)];
}

THINK(Think_Delay) (edict_t *ent) -> void
{
	G_UseTargets(ent, ent->activator);
	G_FreeEdict(ent);
}

void G_PrintActivationMessage(edict_t *ent, edict_t *activator, bool coop_global)
{
	//
	// print the message
	//
	if ((ent->message) && !(activator->svflags & SVF_MONSTER))
	{
		if (coop_global && coop->integer)
			gi.LocBroadcast_Print(PRINT_CENTER, "{}", ent->message);
		else
			gi.LocCenter_Print(activator, "{}", ent->message);

		// [Paril-KEX] allow non-noisy centerprints
		if (ent->noise_index >= 0)
		{
			if (ent->noise_index)
				gi.sound(activator, CHAN_AUTO, ent->noise_index, 1, ATTN_NORM, 0);
			else
				gi.sound(activator, CHAN_AUTO, gi.soundindex("misc/talk1.wav"), 1, ATTN_NORM, 0);
		}
	}
}




void G_MonsterKilled(edict_t *self);

// Sarah: Moved this to a function so scripts can use it
void G_Kill(edict_t* ent)
{
	if (ent->teammaster)
	{
		// PMM - if this entity is part of a chain, cleanly remove it
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
		// [Paril-KEX] remove teammaster too
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

	// [Paril-KEX] if we killtarget a monster, clean up properly
	if (ent->svflags & SVF_MONSTER)
	{
		if (!ent->deadflag && !(ent->monsterinfo.aiflags & AI_DO_NOT_COUNT) && !(ent->spawnflags & SPAWNFLAG_MONSTER_DEAD))
			G_MonsterKilled(ent);
	}

	// PMM
	G_FreeEdict(ent);
}

/*
==============================
G_UseTargets

the global "activator" should be set to the entity that initiated the firing.

If self.delay is set, a DelayedUse entity will be created that will actually
do the SUB_UseTargets after that many seconds have passed.

Centerprints any self.message to the activator.

Search for (string)targetname in all entities that
match (string)self.target and call their .use function

==============================
*/
void G_UseTargets(edict_t *ent, edict_t *activator)
{
	edict_t *t;

	//
	// check for a delay
	//
	if (ent->delay)
	{
		// create a temp object to fire at a later time
		t = G_Spawn();
		t->classname = "DelayedUse";
		t->nextthink = level.time + gtime_t::from_sec(ent->delay);
		t->think = Think_Delay;
		t->activator = activator;
		if (!activator)
			gi.Com_Print("Think_Delay with no activator\n");
		t->message = ent->message;
		t->target = ent->target;
		t->killtarget = ent->killtarget;

		// Sarah: add script_arg to the delay temp entity
		t->script_arg = ent->script_arg;

		return;
	}

	//
	// print the message
	//
	G_PrintActivationMessage(ent, activator, true);

	//
	// kill killtargets
	//
	if (ent->killtarget)
	{
		t = nullptr;
		while ((t = G_FindByString<&edict_t::targetname>(t, ent->killtarget)))
		{
			G_Kill(t);

			if (!ent->inuse)
			{
				gi.Com_Print("entity was removed while using killtargets\n");
				return;
			}
		}
	}

	//
	// fire targets
	//
	if (ent->target)
	{
		t = nullptr;
		while ((t = G_FindByString<&edict_t::targetname>(t, ent->target)))
		{
			// doors fire area portals in a specific way
			if (!Q_strcasecmp(t->classname, "func_areaportal") &&
				(!Q_strcasecmp(ent->classname, "func_door") || !Q_strcasecmp(ent->classname, "func_door_rotating")
				|| !Q_strcasecmp(ent->classname, "func_door_secret") || !Q_strcasecmp(ent->classname, "func_water")))
				continue;

			if (t == ent)
			{
				gi.Com_Print("WARNING: Entity used itself.\n");
			}
			else
			{
				if (t->use)
					t->use(t, ent, activator);
			}
			if (!ent->inuse)
			{
				gi.Com_Print("entity was removed while using targets\n");
				return;
			}
		}
	}
}

constexpr vec3_t VEC_UP = { 0, -1, 0 };
constexpr vec3_t MOVEDIR_UP = { 0, 0, 1 };
constexpr vec3_t VEC_DOWN = { 0, -2, 0 };
constexpr vec3_t MOVEDIR_DOWN = { 0, 0, -1 };

void G_SetMovedir(vec3_t &angles, vec3_t &movedir)
{
	if (angles == VEC_UP)
	{
		movedir = MOVEDIR_UP;
	}
	else if (angles == VEC_DOWN)
	{
		movedir = MOVEDIR_DOWN;
	}
	else
	{
		AngleVectors(angles, movedir, nullptr, nullptr);
	}

	angles = {};
}

char *G_CopyString(const char *in, int32_t tag)
{
    if(!in)
        return nullptr;
    const size_t amt = strlen(in) + 1;
    char *const out = static_cast<char *>(gi.TagMalloc(amt, tag));
    Q_strlcpy(out, in, amt);
    return out;
}

void G_InitEdict(edict_t *e)
{
	// ROGUE
	// FIXME -
	//   this fixes a bug somewhere that is setting "nextthink" for an entity that has
	//   already been released.  nextthink is being set to FRAME_TIME_S after level.time,
	//   since freetime = nextthink - FRAME_TIME_S
	if (e->nextthink)
		e->nextthink = 0_ms;
	// ROGUE

	e->inuse = true;
	e->sv.init = false;
	e->classname = "noclass";
	e->gravity = 1.0;
	e->s.number = e - g_edicts;

	// PGM - do this before calling the spawn function so it can be overridden.
	e->gravityVector[0] = 0.0;
	e->gravityVector[1] = 0.0;
	e->gravityVector[2] = -1.0;
	// PGM
}

/* Sarah
=================
G_Spawn_Reset

Originally, spawning did a linear search from the start of the entity list every time an entity was spawned.
G_Spawn and G_FreeEdict were adjusted to use a bookmark system to limit the number of entities searched
when spawning new entities.

It keeps the position of the last entity that was spawned and searches from there when the next entity is
spawned. When an entity is freed, it keeps track of what index it was in and the time it was freed, and
this is used when spawning an entity later to determine where the search should start from. If the entity
was freed more than 500 milliseconds ago, it just uses that slot immediately without searching.

During the initial spawning of entities at map start, no searching needs to be done at all. It will either
use the slot of an entity that chose to free itself during spawning, or it will expand the entity list.
=================
*/

static uint32_t spawn_pos;

static bool despawn_valid;
static gtime_t despawn_time;
static uint32_t despawn_pos;

// Sarah: Reset spawn bookmarks
void G_Spawn_Reset()
{
	spawn_pos = game.maxclients;
	despawn_valid = false;
}

/*
=================
G_Spawn

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
edict_t *G_Spawn()
{
	edict_t *e;

	//
	bool early = (level.time < 2_sec);

	if (despawn_valid && (early || level.time - despawn_time > 500_ms))
	{
		despawn_valid = false;

		if (spawn_pos > despawn_pos)
		{
			spawn_pos = despawn_pos;
		}

		// We know the index of a valid slot so we may as well use it right now
		//gi.Com_PrintFmt("G_Spawn immediately using slot {}\n", despawn_pos);
		e = &g_edicts[despawn_pos];
		G_InitEdict(e);
		return e;
	}
	else
	{
		spawn_pos++;
	}
	//gi.Com_PrintFmt("G_Spawn resuming from slot {}\n", spawn_pos);
	for (e = &g_edicts[spawn_pos]; spawn_pos < globals.num_edicts; spawn_pos++, e++)
	{
		if (!e->inuse)
		{
			if (early || level.time - e->freetime > 500_ms)
			{
				//gi.Com_PrintFmt("G_Spawn using slot {}\n", spawn_pos);
				G_InitEdict(e);
				return e;
			}
			else if (!despawn_valid)
			{
				despawn_pos = spawn_pos;
				despawn_time = e->freetime;
				despawn_valid = true;
			}
		}
	}

	if (spawn_pos == game.maxentities)
		gi.Com_Error("ED_Alloc: no free edicts");

	globals.num_edicts++;
	G_InitEdict(e);
	return e;
}

/*
=================
G_FreeEdict

Marks the edict as free
=================
*/
THINK(G_FreeEdict) (edict_t *ed) -> void
{
	// already freed
	if (!ed->inuse)
		return;

	gi.unlinkentity(ed); // unlink from world

	if ((ed - g_edicts) <= (ptrdiff_t) (game.maxclients + BODY_QUEUE_SIZE))
	{
#ifdef _DEBUG
		gi.Com_Print("tried to free special edict\n");
#endif
		return;
	}

	gi.Bot_UnRegisterEdict( ed );

	int32_t id = ed->spawn_count + 1;
	memset(ed, 0, sizeof(*ed));
	ed->s.number = ed - g_edicts;
	ed->classname = "freed";
	ed->freetime = level.time;
	ed->inuse = false;
	ed->spawn_count = id;
	ed->sv.init = false;

	// Sarah: Adjust despawn bookmarks and timestamps
	uint32_t pos = ed - g_edicts;

	//gi.Com_PrintFmt("G_FreeEdict freeing slot {}\n", pos);

	if (despawn_valid)
	{
		// Push back the despawn bookmark if the slot is earlier than it in the array
		if (pos < despawn_pos)
		{
			despawn_pos = pos;
			despawn_time = level.time;
		}
	}
	else
	{
		// Set a bookmark
		despawn_pos = pos;
		despawn_time = level.time;
		despawn_valid = true;
	}
}

BoxEdictsResult_t G_TouchTriggers_BoxFilter(edict_t *hit, void *)
{
	if (!hit->touch)
		return BoxEdictsResult_t::Skip;

	return BoxEdictsResult_t::Keep;
}

/*
============
G_TouchTriggers

============
*/
void G_TouchTriggers(edict_t *ent)
{
	int		 i, num;
	static edict_t *touch[MAX_EDICTS];
	edict_t *hit;

	// dead things don't activate triggers!
	if ((ent->client || (ent->svflags & SVF_MONSTER)) && (ent->health <= 0))
		return;

	num = gi.BoxEdicts(ent->absmin, ent->absmax, touch, MAX_EDICTS, AREA_TRIGGERS, G_TouchTriggers_BoxFilter, nullptr);

	// be careful, it is possible to have an entity in this
	// list removed before we get to it (killtriggered)
	for (i = 0; i < num; i++)
	{
		hit = touch[i];
		if (!hit->inuse)
			continue;
		if (!hit->touch)
			continue;
		hit->touch(hit, ent, null_trace, true);
	}
}

// [Paril-KEX] scan for projectiles between our movement positions
// to see if we need to collide against them
void G_TouchProjectiles(edict_t *ent, vec3_t previous_origin)
{
	struct skipped_projectile
	{
		edict_t		*projectile;
		int32_t		spawn_count;
	};
	// a bit ugly, but we'll store projectiles we are ignoring here.
	static std::vector<skipped_projectile> skipped;

	while (true)
	{
		trace_t tr = gi.trace(previous_origin, ent->mins, ent->maxs, ent->s.origin, ent, ent->clipmask | CONTENTS_PROJECTILE);

		if (tr.fraction == 1.0f)
			break;
		else if (!(tr.ent->svflags & SVF_PROJECTILE))
			break;

		// always skip this projectile since certain conditions may cause the projectile
		// to not disappear immediately
		tr.ent->svflags &= ~SVF_PROJECTILE;
		skipped.push_back({ tr.ent, tr.ent->spawn_count });

		// if we're both players and it's coop, allow the projectile to "pass" through
		if (ent->client && tr.ent->owner && tr.ent->owner->client && !G_ShouldPlayersCollide(true))
			continue;

		G_Impact(ent, tr);
	}

	for (auto &skip : skipped)
		if (skip.projectile->inuse && skip.projectile->spawn_count == skip.spawn_count)
			skip.projectile->svflags |= SVF_PROJECTILE;

	skipped.clear();
}

/*
==============================================================================

Kill box

==============================================================================
*/

/*
=================
KillBox

Kills all entities that would touch the proposed new positioning
of ent.
=================
*/

BoxEdictsResult_t KillBox_BoxFilter(edict_t *hit, void *)
{
	if (!hit->solid || !hit->takedamage || hit->solid == SOLID_TRIGGER)
		return BoxEdictsResult_t::Skip;

	return BoxEdictsResult_t::Keep;
}

bool KillBox(edict_t *ent, bool from_spawning, mod_id_t mod, bool bsp_clipping)
{
	// don't telefrag as spectator...
	if (ent->movetype == MOVETYPE_NOCLIP)
		return true;

	contents_t mask = CONTENTS_MONSTER | CONTENTS_PLAYER;

	// [Paril-KEX] don't gib other players in coop if we're not colliding
	if (from_spawning && ent->client && coop->integer && !G_ShouldPlayersCollide(false))
		mask &= ~CONTENTS_PLAYER;

	int		 i, num;
	static edict_t *touch[MAX_EDICTS];
	edict_t *hit;

	num = gi.BoxEdicts(ent->absmin, ent->absmax, touch, MAX_EDICTS, AREA_SOLID, KillBox_BoxFilter, nullptr);

	for (i = 0; i < num; i++)
	{
		hit = touch[i];

		if (hit == ent)
			continue;
		else if (!hit->inuse || !hit->takedamage || !hit->solid || hit->solid == SOLID_TRIGGER || hit->solid == SOLID_BSP)
			continue;
		else if (hit->client && !(mask & CONTENTS_PLAYER))
			continue;

		if ((ent->solid == SOLID_BSP || (ent->svflags & SVF_HULL)) && bsp_clipping)
		{
			trace_t clip = gi.clip(ent, hit->s.origin, hit->mins, hit->maxs, hit->s.origin, G_GetClipMask(hit));

			if (clip.fraction == 1.0f)
				continue;
		}

		// [Paril-KEX] don't allow telefragging of friends in coop.
		// the player that is about to be telefragged will have collision
		// disabled until another time.
		if (ent->client && hit->client && coop->integer)
		{
			hit->clipmask &= ~CONTENTS_PLAYER;
			ent->clipmask &= ~CONTENTS_PLAYER;
			continue;
		}

		T_Damage(hit, ent, ent, vec3_origin, ent->s.origin, vec3_origin, 100000, 0, DAMAGE_NO_PROTECTION, mod);
	}

	return true; // all clear
}
