/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2002 EQEMu Development Team (http://eqemu.org)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/
#include "../common/debug.h"
#include <iostream>
#include <string>
#include <cctype>
#include <math.h>
#include "../common/moremath.h"
#include <stdio.h>
#include "../common/packet_dump_file.h"
#include "zone.h"
#ifdef _WINDOWS
#define snprintf	_snprintf
#define strncasecmp	_strnicmp
#define strcasecmp	_stricmp
#else
#include <stdlib.h>
#include <pthread.h>
#endif

#include "npc.h"
#include "map.h"
#include "entity.h"
#include "masterentity.h"
#include "../common/spdat.h"
#include "../common/bodytypes.h"
#include "spawngroup.h"
#include "../common/misc_functions.h"
#include "../common/string_util.h"
#include "../common/rulesys.h"
#include "string_ids.h"

//#define SPELLQUEUE //Use only if you want to be spammed by spell testing


extern Zone* zone;
extern volatile bool ZoneLoaded;
extern EntityList entity_list;

#include "quest_parser_collection.h"

NPC::NPC(const NPCType* d, Spawn2* in_respawn, float x, float y, float z, float heading, int iflymode, bool IsCorpse)
: Mob(d->name,
		d->lastname,
		d->max_hp,
		d->max_hp,
		d->gender,
		d->race,
		d->class_,
		(bodyType)d->bodytype,
		d->deity,
		d->level,
		d->npc_id,
		d->size,
		d->runspeed,
		heading,
		x,
		y,
		z,
		d->light,
		d->texture,
		d->helmtexture,
		d->AC,
		d->ATK,
		d->STR,
		d->STA,
		d->DEX,
		d->AGI,
		d->INT,
		d->WIS,
		d->CHA,
		d->haircolor,
		d->beardcolor,
		d->eyecolor1,
		d->eyecolor2,
		d->hairstyle,
		d->luclinface,
		d->beard,
		(uint32*)d->armor_tint,
		0,
		d->see_invis,			// pass see_invis/see_ivu flags to mob constructor
		d->see_invis_undead,
		d->see_hide,
		d->see_improved_hide,
		d->hp_regen,
		d->mana_regen,
		d->qglobal,
		d->maxlevel,
		d->scalerate ),
	attacked_timer(CombatEventTimer_expire),
	swarm_timer(100),
	classattack_timer(1000),
	knightattack_timer(1000),
	assist_timer(AIassistcheck_delay),
	qglobal_purge_timer(30000),
	sendhpupdate_timer(1000),
	enraged_timer(1000),
	taunt_timer(TauntReuseTime * 1000)
{
	//What is the point of this, since the names get mangled..
	Mob* mob = entity_list.GetMob(name);
	if(mob != 0)
		entity_list.RemoveEntity(mob->GetID());

	int moblevel=GetLevel();

	NPCTypedata = d;
	NPCTypedata_ours = nullptr;
	respawn2 = in_respawn;
	swarm_timer.Disable();

	taunting = false;
	proximity = nullptr;
	copper = 0;
	silver = 0;
	gold = 0;
	platinum = 0;
	max_dmg = d->max_dmg;
	min_dmg = d->min_dmg;
	attack_count = d->attack_count;
	grid = 0;
	wp_m = 0;
	max_wp=0;
	save_wp = 0;
	spawn_group = 0;
	swarmInfoPtr = nullptr;
	spellscale = d->spellscale;
	healscale = d->healscale;

	logging_enabled = NPC_DEFAULT_LOGGING_ENABLED;

	pAggroRange = d->aggroradius;
	pAssistRange = d->assistradius;
	findable = d->findable;
	trackable = d->trackable;

	MR = d->MR;
	CR = d->CR;
	DR = d->DR;
	FR = d->FR;
	PR = d->PR;
	Corrup = d->Corrup;
	PhR = d->PhR;

	STR = d->STR;
	STA = d->STA;
	AGI = d->AGI;
	DEX = d->DEX;
	INT = d->INT;
	WIS = d->WIS;
	CHA = d->CHA;
	npc_mana = d->Mana;

	//quick fix of ordering if they screwed it up in the DB
	if(max_dmg < min_dmg) {
		int tmp = min_dmg;
		min_dmg = max_dmg;
		max_dmg = tmp;
	}

	// Max Level and Stat Scaling if maxlevel is set
	if(maxlevel > level)
	{
		LevelScale();
	}

	// Set Resists if they are 0 in the DB
	CalcNPCResists();

	// Set Mana and HP Regen Rates if they are 0 in the DB
	CalcNPCRegen();

	// Set Min and Max Damage if they are 0 in the DB
	if(max_dmg == 0){
		CalcNPCDamage();
	}

	accuracy_rating = d->accuracy_rating;
	avoidance_rating = d->avoidance_rating;
	ATK = d->ATK;

	CalcMaxMana();
	SetMana(GetMaxMana());

	MerchantType = d->merchanttype;
	merchant_open = GetClass() == MERCHANT;
	org_x = x;
	org_y = y;
	org_z = z;
	flymode = iflymode;
	guard_x = -1;	//just some value we might be able to recongize as "unset"
	guard_y = -1;
	guard_z = -1;
	guard_heading = 0;
	guard_anim = eaStanding;
	roambox_distance = 0;
	roambox_max_x = -2;
	roambox_max_y = -2;
	roambox_min_x = -2;
	roambox_min_y = -2;
	roambox_movingto_x = -2;
	roambox_movingto_y = -2;
	roambox_min_delay = 1000;
	roambox_delay = 1000;
	org_heading = heading;
	p_depop = false;
	loottable_id = d->loottable_id;

	no_target_hotkey = d->no_target_hotkey;

	primary_faction = 0;
	SetNPCFactionID(d->npc_faction_id);
	SetPreCharmNPCFactionID(d->npc_faction_id);

	npc_spells_id = 0;
	HasAISpell = false;
	HasAISpellEffects = false;

	SpellFocusDMG = 0;
	SpellFocusHeal = 0;

	pet_spell_id = 0;

	delaytimer = false;
	combat_event = false;
	attack_delay = d->attack_delay;
	slow_mitigation = d->slow_mitigation;

	EntityList::RemoveNumbers(name);
	entity_list.MakeNameUnique(name);

	npc_aggro = d->npc_aggro;

	AI_Start();

	d_meele_texture1 = d->d_meele_texture1;
	d_meele_texture2 = d->d_meele_texture2;
	ammo_idfile = d->ammo_idfile;
	memset(equipment, 0, sizeof(equipment));
	prim_melee_type = d->prim_melee_type;
	sec_melee_type = d->sec_melee_type;
	ranged_type = d->ranged_type;

	// If Melee Textures are not set, set attack type to Hand to Hand as default
	if(!d_meele_texture1)
		prim_melee_type = 28;
	if(!d_meele_texture2)
		sec_melee_type = 28;

	//give NPCs skill values...
	int r;
	for(r = 0; r <= HIGHEST_SKILL; r++) {
		skills[r] = database.GetSkillCap(GetClass(),(SkillUseTypes)r,moblevel);
	}

	reface_timer = new Timer(15000);
	reface_timer->Disable();
	qGlobals = nullptr;
	guard_x_saved = 0;
	guard_y_saved = 0;
	guard_z_saved = 0;
	guard_heading_saved = 0;
	SetEmoteID(d->emoteid);
	SetWalkSpeed(d->walkspeed);

	InitializeBuffSlots();
	CalcBonuses();
	raid_target = d->raid_target;
}

NPC::~NPC()
{
	AI_Stop();

	if(proximity != nullptr) {
		entity_list.RemoveProximity(GetID());
		safe_delete(proximity);
	}

	safe_delete(NPCTypedata_ours);

	{
	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		safe_delete(item);
	}
	itemlist.clear();
	}

	{
	std::list<struct NPCFaction*>::iterator cur,end;
	cur = faction_list.begin();
	end = faction_list.end();
	for(; cur != end; ++cur) {
		struct NPCFaction* fac = *cur;
		safe_delete(fac);
	}
	faction_list.clear();
	}

	safe_delete(reface_timer);
	safe_delete(swarmInfoPtr);
	safe_delete(qGlobals);
	UninitializeBuffSlots();
}

void NPC::SetTarget(Mob* mob) {
	if(mob == GetTarget())		//dont bother if they are allready our target
		return;

	//This is not the default behavior for swarm pets, must be specified from quest functions or rules value.
	if(GetSwarmInfo() && GetSwarmInfo()->target && GetTarget() && (GetTarget()->GetHP() > 0)) {
		Mob *targ = entity_list.GetMob(GetSwarmInfo()->target);
		if(targ != mob){
			return;
		}
	}

	if (mob) {
		SetAttackTimer();
	} else {
		ranged_timer.Disable();
		//attack_timer.Disable();
		attack_dw_timer.Disable();
	}
	Mob::SetTarget(mob);
}

ServerLootItem_Struct* NPC::GetItem(int slot_id) {
	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		if (item->equip_slot == slot_id) {
			return item;
		}
	}
	return(nullptr);
}

void NPC::RemoveItem(uint32 item_id, uint16 quantity, uint16 slot) {
	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		if (item->item_id == item_id && slot <= 0 && quantity <= 0) {
			itemlist.erase(cur);
			return;
		}
		else if (item->item_id == item_id && item->equip_slot == slot && quantity >= 1) {
			//std::cout<<"NPC::RemoveItem"<<" equipSlot:"<<iterator.GetData()->equipSlot<<" quantity:"<< quantity<<std::endl; // iterator undefined [CODEBUG]
			if (item->charges <= quantity)
				itemlist.erase(cur);
			else
				item->charges -= quantity;
			return;
		}
	}
}

