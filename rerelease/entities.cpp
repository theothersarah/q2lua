#include "g_local.h"

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

// =============================================================================
// path_track entity
// =============================================================================

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
