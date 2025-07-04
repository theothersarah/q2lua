#include "g_local.h"

// =============================================================================
// trigger_enter_level entity
// =============================================================================

// This triggers every time a level is entered, not just for the first time like a
// trigger_always. It functions like a target_crosslevel_target, including the way
// it double-dips on the delay time, but it unconditionally triggers its targets
// and doesn't delete itself.

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

// =============================================================================
// path_track entity
// =============================================================================

// This is a path_corner without the monster-related stuff, so it doesn't have a touchable
// hitbox.

// Spawnflags:
// 1 - Teleport

void SP_path_track(edict_t* self)
{
	if (!self->targetname)
	{
		gi.Com_PrintFmt("{} with no targetname\n", *self);
		G_FreeEdict(self);
		return;
	}
}

// =============================================================================
// func_mover entity
// =============================================================================

// This is a point entity that functions almost exactly like a train. It can be used for
// laser targets, etc., entities can be attached to it with the Move Teamchain spawnflag,
// and it can optionally have a visible model assigned to it.

constexpr spawnflags_t SPAWNFLAG_TRAIN_USE_ORIGIN = 32_spawnflag;

void SP_func_mover(edict_t *self)
{
	// This flag has to be set since this entity only has an origin
	self->spawnflags |= SPAWNFLAG_TRAIN_USE_ORIGIN;

	// Train stuff
	self->movetype = MOVETYPE_PUSH;

	if (!self->speed)
	{
		self->speed = 100;
	}

	self->moveinfo.speed = self->speed;
	self->moveinfo.accel = self->moveinfo.decel = self->moveinfo.speed;

	self->use = train_use;

	// If this doesn't have a target there's nothing to do
	if (self->target)
	{
		self->nextthink = level.time + FRAME_TIME_S;
		self->think = func_train_find;
	}
	else
	{
		gi.Com_PrintFmt("{}: no target\n", *self);
		G_FreeEdict(self);
	}

	// Optionally display a model
	if (self->model)
	{
		self->s.modelindex = gi.modelindex(self->model);
	}

	gi.linkentity(self);
}