void NPC::CheckMinMaxLevel(Mob *them)
{
	if(them == nullptr || !them->IsClient())
		return;

	uint16 themlevel = them->GetLevel();
	uint8 material;

	std::list<ServerLootItem_Struct*>::iterator cur = itemlist.begin();
	while(cur != itemlist.end())
	{
		if(!(*cur))
			return;

		if(themlevel < (*cur)->min_level || themlevel > (*cur)->max_level)
		{
			material = Inventory::CalcMaterialFromSlot((*cur)->equip_slot);
			if(material != 0xFF)
				SendWearChange(material);

			cur = itemlist.erase(cur);
			continue;
		}
		++cur;
	}

}

void NPC::ClearItemList() {
	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		safe_delete(item);
	}
	itemlist.clear();
}

void NPC::QueryLoot(Client* to) {
	int x = 0;
	to->Message(0, "Coin: %ip %ig %is %ic", platinum, gold, silver, copper);

	ItemList::iterator cur, end;
	cur = itemlist.begin();
	end = itemlist.end();
	for (; cur != end; ++cur) {
		const Item_Struct* item = database.GetItem((*cur)->item_id);
		if (item)
		{
			static char itemid[7];
			sprintf(itemid, "%06d", item->ID);
			to->Message(0, "(%i:%i) minlvl: %i maxlvl: %i %i: %c%c%s%s%c", (*cur)->equip_slot, (*cur)->lootslot, (*cur)->min_level, (*cur)->max_level, (int)item->ID, 0x12, 0x30, itemid, item->Name, 0x12);
		}
		else
			LogFile->write(EQEMuLog::Error, "Database error, invalid item");
		x++;
	}
	to->Message(0, "%i items on %s.", x, GetName());
}

void NPC::AddCash(uint16 in_copper, uint16 in_silver, uint16 in_gold, uint16 in_platinum) {
	if(in_copper >= 0)
		copper = in_copper;
	else
		copper = 0;

	if(in_silver >= 0)
		silver = in_silver;
	else
		silver = 0;

	if(in_gold >= 0)
		gold = in_gold;
	else
		gold = 0;

	if(in_platinum >= 0)
		platinum = in_platinum;
	else
		platinum = 0;
}

void NPC::AddCash() {
	copper = zone->random.Int(1, 100);
	silver = zone->random.Int(1, 50);
	gold = zone->random.Int(1, 10);
	platinum = zone->random.Int(1, 5);
}

void NPC::RemoveCash() {
	copper = 0;
	silver = 0;
	gold = 0;
	platinum = 0;
}

bool NPC::Process()
{
	if (IsStunned() && stunned_timer.Check())
	{
		this->stunned = false;
		this->stunned_timer.Disable();
		this->spun_timer.Disable();
	}

	if (p_depop)
	{
		Mob* owner = entity_list.GetMob(this->ownerid);
		if (owner != 0)
		{
			//if(GetBodyType() != BT_SwarmPet)
			// owner->SetPetID(0);
			this->ownerid = 0;
			this->petid = 0;
		}
		return false;
	}

	SpellProcess();

	if(tic_timer.Check())
	{
		BuffProcess();

		if(curfp)
			ProcessFlee();

		uint32 bonus = 0;

		if(GetAppearance() == eaSitting)
			bonus+=3;

		int32 OOCRegen = 0;
		if(oocregen > 0){ //should pull from Mob class
			OOCRegen += GetMaxHP() * oocregen / 100;
			}
		//Lieka Edit:Fixing NPC regen.NPCs should regen to full during a set duration, not based on their HPs.Increase NPC's HPs by % of total HPs / tick.
		if((GetHP() < GetMaxHP()) && !IsPet()) {
			if(!IsEngaged()) {//NPC out of combat
				if(GetNPCHPRegen() > OOCRegen)
					SetHP(GetHP() + GetNPCHPRegen());
				else
					SetHP(GetHP() + OOCRegen);
			} else
				SetHP(GetHP()+GetNPCHPRegen());
		} else if(GetHP() < GetMaxHP() && GetOwnerID() !=0) {
			if(!IsEngaged()) //pet
				SetHP(GetHP()+GetNPCHPRegen()+bonus+(GetLevel()/5));
			else
				SetHP(GetHP()+GetNPCHPRegen()+bonus);
		} else
			SetHP(GetHP()+GetNPCHPRegen());

		if(GetMana() < GetMaxMana()) {
			SetMana(GetMana()+mana_regen+bonus);
		}
	}

	if (sendhpupdate_timer.Check() && (IsTargeted() || (IsPet() && GetOwner() && GetOwner()->IsClient()))) {
		if(!IsFullHP || cur_hp<max_hp){
			SendHPUpdate();
		}
	}

	if(HasVirus()) {
		if(viral_timer.Check()) {
			viral_timer_counter++;
			for(int i = 0; i < MAX_SPELL_TRIGGER*2; i+=2) {
				if(viral_spells[i])	{
					if(viral_timer_counter % spells[viral_spells[i]].viral_timer == 0) {
						SpreadVirus(viral_spells[i], viral_spells[i+1]);
					}
				}
			}
		}
		if(viral_timer_counter > 999)
			viral_timer_counter = 0;
	}

	if(spellbonuses.GravityEffect == 1) {
		if(gravity_timer.Check())
			DoGravityEffect();
	}

	if(reface_timer->Check() && !IsEngaged() && (guard_x == GetX() && guard_y == GetY() && guard_z == GetZ())) {
		SetHeading(guard_heading);
		SendPosition();
		reface_timer->Disable();
	}

	if (IsMezzed())
		return true;

	if(IsStunned()) {
		if(spun_timer.Check())
			Spin();
		return true;
	}

	if (enraged_timer.Check()){
		ProcessEnrage();
	}

	//Handle assists...
	if(assist_timer.Check() && IsEngaged() && !Charmed()) {
		entity_list.AIYellForHelp(this, GetTarget());
	}

	if(qGlobals)
	{
		if(qglobal_purge_timer.Check())
		{
			qGlobals->PurgeExpiredGlobals();
		}
	}

	AI_Process();

	return true;
}

uint32 NPC::CountLoot() {
	return(itemlist.size());
}

void NPC::Depop(bool StartSpawnTimer) {
	uint16 emoteid = this->GetEmoteID();
	if(emoteid != 0)
		this->DoNPCEmote(ONDESPAWN,emoteid);
	p_depop = true;
	if (StartSpawnTimer) {
		if (respawn2 != 0) {
			respawn2->DeathReset();
		}
	}
}

bool NPC::DatabaseCastAccepted(int spell_id) {
	for (int i=0; i < 12; i++) {
		switch(spells[spell_id].effectid[i]) {
		case SE_Stamina: {
			if(IsEngaged() && GetHPRatio() < 100)
				return true;
			else
				return false;
			break;
		}
		case SE_CurrentHPOnce:
		case SE_CurrentHP: {
			if(this->GetHPRatio() < 100 && spells[spell_id].buffduration == 0)
				return true;
			else
				return false;
			break;
		}

		case SE_HealOverTime: {
			if(this->GetHPRatio() < 100)
				return true;
			else
				return false;
			break;
		}
		case SE_DamageShield: {
			return true;
		}
		case SE_NecPet:
		case SE_SummonPet: {
			if(GetPet()){
#ifdef SPELLQUEUE
				printf("%s: Attempted to make a second pet, denied.\n",GetName());
#endif
				return false;
			}
			break;
		}
		case SE_LocateCorpse:
		case SE_SummonCorpse: {
			return false; //Pfft, npcs don't need to summon corpses/locate corpses!
			break;
		}
		default:
			if(spells[spell_id].goodEffect == 1 && !(spells[spell_id].buffduration == 0 && this->GetHPRatio() == 100) && !IsEngaged())
				return true;
			return false;
		}
	}
	return false;
}

NPC* NPC::SpawnNPC(const char* spawncommand, float in_x, float in_y, float in_z, float in_heading, Client* client) {
	if(spawncommand == 0 || spawncommand[0] == 0) {
		return 0;
	}
	else {
		Seperator sep(spawncommand);
		//Lets see if someone didn't fill out the whole #spawn function properly
		if (!sep.IsNumber(1))
			sprintf(sep.arg[1],"1");
		if (!sep.IsNumber(2))
			sprintf(sep.arg[2],"1");
		if (!sep.IsNumber(3))
			sprintf(sep.arg[3],"0");
		if (atoi(sep.arg[4]) > 2100000000 || atoi(sep.arg[4]) <= 0)
			sprintf(sep.arg[4]," ");
		if (!strcmp(sep.arg[5],"-"))
			sprintf(sep.arg[5]," ");
		if (!sep.IsNumber(5))
			sprintf(sep.arg[5]," ");
		if (!sep.IsNumber(6))
			sprintf(sep.arg[6],"1");
		if (!sep.IsNumber(8))
			sprintf(sep.arg[8],"0");
		if (!sep.IsNumber(9))
			sprintf(sep.arg[9], "0");
		if (!sep.IsNumber(7))
			sprintf(sep.arg[7],"0");
		if (!strcmp(sep.arg[4],"-"))
			sprintf(sep.arg[4]," ");
		if (!sep.IsNumber(10))	// bodytype
			sprintf(sep.arg[10], "0");
		//Calc MaxHP if client neglected to enter it...
		if (!sep.IsNumber(4)) {
			//Stolen from Client::GetMaxHP...
			uint8 multiplier = 0;
			int tmplevel = atoi(sep.arg[2]);
			switch(atoi(sep.arg[5]))
			{
			case WARRIOR:
				if (tmplevel < 20)
					multiplier = 22;
				else if (tmplevel < 30)
					multiplier = 23;
				else if (tmplevel < 40)
					multiplier = 25;
				else if (tmplevel < 53)
					multiplier = 27;
				else if (tmplevel < 57)
					multiplier = 28;
				else
					multiplier = 30;
				break;

			case DRUID:
			case CLERIC:
			case SHAMAN:
				multiplier = 15;
				break;

			case PALADIN:
			case SHADOWKNIGHT:
				if (tmplevel < 35)
					multiplier = 21;
				else if (tmplevel < 45)
					multiplier = 22;
				else if (tmplevel < 51)
					multiplier = 23;
				else if (tmplevel < 56)
					multiplier = 24;
				else if (tmplevel < 60)
					multiplier = 25;
				else
					multiplier = 26;
				break;

			case MONK:
			case BARD:
			case ROGUE:
			//case BEASTLORD:
				if (tmplevel < 51)
					multiplier = 18;
				else if (tmplevel < 58)
					multiplier = 19;
				else
					multiplier = 20;
				break;

			case RANGER:
				if (tmplevel < 58)
					multiplier = 20;
				else
					multiplier = 21;
				break;

			case MAGICIAN:
			case WIZARD:
			case NECROMANCER:
			case ENCHANTER:
				multiplier = 12;
				break;

			default:
				if (tmplevel < 35)
					multiplier = 21;
				else if (tmplevel < 45)
					multiplier = 22;
				else if (tmplevel < 51)
					multiplier = 23;
				else if (tmplevel < 56)
					multiplier = 24;
				else if (tmplevel < 60)
					multiplier = 25;
				else
					multiplier = 26;
				break;
			}
			sprintf(sep.arg[4],"%i",5+multiplier*atoi(sep.arg[2])+multiplier*atoi(sep.arg[2])*75/300);
		}

		// Autoselect NPC Gender
		if (sep.arg[5][0] == 0) {
			sprintf(sep.arg[5], "%i", (int) Mob::GetDefaultGender(atoi(sep.arg[1])));
		}

		//Time to create the NPC!!
		NPCType* npc_type = new NPCType;
		memset(npc_type, 0, sizeof(NPCType));

		strncpy(npc_type->name, sep.arg[0], 60);
		npc_type->cur_hp = atoi(sep.arg[4]);
		npc_type->max_hp = atoi(sep.arg[4]);
		npc_type->race = atoi(sep.arg[1]);
		npc_type->gender = atoi(sep.arg[5]);
		npc_type->class_ = atoi(sep.arg[6]);
		npc_type->deity = 1;
		npc_type->level = atoi(sep.arg[2]);
		npc_type->npc_id = 0;
		npc_type->loottable_id = 0;
		npc_type->texture = atoi(sep.arg[3]);
		npc_type->light = 0;
		npc_type->runspeed = 1.25;
		npc_type->d_meele_texture1 = atoi(sep.arg[7]);
		npc_type->d_meele_texture2 = atoi(sep.arg[8]);
		npc_type->merchanttype = atoi(sep.arg[9]);
		npc_type->bodytype = atoi(sep.arg[10]);

		npc_type->STR = 150;
		npc_type->STA = 150;
		npc_type->DEX = 150;
		npc_type->AGI = 150;
		npc_type->INT = 150;
		npc_type->WIS = 150;
		npc_type->CHA = 150;

		npc_type->attack_delay = 30;

		npc_type->prim_melee_type = 28;
		npc_type->sec_melee_type = 28;

		NPC* npc = new NPC(npc_type, 0, in_x, in_y, in_z, in_heading/8, FlyMode3);
		npc->GiveNPCTypeData(npc_type);

		entity_list.AddNPC(npc);

		if (client) {
			// Notify client of spawn data
			client->Message(0, "New spawn:");
			client->Message(0, "Name: %s", npc->name);
			client->Message(0, "Race: %u", npc->race);
			client->Message(0, "Level: %u", npc->level);
			client->Message(0, "Material: %u", npc->texture);
			client->Message(0, "Current/Max HP: %i", npc->max_hp);
			client->Message(0, "Gender: %u", npc->gender);
			client->Message(0, "Class: %u", npc->class_);
			client->Message(0, "Weapon Item Number: %u/%u", npc->d_meele_texture1, npc->d_meele_texture2);
			client->Message(0, "MerchantID: %u", npc->MerchantType);
			client->Message(0, "Bodytype: %u", npc->bodytype);
		}

		return npc;
	}
}

uint32 ZoneDatabase::CreateNewNPCCommand(const char* zone, uint32 zone_version,Client *client, NPC* spawn, uint32 extra) {

    uint32 npc_type_id = 0;

	if (extra && client && client->GetZoneID())
	{
		// Set an npc_type ID within the standard range for the current zone if possible (zone_id * 1000)
		int starting_npc_id = client->GetZoneID() * 1000;

		std::string query = StringFormat("SELECT MAX(id) FROM npc_types WHERE id >= %i AND id < %i",
                                        starting_npc_id, starting_npc_id + 1000);
        auto results = QueryDatabase(query);
		if (results.Success()) {
            if (results.RowCount() != 0)
			{
                auto row = results.begin();
                npc_type_id = atoi(row[0]) + 1;
                // Prevent the npc_type id from exceeding the range for this zone
                if (npc_type_id >= (starting_npc_id + 1000))
                    npc_type_id = 0;
            }
            else // No npc_type IDs set in this range yet
                npc_type_id = starting_npc_id;
        }
    }

	char tmpstr[64];
	EntityList::RemoveNumbers(strn0cpy(tmpstr, spawn->GetName(), sizeof(tmpstr)));
	std::string query;
	if (npc_type_id)
	{
        query = StringFormat("INSERT INTO npc_types (id, name, level, race, class, hp, gender, "
                                        "texture, helmtexture, size, loottable_id, merchant_id, face, "
                                        "runspeed, prim_melee_type, sec_melee_type) "
                                        "VALUES(%i, \"%s\" , %i, %i, %i, %i, %i, %i, %i, %f, %i, %i, %i, %f, %i, %i)",
                                        npc_type_id, tmpstr, spawn->GetLevel(), spawn->GetRace(), spawn->GetClass(),
                                        spawn->GetMaxHP(), spawn->GetGender(), spawn->GetTexture(),
                                        spawn->GetHelmTexture(), spawn->GetSize(), spawn->GetLoottableID(),
                                        spawn->MerchantType, 0, spawn->GetRunspeed(), 28, 28);
        auto results = QueryDatabase(query);
		if (!results.Success()) {
			LogFile->write(EQEMuLog::Error, "NPCSpawnDB Error: %s %s", query.c_str(), results.ErrorMessage().c_str());
			return false;
		}
		npc_type_id = results.LastInsertedID();
	}
	else
	{
        query = StringFormat("INSERT INTO npc_types (name, level, race, class, hp, gender, "
                                        "texture, helmtexture, size, loottable_id, merchant_id, face, "
                                        "runspeed, prim_melee_type, sec_melee_type) "
                                        "VALUES(\"%s\", %i, %i, %i, %i, %i, %i, %i, %f, %i, %i, %i, %f, %i, %i)",
                                        tmpstr, spawn->GetLevel(), spawn->GetRace(), spawn->GetClass(),
                                        spawn->GetMaxHP(), spawn->GetGender(), spawn->GetTexture(),
                                        spawn->GetHelmTexture(), spawn->GetSize(), spawn->GetLoottableID(),
                                        spawn->MerchantType, 0, spawn->GetRunspeed(), 28, 28);
        auto results = QueryDatabase(query);
		if (!results.Success()) {
			LogFile->write(EQEMuLog::Error, "NPCSpawnDB Error: %s %s", query.c_str(), results.ErrorMessage().c_str());
			return false;
		}
		npc_type_id = results.LastInsertedID();
	}

	if(client)
        client->LogSQL(query.c_str());

	query = StringFormat("INSERT INTO spawngroup (id, name) VALUES(%i, '%s-%s')", 0, zone, spawn->GetName());
    auto results = QueryDatabase(query);
	if (!results.Success()) {
		LogFile->write(EQEMuLog::Error, "NPCSpawnDB Error: %s %s", query.c_str(), results.ErrorMessage().c_str());
		return false;
	}
    uint32 spawngroupid = results.LastInsertedID();

	if(client)
        client->LogSQL(query.c_str());

    query = StringFormat("INSERT INTO spawn2 (zone, version, x, y, z, respawntime, heading, spawngroupID) "
                        "VALUES('%s', %u, %f, %f, %f, %i, %f, %i)",
                        zone, zone_version, spawn->GetX(), spawn->GetY(), spawn->GetZ(), 1200,
                        spawn->GetHeading(), spawngroupid);
    results = QueryDatabase(query);
	if (!results.Success()) {
		LogFile->write(EQEMuLog::Error, "NPCSpawnDB Error: %s %s", query.c_str(), results.ErrorMessage().c_str());
		return false;
	}

	if(client)
        client->LogSQL(query.c_str());

    query = StringFormat("INSERT INTO spawnentry (spawngroupID, npcID, chance) VALUES(%i, %i, %i)",
                        spawngroupid, npc_type_id, 100);
    results = QueryDatabase(query);
	if (!results.Success()) {
		LogFile->write(EQEMuLog::Error, "NPCSpawnDB Error: %s %s", query.c_str(), results.ErrorMessage().c_str());
		return false;
	}

	if(client)
        client->LogSQL(query.c_str());

	return true;
}

uint32 ZoneDatabase::AddNewNPCSpawnGroupCommand(const char* zone, uint32 zone_version, Client *client, NPC* spawn, uint32 respawnTime) {
    uint32 last_insert_id = 0;

	std::string query = StringFormat("INSERT INTO spawngroup (name) VALUES('%s%s%i')",
                                    zone, spawn->GetName(), Timer::GetCurrentTime());
    auto results = QueryDatabase(query);
	if (!results.Success()) {
		LogFile->write(EQEMuLog::Error, "CreateNewNPCSpawnGroupCommand Error: %s %s", query.c_str(), results.ErrorMessage().c_str());
		return 0;
	}
    last_insert_id = results.LastInsertedID();

    if(client)
        client->LogSQL(query.c_str());

    uint32 respawntime = 0;
    uint32 spawnid = 0;
    if (respawnTime)
        respawntime = respawnTime;
    else if(spawn->respawn2 && spawn->respawn2->RespawnTimer() != 0)
        respawntime = spawn->respawn2->RespawnTimer();
    else
        respawntime = 1200;

    query = StringFormat("INSERT INTO spawn2 (zone, version, x, y, z, respawntime, heading, spawngroupID) "
                        "VALUES('%s', %u, %f, %f, %f, %i, %f, %i)",
                        zone, zone_version, spawn->GetX(), spawn->GetY(), spawn->GetZ(), respawntime,
                        spawn->GetHeading(), last_insert_id);
    results = QueryDatabase(query);
    if (!results.Success()) {
        LogFile->write(EQEMuLog::Error, "CreateNewNPCSpawnGroupCommand Error: %s %s", query.c_str(), results.ErrorMessage().c_str());
        return 0;
    }
    spawnid = results.LastInsertedID();

    if(client)
        client->LogSQL(query.c_str());

    query = StringFormat("INSERT INTO spawnentry (spawngroupID, npcID, chance) VALUES(%i, %i, %i)",
                        last_insert_id, spawn->GetNPCTypeID(), 100);
    results = QueryDatabase(query);
    if (!results.Success()) {
        LogFile->write(EQEMuLog::Error, "CreateNewNPCSpawnGroupCommand Error: %s %s", query.c_str(), results.ErrorMessage().c_str());
        return 0;
    }

    if(client)
        client->LogSQL(query.c_str());

    return spawnid;
}

uint32 ZoneDatabase::UpdateNPCTypeAppearance(Client *client, NPC* spawn) {

	std::string query = StringFormat("UPDATE npc_types SET name = \"%s\", level = %i, race = %i, class = %i, "
                                    "hp = %i, gender = %i, texture = %i, helmtexture = %i, size = %i, "
                                    "loottable_id = %i, merchant_id = %i, face = %i, WHERE id = %i",
                                    spawn->GetName(), spawn->GetLevel(), spawn->GetRace(), spawn->GetClass(),
                                    spawn->GetMaxHP(), spawn->GetGender(), spawn->GetTexture(),
                                    spawn->GetHelmTexture(), spawn->GetSize(), spawn->GetLoottableID(),
                                    spawn->MerchantType, spawn->GetNPCTypeID());
    auto results = QueryDatabase(query);
    if (!results.Success() && client)
            client->LogSQL(query.c_str());

    return results.Success() == true? 1: 0;
}

uint32 ZoneDatabase::DeleteSpawnLeaveInNPCTypeTable(const char* zone, Client *client, NPC* spawn) {
	uint32 id = 0;
	uint32 spawngroupID = 0;

	std::string query = StringFormat("SELECT id, spawngroupID FROM spawn2 WHERE "
                                    "zone='%s' AND spawngroupID=%i", zone, spawn->GetSp2());
    auto results = QueryDatabase(query);
    if (!results.Success())
		return 0;

    if (results.RowCount() == 0)
        return 0;

	auto row = results.begin();
	if (row[0])
        id = atoi(row[0]);

	if (row[1])
        spawngroupID = atoi(row[1]);

    query = StringFormat("DELETE FROM spawn2 WHERE id = '%i'", id);
    results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	if(client)
        client->LogSQL(query.c_str());

    query = StringFormat("DELETE FROM spawngroup WHERE id = '%i'", spawngroupID);
    results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	if(client)
        client->LogSQL(query.c_str());

    query = StringFormat("DELETE FROM spawnentry WHERE spawngroupID = '%i'", spawngroupID);
    results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	if(client)
        client->LogSQL(query.c_str());

	return 1;
}

uint32 ZoneDatabase::DeleteSpawnRemoveFromNPCTypeTable(const char* zone, uint32 zone_version, Client *client, NPC* spawn) {

	uint32 id = 0;
	uint32 spawngroupID = 0;

	std::string query = StringFormat("SELECT id, spawngroupID FROM spawn2 WHERE zone = '%s' "
                                    "AND version = %u AND spawngroupID = %i",
                                    zone, zone_version, spawn->GetSp2());
    auto results = QueryDatabase(query);
    if (!results.Success())
		return 0;

    if (results.RowCount() == 0)
        return 0;

	auto row = results.begin();

	if (row[0])
        id = atoi(row[0]);

	if (row[1])
        spawngroupID = atoi(row[1]);

    query = StringFormat("DELETE FROM spawn2 WHERE id = '%i'", id);
    results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	if(client)
        client->LogSQL(query.c_str());

    query = StringFormat("DELETE FROM spawngroup WHERE id = '%i'", spawngroupID);
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	if(client)
        client->LogSQL(query.c_str());

    query = StringFormat("DELETE FROM spawnentry WHERE spawngroupID = '%i'", spawngroupID);
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	if(client)
        client->LogSQL(query.c_str());

    query = StringFormat("DELETE FROM npc_types WHERE id = '%i'", spawn->GetNPCTypeID());
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	if(client)
        client->LogSQL(query.c_str());

	return 1;
}

uint32  ZoneDatabase::AddSpawnFromSpawnGroup(const char* zone, uint32 zone_version, Client *client, NPC* spawn, uint32 spawnGroupID) {

	uint32 last_insert_id = 0;
	std::string query = StringFormat("INSERT INTO spawn2 (zone, version, x, y, z, respawntime, heading, spawngroupID) "
                                    "VALUES('%s', %u, %f, %f, %f, %i, %f, %i)",
                                    zone, zone_version, client->GetX(), client->GetY(), client->GetZ(),
                                    120, client->GetHeading(), spawnGroupID);
    auto results = QueryDatabase(query);
    if (!results.Success())
		return 0;

	if(client)
        client->LogSQL(query.c_str());

    return 1;
}

uint32 ZoneDatabase::AddNPCTypes(const char* zone, uint32 zone_version, Client *client, NPC* spawn, uint32 spawnGroupID) {

    uint32 npc_type_id;
	char numberlessName[64];

	EntityList::RemoveNumbers(strn0cpy(numberlessName, spawn->GetName(), sizeof(numberlessName)));
	std::string query = StringFormat("INSERT INTO npc_types (name, level, race, class, hp, gender, "
                                    "texture, helmtexture, size, loottable_id, merchant_id, face, "
                                    "runspeed, prim_melee_type, sec_melee_type) "
                                    "VALUES(\"%s\", %i, %i, %i, %i, %i, %i, %i, %f, %i, %i, %i, %f, %i, %i)",
                                    numberlessName, spawn->GetLevel(), spawn->GetRace(), spawn->GetClass(),
                                    spawn->GetMaxHP(), spawn->GetGender(), spawn->GetTexture(),
                                    spawn->GetHelmTexture(), spawn->GetSize(), spawn->GetLoottableID(),
                                    spawn->MerchantType, 0, spawn->GetRunspeed(), 28, 28);
    auto results = QueryDatabase(query);
	if (!results.Success())
		return 0;
    npc_type_id = results.LastInsertedID();

	if(client)
        client->LogSQL(query.c_str());

	if(client)
        client->Message(0, "%s npc_type ID %i created successfully!", numberlessName, npc_type_id);

	return 1;
}

uint32 ZoneDatabase::NPCSpawnDB(uint8 command, const char* zone, uint32 zone_version, Client *c, NPC* spawn, uint32 extra) {

	switch (command) {
		case 0: { // Create a new NPC and add all spawn related data
			return CreateNewNPCCommand(zone, zone_version, c, spawn, extra);
		}
		case 1:{ // Add new spawn group and spawn point for an existing NPC Type ID
			return AddNewNPCSpawnGroupCommand(zone, zone_version, c, spawn, extra);
		}
		case 2: { // Update npc_type appearance and other data on targeted spawn
			return UpdateNPCTypeAppearance(c, spawn);
		}
		case 3: { // delete spawn from spawning, but leave in npc_types table
			return DeleteSpawnLeaveInNPCTypeTable(zone, c, spawn);
		}
		case 4: { //delete spawn from DB (including npc_type)
			return DeleteSpawnRemoveFromNPCTypeTable(zone, zone_version, c, spawn);
		}
		case 5: { // add a spawn from spawngroup
			return AddSpawnFromSpawnGroup(zone, zone_version, c, spawn, extra);
        }
		case 6: { // add npc_type
			return AddNPCTypes(zone, zone_version, c, spawn, extra);
		}
	}
	return false;
}

int32 NPC::GetEquipmentMaterial(uint8 material_slot) const
{
	if (material_slot >= _MaterialCount)
		return 0;

	int inv_slot = Inventory::CalcSlotFromMaterial(material_slot);
	if (inv_slot == -1)
		return 0;
	if(equipment[inv_slot] == 0) {
		switch(material_slot) {
		case MaterialHead:
			return helmtexture;
		case MaterialChest:
			return texture;
		case MaterialPrimary:
			return d_meele_texture1;
		case MaterialSecondary:
			return d_meele_texture2;
		default:
			//they have nothing in the slot, and its not a special slot... they get nothing.
			return(0);
		}
	}

	//they have some loot item in this slot, pass it up to the default handler
	return(Mob::GetEquipmentMaterial(material_slot));
}

uint32 NPC::GetMaxDamage(uint8 tlevel)
{
	uint32 dmg = 0;
	if (tlevel < 40)
		dmg = tlevel*2+2;
	else if (tlevel < 50)
		dmg = level*25/10+2;
	else if (tlevel < 60)
		dmg = (tlevel*3+2)+((tlevel-50)*30);
	else
		dmg = (tlevel*3+2)+((tlevel-50)*35);
	return dmg;
}

void NPC::PickPocket(Client* thief) {

	thief->CheckIncreaseSkill(SkillPickPockets, nullptr, 5);

	//make sure were allowed to targte them:
	int olevel = GetLevel();
	if(olevel > (thief->GetLevel() + THIEF_PICKPOCKET_OVER)) {
		thief->Message(CC_Red, "You are too inexperienced to pick pocket this target");
		thief->SendPickPocketResponse(this, 0, PickPocketFailed);
		//should we check aggro
		return;
	}

	if(zone->random.Roll(5)) {
		AddToHateList(thief, 50);
		Say("Stop thief!");
		thief->Message(CC_Red, "You are noticed trying to steal!");
		thief->SendPickPocketResponse(this, 0, PickPocketFailed);
		return;
	}

	int steal_skill = thief->GetSkill(SkillPickPockets);
	int stealchance = steal_skill*100/(5*olevel+5);
	ItemInst* inst = 0;
	int x = 0;
	int slot[50];
	int steal_items[50];
	int charges[50];
	int money[4];
	money[0] = GetPlatinum();
	money[1] = GetGold();
	money[2] = GetSilver();
	money[3] = GetCopper();
	if (steal_skill < 125)
		money[0] = 0;
	if (steal_skill < 60)
		money[1] = 0;
	memset(slot,0,50);
	memset(steal_items,0,50);
	memset(charges,0,50);
	//Determine wheter to steal money or an item.
	bool no_coin = ((money[0] + money[1] + money[2] + money[3]) == 0);
	bool steal_item = (zone->random.Roll(50) || no_coin);
	if (steal_item)
	{
		ItemList::iterator cur,end;
		cur = itemlist.begin();
		end = itemlist.end();
		for(; cur != end && x < 49; ++cur) {
			ServerLootItem_Struct* citem = *cur;
			const Item_Struct* item = database.GetItem(citem->item_id);
			if (item)
			{
				inst = database.CreateItem(item, citem->charges);
				bool is_arrow = (item->ItemType == ItemTypeArrow) ? true : false;
				int slot_id = thief->GetInv().FindFreeSlot(false, true, inst->GetItem()->Size, is_arrow);
				if (/*!Equipped(item->ID) &&*/
					!item->Magic && item->NoDrop != 0 && !inst->IsType(ItemClassContainer) && slot_id != INVALID_INDEX
					/*&& steal_skill > item->StealSkill*/ )
				{
					slot[x] = slot_id;
					steal_items[x] = item->ID;
					if (inst->IsStackable())
						charges[x] = 1;
					else
						charges[x] = citem->charges;
					x++;
				}
			}
		}
		if (x > 0)
		{
			int random = zone->random.Int(0, x-1);
			inst = database.CreateItem(steal_items[random], charges[random]);
			if (inst)
			{
				const Item_Struct* item = inst->GetItem();
				if (item)
				{
					if (/*item->StealSkill || */steal_skill >= stealchance)
					{
						thief->PutItemInInventory(slot[random], *inst);
						thief->SendItemPacket(slot[random], inst, ItemPacketTrade);
						RemoveItem(item->ID);
						thief->SendPickPocketResponse(this, 0, PickPocketItem, item);
					}
					else
						steal_item = false;
				}
				else
					steal_item = false;
			}
			else
				steal_item = false;
		}
		else if (!no_coin)
		{
			steal_item = false;
		}
		else
		{
			thief->Message(0, "This target's pockets are empty");
			thief->SendPickPocketResponse(this, 0, PickPocketFailed);
		}
	}
	if (!steal_item) //Steal money
	{
		uint32 amt = zone->random.Int(1, (steal_skill/25)+1);
		int steal_type = 0;
		if (!money[0])
		{
			steal_type = 1;
			if (!money[1])
			{
				steal_type = 2;
				if (!money[2])
				{
					steal_type = 3;
				}
			}
		}

		if (zone->random.Roll(stealchance))
		{
			switch (steal_type)
			{
				case 0:{
						if (amt > GetPlatinum())
							amt = GetPlatinum();
						SetPlatinum(GetPlatinum()-amt);
						thief->AddMoneyToPP(0,0,0,amt,false);
						thief->SendPickPocketResponse(this, amt, PickPocketPlatinum);
						break;
				}
				case 1:{
						if (amt > GetGold())
							amt = GetGold();
						SetGold(GetGold()-amt);
						thief->AddMoneyToPP(0,0,amt,0,false);
						thief->SendPickPocketResponse(this, amt, PickPocketGold);
						break;
				}
				case 2:{
						if (amt > GetSilver())
							amt = GetSilver();
						SetSilver(GetSilver()-amt);
						thief->AddMoneyToPP(0,amt,0,0,false);
						thief->SendPickPocketResponse(this, amt, PickPocketSilver);
						break;
				}
				case 3:{
						if (amt > GetCopper())
							amt = GetCopper();
						SetCopper(GetCopper()-amt);
						thief->AddMoneyToPP(amt,0,0,0,false);
						thief->SendPickPocketResponse(this, amt, PickPocketCopper);
						break;
				}
			}
		}
		else
		{
			thief->SendPickPocketResponse(this, 0, PickPocketFailed);
		}
	}
	safe_delete(inst);
}

void Mob::NPCSpecialAttacks(const char* parse, int permtag, bool reset, bool remove) {
	if(reset)
	{
		ClearSpecialAbilities();
	}

	const char* orig_parse = parse;
	while (*parse)
	{
		switch(*parse)
		{
			case 'E':
				SetSpecialAbility(SPECATK_ENRAGE, remove ? 0 : 1);
				break;
			case 'F':
				SetSpecialAbility(SPECATK_FLURRY, remove ? 0 : 1);
				break;
			case 'R':
				SetSpecialAbility(SPECATK_RAMPAGE, remove ? 0 : 1);
				break;
			case 'r':
				SetSpecialAbility(SPECATK_AREA_RAMPAGE, remove ? 0 : 1);
				break;
			case 'S':
				if(remove) {
					SetSpecialAbility(SPECATK_SUMMON, 0);
					StopSpecialAbilityTimer(SPECATK_SUMMON);
				} else {
					SetSpecialAbility(SPECATK_SUMMON, 1);
				}
			break;
			case 'T':
				SetSpecialAbility(SPECATK_TRIPLE, remove ? 0 : 1);
				break;
			case 'Q':
				//quad requires triple to work properly
				if(remove) {
					SetSpecialAbility(SPECATK_QUAD, 0);
				} else {
					SetSpecialAbility(SPECATK_TRIPLE, 1);
					SetSpecialAbility(SPECATK_QUAD, 1);
					}
				break;
			case 'b':
				SetSpecialAbility(SPECATK_BANE, remove ? 0 : 1);
				break;
			case 'm':
				SetSpecialAbility(SPECATK_MAGICAL, remove ? 0 : 1);
				break;
			case 'U':
				SetSpecialAbility(UNSLOWABLE, remove ? 0 : 1);
				break;
			case 'M':
				SetSpecialAbility(UNMEZABLE, remove ? 0 : 1);
				break;
			case 'C':
				SetSpecialAbility(UNCHARMABLE, remove ? 0 : 1);
				break;
			case 'N':
				SetSpecialAbility(UNSTUNABLE, remove ? 0 : 1);
				break;
			case 'I':
				SetSpecialAbility(UNSNAREABLE, remove ? 0 : 1);
				break;
			case 'D':
				SetSpecialAbility(UNFEARABLE, remove ? 0 : 1);
				break;
			case 'K':
				SetSpecialAbility(UNDISPELLABLE, remove ? 0 : 1);
				break;
			case 'A':
				SetSpecialAbility(IMMUNE_MELEE, remove ? 0 : 1);
				break;
			case 'B':
				SetSpecialAbility(IMMUNE_MAGIC, remove ? 0 : 1);
				break;
			case 'f':
				SetSpecialAbility(IMMUNE_FLEEING, remove ? 0 : 1);
				break;
			case 'O':
				SetSpecialAbility(IMMUNE_MELEE_EXCEPT_BANE, remove ? 0 : 1);
				break;
			case 'W':
				SetSpecialAbility(IMMUNE_MELEE_NONMAGICAL, remove ? 0 : 1);
				break;
			case 'H':
				SetSpecialAbility(IMMUNE_AGGRO, remove ? 0 : 1);
				break;
			case 'G':
				SetSpecialAbility(IMMUNE_AGGRO_ON, remove ? 0 : 1);
				break;
			case 'g':
				SetSpecialAbility(IMMUNE_CASTING_FROM_RANGE, remove ? 0 : 1);
				break;
			case 'd':
				SetSpecialAbility(IMMUNE_FEIGN_DEATH, remove ? 0 : 1);
				break;
			case 'Y':
				SetSpecialAbility(SPECATK_RANGED_ATK, remove ? 0 : 1);
				break;
			case 'L':
				SetSpecialAbility(SPECATK_INNATE_DW, remove ? 0 : 1);
				break;
			case 't':
				SetSpecialAbility(NPC_TUNNELVISION, remove ? 0 : 1);
				break;
			case 'n':
				SetSpecialAbility(NPC_NO_BUFFHEAL_FRIENDS, remove ? 0 : 1);
				break;
			case 'p':
				SetSpecialAbility(IMMUNE_PACIFY, remove ? 0 : 1);
				break;
			case 'J':
				SetSpecialAbility(LEASH, remove ? 0 : 1);
				break;
			case 'j':
				SetSpecialAbility(TETHER, remove ? 0 : 1);
				break;
			case 'o':
				SetSpecialAbility(DESTRUCTIBLE_OBJECT, remove ? 0 : 1);
				break;
			case 'Z':
				SetSpecialAbility(NO_HARM_FROM_CLIENT, remove ? 0 : 1);
				break;
			case 'i':
				SetSpecialAbility(IMMUNE_TAUNT, remove ? 0 : 1);
				break;
			case 'e':
				SetSpecialAbility(ALWAYS_FLEE, remove ? 0 : 1);
				break;
			case 'h':
				SetSpecialAbility(FLEE_PERCENT, remove ? 0 : 1);
				break;

			default:
				break;
		}
		parse++;
	}

	if(permtag == 1 && this->GetNPCTypeID() > 0)
	{
		if(database.SetSpecialAttkFlag(this->GetNPCTypeID(), orig_parse))
		{
			LogFile->write(EQEMuLog::Normal, "NPCTypeID: %i flagged to '%s' for Special Attacks.\n",this->GetNPCTypeID(),orig_parse);
		}
	}
}

bool Mob::HasNPCSpecialAtk(const char* parse) {

	bool HasAllAttacks = true;

	while (*parse && HasAllAttacks == true)
	{
		switch(*parse)
		{
			case 'E':
				if (!GetSpecialAbility(SPECATK_ENRAGE))
					HasAllAttacks = false;
				break;
			case 'F':
				if (!GetSpecialAbility(SPECATK_FLURRY))
					HasAllAttacks = false;
				break;
			case 'R':
				if (!GetSpecialAbility(SPECATK_RAMPAGE))
					HasAllAttacks = false;
				break;
			case 'r':
				if (!GetSpecialAbility(SPECATK_AREA_RAMPAGE))
					HasAllAttacks = false;
				break;
			case 'S':
				if (!GetSpecialAbility(SPECATK_SUMMON))
					HasAllAttacks = false;
				break;
			case 'T':
				if (!GetSpecialAbility(SPECATK_TRIPLE))
					HasAllAttacks = false;
				break;
			case 'Q':
				if (!GetSpecialAbility(SPECATK_QUAD))
					HasAllAttacks = false;
				break;
			case 'b':
				if (!GetSpecialAbility(SPECATK_BANE))
					HasAllAttacks = false;
				break;
			case 'm':
				if (!GetSpecialAbility(SPECATK_MAGICAL))
					HasAllAttacks = false;
				break;
			case 'U':
				if (!GetSpecialAbility(UNSLOWABLE))
					HasAllAttacks = false;
				break;
			case 'M':
				if (!GetSpecialAbility(UNMEZABLE))
					HasAllAttacks = false;
				break;
			case 'C':
				if (!GetSpecialAbility(UNCHARMABLE))
					HasAllAttacks = false;
				break;
			case 'N':
				if (!GetSpecialAbility(UNSTUNABLE))
					HasAllAttacks = false;
				break;
			case 'I':
				if (!GetSpecialAbility(UNSNAREABLE))
					HasAllAttacks = false;
				break;
			case 'D':
				if (!GetSpecialAbility(UNFEARABLE))
					HasAllAttacks = false;
				break;
			case 'A':
				if (!GetSpecialAbility(IMMUNE_MELEE))
					HasAllAttacks = false;
				break;
			case 'B':
				if (!GetSpecialAbility(IMMUNE_MAGIC))
					HasAllAttacks = false;
				break;
			case 'f':
				if (!GetSpecialAbility(IMMUNE_FLEEING))
					HasAllAttacks = false;
				break;
			case 'O':
				if (!GetSpecialAbility(IMMUNE_MELEE_EXCEPT_BANE))
					HasAllAttacks = false;
				break;
			case 'W':
				if (!GetSpecialAbility(IMMUNE_MELEE_NONMAGICAL))
					HasAllAttacks = false;
				break;
			case 'H':
				if (!GetSpecialAbility(IMMUNE_AGGRO))
					HasAllAttacks = false;
				break;
			case 'G':
				if (!GetSpecialAbility(IMMUNE_AGGRO_ON))
					HasAllAttacks = false;
				break;
			case 'g':
				if (!GetSpecialAbility(IMMUNE_CASTING_FROM_RANGE))
					HasAllAttacks = false;
				break;
			case 'd':
				if (!GetSpecialAbility(IMMUNE_FEIGN_DEATH))
					HasAllAttacks = false;
				break;
			case 'Y':
				if (!GetSpecialAbility(SPECATK_RANGED_ATK))
					HasAllAttacks = false;
				break;
			case 'L':
				if (!GetSpecialAbility(SPECATK_INNATE_DW))
					HasAllAttacks = false;
				break;
			case 't':
				if (!GetSpecialAbility(NPC_TUNNELVISION))
					HasAllAttacks = false;
				break;
			case 'n':
				if (!GetSpecialAbility(NPC_NO_BUFFHEAL_FRIENDS))
					HasAllAttacks = false;
				break;
			case 'p':
				if(!GetSpecialAbility(IMMUNE_PACIFY))
					HasAllAttacks = false;
				break;
			case 'J':
				if(!GetSpecialAbility(LEASH))
					HasAllAttacks = false;
				break;
			case 'j':
				if(!GetSpecialAbility(TETHER))
					HasAllAttacks = false;
				break;
			case 'o':
				if(!GetSpecialAbility(DESTRUCTIBLE_OBJECT))
				{
					HasAllAttacks = false;
				}
				break;
			case 'Z':
				if(!GetSpecialAbility(NO_HARM_FROM_CLIENT)){
					HasAllAttacks = false;
				}
				break;
			case 'e':
				if(!GetSpecialAbility(ALWAYS_FLEE))
					HasAllAttacks = false;
				break;
			case 'h':
				if(!GetSpecialAbility(FLEE_PERCENT))
					HasAllAttacks = false;
				break;
			default:
				HasAllAttacks = false;
				break;
		}
		parse++;
	}

	return HasAllAttacks;
}

void NPC::FillSpawnStruct(NewSpawn_Struct* ns, Mob* ForWho)
{
	Mob::FillSpawnStruct(ns, ForWho);

	//Not recommended if using above (However, this will work better on older clients).
	if (RuleB(Pets, UnTargetableSwarmPet)) {
		if(GetOwnerID() || GetSwarmOwner()) {
			ns->spawn.is_pet = 1;
			if (!IsCharmed() && GetOwnerID()) {
				Client *c = entity_list.GetClientByID(GetOwnerID());
				if(c)
					sprintf(ns->spawn.lastName, "%s's Pet", c->GetName());
			}
			else if (GetSwarmOwner()) {
				ns->spawn.bodytype = 11;
				if(!IsCharmed())
				{
					Client *c = entity_list.GetClientByID(GetSwarmOwner());
					if(c)
						sprintf(ns->spawn.lastName, "%s's Pet", c->GetName());
				}
			}
		}
	} else {
		if(GetOwnerID()) {
			ns->spawn.is_pet = 1;
			if (!IsCharmed() && GetOwnerID()) {
				Client *c = entity_list.GetClientByID(GetOwnerID());
				if(c)
					sprintf(ns->spawn.lastName, "%s's Pet", c->GetName());
			}
		} else
			ns->spawn.is_pet = 0;
	}

	ns->spawn.is_npc = 1;
}

void NPC::SetLevel(uint8 in_level, bool command)
{
	level = in_level;
	SendAppearancePacket(AT_WhoLevel, in_level);
}

void NPC::ModifyNPCStat(const char *identifier, const char *newValue)
{
	std::string id = identifier;
	std::string val = newValue;
	for(int i = 0; i < id.length(); ++i) {
		id[i] = std::tolower(id[i]);
	}

	if(id == "ac") { AC = atoi(val.c_str()); return; }
	else if(id == "str") { STR = atoi(val.c_str()); return; }
	else if(id == "sta") { STA = atoi(val.c_str()); return; }
	else if(id == "agi") { AGI = atoi(val.c_str()); return; }
	else if(id == "dex") { DEX = atoi(val.c_str()); return; }
	else if(id == "wis") { WIS = atoi(val.c_str()); CalcMaxMana(); return; }
	else if(id == "int" || id == "_int") { INT = atoi(val.c_str()); CalcMaxMana(); return; }
	else if(id == "cha") { CHA = atoi(val.c_str()); return; }
	else if(id == "max_hp") { base_hp = atoi(val.c_str()); CalcMaxHP(); if (cur_hp > max_hp) { cur_hp = max_hp; } return; }
	else if(id == "max_mana") { npc_mana = atoi(val.c_str()); CalcMaxMana(); if (cur_mana > max_mana){ cur_mana = max_mana; } return; }
	else if(id == "mr") { MR = atoi(val.c_str()); return; }
	else if(id == "fr") { FR = atoi(val.c_str()); return; }
	else if(id == "cr") { CR = atoi(val.c_str()); return; }
	else if(id == "pr") { PR = atoi(val.c_str()); return; }
	else if(id == "dr") { DR = atoi(val.c_str()); return; }
	else if(id == "PhR") { PhR = atoi(val.c_str()); return; }
	else if(id == "runspeed") { runspeed = (float)atof(val.c_str()); CalcBonuses(); return; }
	else if(id == "special_attacks") { NPCSpecialAttacks(val.c_str(), 0, 1); return; }
	else if(id == "special_abilities") { ProcessSpecialAbilities(val.c_str()); return; }
	else if(id == "atk") { ATK = atoi(val.c_str()); return; }
	else if(id == "accuracy") { accuracy_rating = atoi(val.c_str()); return; }
	else if(id == "avoidance") { avoidance_rating = atoi(val.c_str()); return; }
	else if(id == "trackable") { trackable = atoi(val.c_str()); return; }
	else if(id == "min_hit") { min_dmg = atoi(val.c_str()); return; }
	else if(id == "max_hit") { max_dmg = atoi(val.c_str()); return; }
	else if(id == "attack_count") { attack_count = atoi(val.c_str()); return; }
	else if(id == "see_invis") { see_invis = atoi(val.c_str()); return; }
	else if(id == "see_invis_undead") { see_invis_undead = atoi(val.c_str()); return; }
	else if(id == "see_hide") { see_hide = atoi(val.c_str()); return; }
	else if(id == "see_improved_hide") { see_improved_hide = atoi(val.c_str()); return; }
	else if(id == "hp_regen") { hp_regen = atoi(val.c_str()); return; }
	else if(id == "mana_regen") { mana_regen = atoi(val.c_str()); return; }
	else if(id == "level") { SetLevel(atoi(val.c_str())); return; }
	else if(id == "aggro") { pAggroRange = atof(val.c_str()); return; }
	else if(id == "assist") { pAssistRange = atof(val.c_str()); return; }
	else if(id == "slow_mitigation") { slow_mitigation = atoi(val.c_str()); return; }
	else if(id == "loottable_id") { loottable_id = atof(val.c_str()); return; }
	else if(id == "healscale") { healscale = atof(val.c_str()); return; }
	else if(id == "spellscale") { spellscale = atof(val.c_str()); return; }
}

void NPC::LevelScale() {

	uint8 random_level = (zone->random.Int(level, maxlevel));
	float scaling = (((random_level / (float)level) - 1) * (scalerate / 100.0f));

	if(scalerate == 0 || maxlevel <= 25)
	{
		//todo: add expansion column to npc_types
		if (npctype_id < 200000)
		{
			max_hp += (random_level - level) * 20;
			base_hp += (random_level - level) * 20;
		}
		else
		{
			max_hp += (random_level - level) * 100;
			base_hp += (random_level - level) * 100;
		}

		cur_hp = max_hp;

		max_dmg += (random_level - level) * 2;
	}
	else
	{
		uint8 scale_adjust = 1;

		base_hp += (int)(base_hp * scaling);
		max_hp += (int)(max_hp * scaling);
		cur_hp = max_hp;

		if (max_dmg)
		{
			max_dmg += (int)(max_dmg * scaling / scale_adjust);
			min_dmg += (int)(min_dmg * scaling / scale_adjust);
		}

		STR += (int)(STR * scaling / scale_adjust);
		STA += (int)(STA * scaling / scale_adjust);
		AGI += (int)(AGI * scaling / scale_adjust);
		DEX += (int)(DEX * scaling / scale_adjust);
		if(INT > RuleI(Aggro, IntAggroThreshold))
			INT += (int)(INT * scaling / scale_adjust);
		WIS += (int)(WIS * scaling / scale_adjust);
		CHA += (int)(CHA * scaling / scale_adjust);
		if (MR)
			MR += (int)(MR * scaling / scale_adjust);
		if (CR)
			CR += (int)(CR * scaling / scale_adjust);
		if (DR)
			DR += (int)(DR * scaling / scale_adjust);
		if (FR)
			FR += (int)(FR * scaling / scale_adjust);
		if (PR)
			PR += (int)(PR * scaling / scale_adjust);
	}

	level = random_level;

	return;
}

void NPC::CalcNPCResists() {

	if (!MR)
		MR = (GetLevel() * 11)/10;
	if (!CR)
		CR = (GetLevel() * 11)/10;
	if (!DR)
		DR = (GetLevel() * 11)/10;
	if (!FR)
		FR = (GetLevel() * 11)/10;
	if (!PR)
		PR = (GetLevel() * 11)/10;
	if (!Corrup)
		Corrup = 15;
	if (!PhR)
		PhR = 10;
	return;
}

void NPC::CalcNPCRegen() {

	// Fix for lazy db-updaters (regen values left at 0)
	if (GetCasterClass() != 'N' && mana_regen == 0)
		mana_regen = (GetLevel() / 10) + 4;
	else if(mana_regen < 0)
		mana_regen = 0;
	else
		mana_regen = mana_regen;

	// Gives low end monsters no regen if set to 0 in database. Should make low end monsters killable
	// Might want to lower this to /5 rather than 10.
	if(hp_regen == 0)
	{
		if (GetLevel() <= 15)
			hp_regen = 1;
		else if(GetLevel() > 15 && GetLevel() <= 30)
			hp_regen = 2;
		else if (GetLevel() > 30 && GetLevel() <= 35)
			hp_regen = 4;
		else if(GetLevel() > 35 && GetLevel() <= 40)
			hp_regen = 8;
		else if(GetLevel() > 40 && GetLevel() <= 45)
			hp_regen = 12;
		else if(GetLevel() > 45 && GetLevel() <= 50)
			hp_regen = 18;
		else
			hp_regen = 24;
	}
	else if (hp_regen < 0)
		hp_regen = 0;
	else
		hp_regen = hp_regen;

	return;
}

void NPC::CalcNPCDamage() {

	int AC_adjust=12;

	if (GetLevel() >= 66) {
		if (min_dmg==0)
			min_dmg = 220;
		if (max_dmg==0)
			max_dmg = ((((99000)*(GetLevel()-64))/400)*AC_adjust/10);
	}
	else if (GetLevel() >= 60 && GetLevel() <= 65){
		if(min_dmg==0)
			min_dmg = (GetLevel()+(GetLevel()/3));
		if(max_dmg==0)
			max_dmg = (GetLevel()*3)*AC_adjust/10;
	}
	else if (GetLevel() >= 51 && GetLevel() <= 59){
		if(min_dmg==0)
			min_dmg = (GetLevel()+(GetLevel()/3));
		if(max_dmg==0)
			max_dmg = (GetLevel()*3)*AC_adjust/10;
	}
	else if (GetLevel() >= 40 && GetLevel() <= 50) {
		if (min_dmg==0)
			min_dmg = GetLevel();
		if(max_dmg==0)
			max_dmg = (GetLevel()*3)*AC_adjust/10;
	}
	else if (GetLevel() >= 28 && GetLevel() <= 39) {
		if (min_dmg==0)
			min_dmg = GetLevel() / 2;
		if (max_dmg==0)
			max_dmg = ((GetLevel()*2)+2)*AC_adjust/10;
	}
	else if (GetLevel() <= 27) {
		if (min_dmg==0)
			min_dmg=1;
		if (max_dmg==0)
			max_dmg = (GetLevel()*2)*AC_adjust/10;
	}

	int32 clfact = GetClassLevelFactor();
	min_dmg = (min_dmg * clfact) / 220;
	max_dmg = (max_dmg * clfact) / 220;

	return;
}


uint32 NPC::GetSpawnPointID() const
{
	if(respawn2)
	{
		return respawn2->GetID();
	}
	return 0;
}

void NPC::NPCSlotTexture(uint8 slot, uint16 texture)
{
	if (slot == 7) {
		d_meele_texture1 = texture;
	}
	else if (slot == 8) {
		d_meele_texture2 = texture;
	}
	else if (slot < 6) {
		// Reserved for texturing individual armor slots
	}
	return;
}

uint32 NPC::GetSwarmOwner()
{
	if(GetSwarmInfo() != nullptr)
	{
		return GetSwarmInfo()->owner_id;
	}
	return 0;
}

uint32 NPC::GetSwarmTarget()
{
	if(GetSwarmInfo() != nullptr)
	{
		return GetSwarmInfo()->target;
	}
	return 0;
}

void NPC::SetSwarmTarget(int target_id)
{
	if(GetSwarmInfo() != nullptr)
	{
		GetSwarmInfo()->target = target_id;
	}
	return;
}

int32 NPC::CalcMaxMana() {
	if(npc_mana == 0) {
		switch (GetCasterClass()) {
			case 'I':
				max_mana = (((GetINT()/2)+1) * GetLevel()) + spellbonuses.Mana + itembonuses.Mana;
				break;
			case 'W':
				max_mana = (((GetWIS()/2)+1) * GetLevel()) + spellbonuses.Mana + itembonuses.Mana;
				break;
			case 'N':
			default:
				max_mana = 0;
				break;
		}
		if (max_mana < 0) {
			max_mana = 0;
		}

		return max_mana;
	} else {
		switch (GetCasterClass()) {
			case 'I':
				max_mana = npc_mana + spellbonuses.Mana + itembonuses.Mana;
				break;
			case 'W':
				max_mana = npc_mana + spellbonuses.Mana + itembonuses.Mana;
				break;
			case 'N':
			default:
				max_mana = 0;
				break;
		}
		if (max_mana < 0) {
			max_mana = 0;
		}

		return max_mana;
	}
}

void NPC::SignalNPC(int _signal_id)
{
	signal_q.push_back(_signal_id);
}

NPC_Emote_Struct* NPC::GetNPCEmote(uint16 emoteid, uint8 event_) {
	LinkedListIterator<NPC_Emote_Struct*> iterator(zone->NPCEmoteList);
	iterator.Reset();
	while(iterator.MoreElements())
	{
		NPC_Emote_Struct* nes = iterator.GetData();
		if (emoteid == nes->emoteid && event_ == nes->event_) {
			return (nes);
		}
		iterator.Advance();
	}
	return (nullptr);
}

void NPC::DoNPCEmote(uint8 event_, uint16 emoteid)
{
	if(this == nullptr || emoteid == 0)
	{
		return;
	}

	NPC_Emote_Struct* nes = GetNPCEmote(emoteid,event_);
	if(nes == nullptr)
	{
		return;
	}

	if(emoteid == nes->emoteid)
	{
		if(nes->type == 1)
			this->Emote("%s",nes->text);
		else if(nes->type == 2)
			this->Shout("%s",nes->text);
		else if(nes->type == 3)
			entity_list.MessageClose_StringID(this, true, 200, 10, GENERIC_STRING, nes->text);
		else
			this->Say("%s",nes->text);
	}
}

bool NPC::CanTalk()
{
	//Races that should be able to talk. (Races up to Titanium)

	uint16 TalkRace[473] =
	{1,2,3,4,5,6,7,8,9,10,11,12,0,0,15,16,0,18,19,20,0,0,23,0,25,0,0,0,0,0,0,
	32,0,0,0,0,0,0,39,40,0,0,0,44,0,0,0,0,49,0,51,0,53,54,55,56,57,58,0,0,0,
	62,0,64,65,66,67,0,0,70,71,0,0,0,0,0,77,78,79,0,81,82,0,0,0,86,0,0,0,90,
	0,92,93,94,95,0,0,98,99,0,101,0,103,0,0,0,0,0,0,110,111,112,0,0,0,0,0,0,
	0,0,0,0,123,0,0,126,0,128,0,130,131,0,0,0,0,136,137,0,139,140,0,0,0,144,
	0,0,0,0,0,150,151,152,153,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,183,184,0,0,187,188,189,0,0,0,0,0,195,196,0,198,0,0,0,202,0,
	0,205,0,0,208,0,0,0,0,0,0,0,0,217,0,219,0,0,0,0,0,0,226,0,0,229,230,0,0,
	0,0,235,236,0,238,239,240,241,242,243,244,0,246,247,0,0,0,251,0,0,254,255,
	256,257,0,0,0,0,0,0,0,0,266,267,0,0,270,271,0,0,0,0,0,277,278,0,0,0,0,283,
	284,0,286,0,288,289,290,0,0,0,0,295,296,297,298,299,300,0,0,0,304,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,320,0,322,323,324,325,0,0,0,0,330,331,332,333,334,335,
	336,337,338,339,340,341,342,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,359,360,361,362,
	0,364,365,366,0,368,369,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,385,386,0,0,0,0,0,392,
	393,394,395,396,397,398,0,400,402,0,0,0,0,406,0,408,0,0,411,0,413,0,0,0,417,
	0,0,420,0,0,0,0,425,0,0,0,0,0,0,0,433,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,458,0,0,0,0,0,0,0,0,467,0,0,470,0,0,473};

	int talk_check = TalkRace[GetRace() - 1];

	if (TalkRace[GetRace() - 1] > 0)
		return true;

	return false;
}

//this is called with 'this' as the mob being looked at, and
//iOther the mob who is doing the looking. It should figure out
//what iOther thinks about 'this'
FACTION_VALUE NPC::GetReverseFactionCon(Mob* iOther) {
	iOther = iOther->GetOwnerOrSelf();
	int primaryFaction= iOther->GetPrimaryFaction();

	//I am pretty sure that this special faction call is backwards
	//and should be iOther->GetSpecialFactionCon(this)
	if (primaryFaction < 0)
		return GetSpecialFactionCon(iOther);

	if (primaryFaction == 0)
		return FACTION_INDIFFERENT;

	//if we are a pet, use our owner's faction stuff
	Mob *own = GetOwner();
	if (own != nullptr)
		return own->GetReverseFactionCon(iOther);

	//make sure iOther is an npc
	//also, if we dont have a faction, then they arnt gunna think anything of us either
	if(!iOther->IsNPC() || GetPrimaryFaction() == 0)
		return(FACTION_INDIFFERENT);

	//if we get here, iOther is an NPC too

	//otherwise, employ the npc faction stuff
	//so we need to look at iOther's faction table to see
	//what iOther thinks about our primary faction
	return(iOther->CastToNPC()->CheckNPCFactionAlly(GetPrimaryFaction()));
}

//Look through our faction list and return a faction con based
//on the npc_value for the other person's primary faction in our list.
FACTION_VALUE NPC::CheckNPCFactionAlly(int32 other_faction) {
	std::list<struct NPCFaction*>::iterator cur,end;
	cur = faction_list.begin();
	end = faction_list.end();
	for(; cur != end; ++cur) {
		struct NPCFaction* fac = *cur;
		if ((int32)fac->factionID == other_faction) {
			if (fac->npc_value > 0)
				return FACTION_ALLY;
			else if (fac->npc_value < 0)
				return FACTION_SCOWLS;
			else
				return FACTION_INDIFFERENT;
		}
	}
	return FACTION_INDIFFERENT;
}

bool NPC::IsFactionListAlly(uint32 other_faction) {
	return(CheckNPCFactionAlly(other_faction) == FACTION_ALLY);
}

int NPC::GetScore()
{
    int lv = std::min(70, (int)GetLevel());
    int basedmg = (lv*2)*(1+(lv / 100)) - (lv / 2);
    int minx = 0;
    int basehp = 0;
    int hpcontrib = 0;
    int dmgcontrib = 0;
    int spccontrib = 0;
    int hp = GetMaxHP();
    int mindmg = min_dmg;
    int maxdmg = max_dmg;
    int final;

    if(lv < 46)
    {
		minx = static_cast<int> (ceil( ((lv - (lv / 10.0)) - 1.0) ));
		basehp = (lv * 10) + (lv * lv);
	}
	else
	{
		minx = static_cast<int> (ceil( ((lv - (lv / 10.0)) - 1.0) - (( lv - 45.0 ) / 2.0) ));
        basehp = (lv * 10) + ((lv * lv) * 4);
    }

    if(hp > basehp)
    {
        hpcontrib = static_cast<int> (((hp / static_cast<float> (basehp)) * 1.5));
        if(hpcontrib > 5) { hpcontrib = 5; }

        if(maxdmg > basedmg)
        {
            dmgcontrib = static_cast<int> (ceil( ((maxdmg / basedmg) * 1.5) ));
        }

        if(HasNPCSpecialAtk("E")) { spccontrib++; }    //Enrage
        if(HasNPCSpecialAtk("F")) { spccontrib++; }    //Flurry
        if(HasNPCSpecialAtk("R")) { spccontrib++; }    //Rampage
        if(HasNPCSpecialAtk("r")) { spccontrib++; }    //Area Rampage
        if(HasNPCSpecialAtk("S")) { spccontrib++; }    //Summon
        if(HasNPCSpecialAtk("T")) { spccontrib += 2; } //Triple
        if(HasNPCSpecialAtk("Q")) { spccontrib += 3; } //Quad
        if(HasNPCSpecialAtk("U")) { spccontrib += 5; } //Unslowable
        if(HasNPCSpecialAtk("L")) { spccontrib++; }    //Innate Dual Wield
    }

    if(npc_spells_id > 12)
	{
        if(lv < 16)
            spccontrib++;
        else
            spccontrib += static_cast<int> (floor(lv/15.0));
    }

    final = minx + hpcontrib + dmgcontrib + spccontrib;
    final = std::max(1, final);
    final = std::min(100, final);
    return(final);
}

uint32 NPC::GetSpawnKillCount()
{
	uint32 sid = GetSpawnPointID();

	if(sid > 0)
	{
		return(zone->GetSpawnKillCount(sid));
	}

	return(0);
}

void NPC::DoQuestPause(Mob *other) {
	if(IsMoving() && !IsOnHatelist(other)) {
		PauseWandering(RuleI(NPC, SayPauseTimeInSec));
		FaceTarget(other);
	} else if(!IsMoving()) {
		FaceTarget(other);
	}

}

void NPC::DepopSwarmPets()
{
	if (GetSwarmInfo()) {
		if (GetSwarmInfo()->duration->Check(false)){
			Mob* owner = entity_list.GetMobID(GetSwarmInfo()->owner_id);
			if (owner)
				owner->SetTempPetCount(owner->GetTempPetCount() - 1);
			
			Depop();
			return;
		}

		//This is only used for optional quest or rule derived behavior now if you force a temp pet on a specific target.
		if (GetSwarmInfo()->target) {
			Mob *targMob = entity_list.GetMob(GetSwarmInfo()->target);
			if(!targMob || (targMob && targMob->IsCorpse())){
				Mob* owner = entity_list.GetMobID(GetSwarmInfo()->owner_id);
				if (owner)
					owner->SetTempPetCount(owner->GetTempPetCount() - 1);

				Depop();
				return;
			}
		}
	}
}
