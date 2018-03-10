// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#include "../common/cbasetypes.h"
#include "../common/core.h"
#include "../common/db.h"
#include "../common/malloc.h"
#include "../common/mapindex.h"
#include "../common/mmo.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/timer.h"
#include "../common/utils.h"
#include "../common/cli.h"
#include "../common/random.h"
#include "../common/ers.h"
#include "int_guild.h"
#include "int_homun.h"
#include "int_mercenary.h"
#include "int_elemental.h"
#include "int_party.h"
#include "int_storage.h"
#include "inter.h"
#include "char_logif.h"
#include "char_mapif.h"
#include "char_cnslif.h"
#include "char_clif.h"
#include "int_mail.h"
#include "char.h"

//definition of exported var declared in .h
int login_fd=-1; //login file descriptor
int char_fd=-1; //char file descriptor
struct Schema_Config schema_config;
struct CharServ_Config charserv_config;
struct mmo_map_server map_server[MAX_MAP_SERVERS];
//Custom limits for the fame lists. [Skotlex]
int fame_list_size_chemist = MAX_FAME_LIST;
int fame_list_size_smith = MAX_FAME_LIST;
int fame_list_size_taekwon = MAX_FAME_LIST;
int fame_list_size_pvprank = MAX_FAME_LIST; // [Zephyrus]
int fame_list_size_bgrank = MAX_FAME_LIST;
int fame_list_size_bg = MAX_FAME_LIST;

// Char-server-side stored fame lists [DracoRPG]
struct fame_list smith_fame_list[MAX_FAME_LIST];
struct fame_list chemist_fame_list[MAX_FAME_LIST];
struct fame_list taekwon_fame_list[MAX_FAME_LIST];
struct fame_list pvprank_fame_list[MAX_FAME_LIST]; // [Zephyrus]
struct fame_list bgrank_fame_list[MAX_FAME_LIST];
struct fame_list bg_fame_list[MAX_FAME_LIST];

#define CHAR_MAX_MSG 300	//max number of msg_conf
static char* msg_table[CHAR_MAX_MSG]; // Login Server messages_conf

// check for exit signal
// 0 is saving complete
// other is char_id
unsigned int save_flag = 0;

#define MAX_STARTITEM 32
struct startitem {
	int nameid; //Item ID
	int amount; //Number of items
	int pos; //Position (for auto-equip)
} start_items[MAX_STARTITEM+1];

// Advanced subnet check [LuzZza]
struct s_subnet {
	uint32 mask;
	uint32 char_ip;
	uint32 map_ip;
} subnet[16];
int subnet_count = 0;

int char_chardb_waiting_disconnect(int tid, unsigned int tick, int id, intptr_t data);

DBMap* auth_db; // int account_id -> struct auth_node*
DBMap* online_char_db; // int account_id -> struct online_char_data*
DBMap* char_db_; // int char_id -> struct mmo_charstatus*
DBMap* char_get_authdb() { return auth_db; }
DBMap* char_get_onlinedb() { return online_char_db; }
DBMap* char_get_chardb() { return char_db_; }

/**
 * @see DBCreateData
 */
DBData char_create_online_data(DBKey key, va_list args){
	struct online_char_data* character;
	CREATE(character, struct online_char_data, 1);
	character->account_id = key.i;
	character->char_id = -1;
	character->server = -1;
	character->fd = -1;
	character->waiting_disconnect = INVALID_TIMER;
	return db_ptr2data(character);
}

void char_set_charselect(int account_id) {
	struct online_char_data* character;

	character = (struct online_char_data*)idb_ensure(online_char_db, account_id, char_create_online_data);

	if( character->server > -1 )
		if( map_server[character->server].users > 0 ) // Prevent this value from going negative.
			map_server[character->server].users--;

	character->char_id = -1;
	character->server = -1;

	if(character->waiting_disconnect != INVALID_TIMER) {
		delete_timer(character->waiting_disconnect, char_chardb_waiting_disconnect);
		character->waiting_disconnect = INVALID_TIMER;
	}

	chlogif_send_setacconline(account_id);

}

void char_set_char_online(int map_id, int char_id, int account_id) {
	struct online_char_data* character;
	struct mmo_charstatus *cp;

	//Update DB
	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online`='1' WHERE `char_id`='%d' LIMIT 1", schema_config.char_db, char_id) )
		Sql_ShowDebug(sql_handle);

	//Check to see for online conflicts
	character = (struct online_char_data*)idb_ensure(online_char_db, account_id, char_create_online_data);
	if( character->char_id != -1 && character->server > -1 && character->server != map_id )
	{
		ShowNotice("set_char_online: Character %d:%d marked in map server %d, but map server %d claims to have (%d:%d) online!\n",
			character->account_id, character->char_id, character->server, map_id, account_id, char_id);
		mapif_disconnectplayer(map_server[character->server].fd, character->account_id, character->char_id, 2);
	}

	//Update state data
	character->char_id = char_id;
	character->server = map_id;

	if( character->server > -1 )
		map_server[character->server].users++;

	//Get rid of disconnect timer
	if(character->waiting_disconnect != INVALID_TIMER) {
		delete_timer(character->waiting_disconnect, char_chardb_waiting_disconnect);
		character->waiting_disconnect = INVALID_TIMER;
	}

	//Set char online in guild cache. If char is in memory, use the guild id on it, otherwise seek it.
	cp = (struct mmo_charstatus*)idb_get(char_db_,char_id);
	inter_guild_CharOnline(char_id, cp?cp->guild_id:-1);

	//Notify login server
	chlogif_send_setacconline(account_id);
}

void char_set_char_offline(int char_id, int account_id){
	struct online_char_data* character;

	if ( char_id == -1 )
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online`='0' WHERE `account_id`='%d'", schema_config.char_db, account_id) )
			Sql_ShowDebug(sql_handle);
	}
	else
	{
		struct mmo_charstatus* cp = (struct mmo_charstatus*)idb_get(char_db_,char_id);
		inter_guild_CharOffline(char_id, cp?cp->guild_id:-1);
		if (cp)
			idb_remove(char_db_,char_id);

		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online`='0' WHERE `char_id`='%d' LIMIT 1", schema_config.char_db, char_id) )
			Sql_ShowDebug(sql_handle);
	}

	if ((character = (struct online_char_data*)idb_get(online_char_db, account_id)) != NULL)
	{	//We don't free yet to avoid aCalloc/aFree spamming during char change. [Skotlex]
		if( character->server > -1 )
			if( map_server[character->server].users > 0 ) // Prevent this value from going negative.
				map_server[character->server].users--;

		if(character->waiting_disconnect != INVALID_TIMER){
			delete_timer(character->waiting_disconnect, char_chardb_waiting_disconnect);
			character->waiting_disconnect = INVALID_TIMER;
		}

		if(character->char_id == char_id)
		{
			character->char_id = -1;
			character->server = -1;
			// needed if player disconnects completely since Skotlex did not want to free the session
			character->pincode_success = false;
		}

		//FIXME? Why Kevin free'd the online information when the char was effectively in the map-server?
	}

	//Remove char if 1- Set all offline, or 2- character is no longer connected to char-server.
	if (char_id == -1 || character == NULL || character->fd == -1){
		chlogif_send_setaccoffline(login_fd,account_id);
	}
}

/**
 * @see DBApply
 */
int char_db_setoffline(DBKey key, DBData *data, va_list ap) {
	struct online_char_data* character = (struct online_char_data*)db_data2ptr(data);
	int server = va_arg(ap, int);
	if (server == -1) {
		character->char_id = -1;
		character->server = -1;
		if(character->waiting_disconnect != INVALID_TIMER){
			delete_timer(character->waiting_disconnect, char_chardb_waiting_disconnect);
			character->waiting_disconnect = INVALID_TIMER;
		}
	} else if (character->server == server)
		character->server = -2; //In some map server that we aren't connected to.
	return 0;
}

/**
 * @see DBApply
 */
static int char_db_kickoffline(DBKey key, DBData *data, va_list ap){
	struct online_char_data* character = (struct online_char_data*)db_data2ptr(data);
	int server_id = va_arg(ap, int);

	if (server_id > -1 && character->server != server_id)
		return 0;

	//Kick out any connected characters, and set them offline as appropriate.
	if (character->server > -1)
		mapif_disconnectplayer(map_server[character->server].fd, character->account_id, character->char_id, 1);
	else if (character->waiting_disconnect == INVALID_TIMER)
		char_set_char_offline(character->char_id, character->account_id);
	else
		return 0; // fail

	return 1;
}

void char_set_all_offline(int id){
	if (id < 0)
		ShowNotice("Sending all users offline.\n");
	else
		ShowNotice("Sending users of map-server %d offline.\n",id);
	online_char_db->foreach(online_char_db,char_db_kickoffline,id);

	if (id >= 0 || !chlogif_isconnected())
		return;
	//Tell login-server to also mark all our characters as offline.
	chlogif_send_setallaccoffline(login_fd);
}

void char_set_all_offline_sql(void){
	//Set all players to 'OFFLINE'
	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online` = '0'", schema_config.char_db) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `online` = '0'", schema_config.guild_member_db) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `connect_member` = '0'", schema_config.guild_db) )
		Sql_ShowDebug(sql_handle);
}

/**
 * @see DBCreateData
 */
static DBData char_create_charstatus(DBKey key, va_list args) {
	struct mmo_charstatus *cp;
	cp = (struct mmo_charstatus *) aCalloc(1,sizeof(struct mmo_charstatus));
	cp->char_id = key.i;
	return db_ptr2data(cp);
}

int char_inventory_to_sql(const struct item items[], int max, int id);

int char_mmo_char_tosql(int char_id, struct mmo_charstatus* p){
	int i = 0;
	int count = 0;
	int diff = 0;
	char save_status[128]; //For displaying save information. [Skotlex]
	struct mmo_charstatus *cp;
	int errors = 0; //If there are any errors while saving, "cp" will not be updated at the end.
	StringBuf buf;

	if (char_id!=p->char_id) return 0;

	cp = idb_ensure(char_db_, char_id, char_create_charstatus);

	StringBuf_Init(&buf);
	memset(save_status, 0, sizeof(save_status));

	//map inventory data
	if( memcmp(p->inventory, cp->inventory, sizeof(p->inventory)) ) {
		if (!char_inventory_to_sql(p->inventory, MAX_INVENTORY, p->char_id))
			strcat(save_status, " inventory");
		else
			errors++;
	}

	//map cart data
	if( memcmp(p->cart, cp->cart, sizeof(p->cart)) ) {
		if (!char_memitemdata_to_sql(p->cart, MAX_CART, p->char_id, TABLE_CART))
			strcat(save_status, " cart");
		else
			errors++;
	}

	//map storage data
	if( memcmp(p->storage.items, cp->storage.items, sizeof(p->storage.items)) ) {
		if (!char_memitemdata_to_sql(p->storage.items, MAX_STORAGE, p->account_id, TABLE_STORAGE))
			strcat(save_status, " storage");
		else
			errors++;
	}
	//map rentstorage data
	if( memcmp(p->ext_storage.items, cp->ext_storage.items, sizeof(p->ext_storage.items)) )
	{
		if( !char_memitemdata_to_sql(p->ext_storage.items, MAX_EXTRA_STORAGE, p->account_id, TABLE_EXT_STORAGE))
			strcat(save_status, " rentstorage");
		else
			errors++;
	}

	if (
		(p->base_exp != cp->base_exp) || (p->base_level != cp->base_level) ||
		(p->job_level != cp->job_level) || (p->job_exp != cp->job_exp) ||
		(p->zeny != cp->zeny) ||
		(p->last_point.map != cp->last_point.map) ||
		(p->last_point.x != cp->last_point.x) || (p->last_point.y != cp->last_point.y) ||
		(p->max_hp != cp->max_hp) || (p->hp != cp->hp) ||
		(p->max_sp != cp->max_sp) || (p->sp != cp->sp) ||
		(p->status_point != cp->status_point) || (p->skill_point != cp->skill_point) ||
		(p->str != cp->str) || (p->agi != cp->agi) || (p->vit != cp->vit) ||
		(p->int_ != cp->int_) || (p->dex != cp->dex) || (p->luk != cp->luk) ||
		(p->option != cp->option) ||
		(p->party_id != cp->party_id) || (p->guild_id != cp->guild_id) || (p->faction_id != cp->faction_id) ||
		(p->pet_id != cp->pet_id) || (p->weapon != cp->weapon) || (p->hom_id != cp->hom_id) ||
		(p->ele_id != cp->ele_id) || (p->shield != cp->shield) || (p->head_top != cp->head_top) ||
		(p->head_mid != cp->head_mid) || (p->head_bottom != cp->head_bottom) || (p->delete_date != cp->delete_date) ||
		(p->rename != cp->rename) || (p->robe != cp->robe) || (p->character_moves != cp->character_moves) ||
		(p->unban_time != cp->unban_time) || (p->font != cp->font) || (p->uniqueitem_counter != cp->uniqueitem_counter) || (p->playtime != cp->playtime)
	)
	{	//Save status
		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `base_level`='%d', `job_level`='%d',"
			"`base_exp`='%u', `job_exp`='%u', `zeny`='%d',"
			"`max_hp`='%d',`hp`='%d',`max_sp`='%d',`sp`='%d',`status_point`='%d',`skill_point`='%d',"
			"`str`='%d',`agi`='%d',`vit`='%d',`int`='%d',`dex`='%d',`luk`='%d',"
			"`option`='%d',`party_id`='%d',`guild_id`='%d',`pet_id`='%d',`homun_id`='%d',`elemental_id`='%d',`faction_id`='%d',"
			"`weapon`='%d',`shield`='%d',`head_top`='%d',`head_mid`='%d',`head_bottom`='%d',"
			"`last_map`='%s',`last_x`='%d',`last_y`='%d',`save_map`='%s',`save_x`='%d',`save_y`='%d', `rename`='%d',"
			"`delete_date`='%lu',`robe`='%d',`moves`='%d',`font`='%u',`uniqueitem_counter`='%u',`playtime`='%u'"
			" WHERE `account_id`='%d' AND `char_id` = '%d'",
			schema_config.char_db, p->base_level, p->job_level,
			p->base_exp, p->job_exp, p->zeny,
			p->max_hp, p->hp, p->max_sp, p->sp, p->status_point, p->skill_point,
			p->str, p->agi, p->vit, p->int_, p->dex, p->luk,
			p->option, p->party_id, p->guild_id, p->pet_id, p->hom_id, p->ele_id, p->faction_id,
			p->weapon, p->shield, p->head_top, p->head_mid, p->head_bottom,
			mapindex_id2name(p->last_point.map), p->last_point.x, p->last_point.y,
			mapindex_id2name(p->save_point.map), p->save_point.x, p->save_point.y, p->rename,
			(unsigned long)p->delete_date, // FIXME: platform-dependent size
			p->robe,p->character_moves,p->font, p->uniqueitem_counter,
			p->playtime,
			p->account_id, p->char_id) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " status");
	}

	//Values that will seldom change (to speed up saving)
	if (
		(p->hair != cp->hair) || (p->hair_color != cp->hair_color) || (p->clothes_color != cp->clothes_color) ||
		(p->class_ != cp->class_) ||
		(p->partner_id != cp->partner_id) || (p->father != cp->father) ||
		(p->mother != cp->mother) || (p->child != cp->child) ||
 		(p->karma != cp->karma) || (p->manner != cp->manner) ||
		(p->fame != cp->fame)
	)
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `class`='%d',"
			"`hair`='%d',`hair_color`='%d',`clothes_color`='%d',"
			"`partner_id`='%d', `father`='%d', `mother`='%d', `child`='%d',"
			"`karma`='%d',`manner`='%d', `fame`='%d'"
			" WHERE  `account_id`='%d' AND `char_id` = '%d'",
			schema_config.char_db, p->class_,
			p->hair, p->hair_color, p->clothes_color,
			p->partner_id, p->father, p->mother, p->child,
			p->karma, p->manner, p->fame,
			p->account_id, p->char_id) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " status2");
	}

	/* Player PVP Event Ranking */
	if( memcmp(&p->pvp, &cp->pvp, sizeof(struct s_killrank)) )
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "REPLACE INTO `char_pvp` (`char_id`, `kill_count`, `death_count`, `score`) VALUES ('%d', '%d', '%d', '%d')", p->char_id, p->pvp.kill_count, p->pvp.death_count, p->pvp.score) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " pvprank");
	}

	/* Player PK Ranking */
	if( memcmp(&p->pk, &cp->pk, sizeof(struct s_killrank)) )
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "REPLACE INTO `char_pk` (`char_id`, `kill_count`, `death_count`, `score`) VALUES ('%d', '%d', '%d', '%d')", p->char_id, p->pk.kill_count, p->pk.death_count, p->pk.score) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " pkrank");
	}

	/* Player Battleground Stadistics */
	if( memcmp(&p->bgstats, &cp->bgstats, sizeof(struct s_battleground_stats)) )
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "REPLACE INTO `char_bg` ("
			"`char_id`, "
			"`top_damage`, `damage_done`, `damage_received`, "
			"`skulls`, `ti_wins`, `ti_lost`, `ti_tie`, "
			"`eos_flags`, `eos_bases`, `eos_wins`, `eos_lost`, `eos_tie`, "
			"`boss_killed`, `boss_damage`, `boss_flags`, `boss_wins`, `boss_lost`, `boss_tie`, "
			"`dom_bases`, `dom_off_kills`, `dom_def_kills`, `dom_wins`, `dom_lost`, `dom_tie`, "
			"`td_kills`, `td_deaths`, `td_wins`, `td_lost`, `td_tie`, "
			"`sc_stole`, `sc_captured`, `sc_droped`, `sc_wins`, `sc_lost`, `sc_tie`, "
			"`ctf_taken`, `ctf_captured`, `ctf_droped`, `ctf_wins`, `ctf_lost`, `ctf_tie`, "
			"`emperium_kill`, `barricade_kill`, `gstone_kill`, `cq_wins`, `cq_lost`, "
			"`ru_captures`, `ru_wins`, `ru_lost`, "
			"`kill_count`, `death_count`, `win`, `lost`, `tie`, `leader_win`, `leader_lost`, `leader_tie`, `deserter`, `score`, `points`, `rank_points`, `rank_games`,"
			"`sp_heal_potions`, `hp_heal_potions`, `yellow_gemstones`, `red_gemstones`, `blue_gemstones`, `poison_bottles`, `acid_demostration`, `acid_demostration_fail`, "
			"`support_skills_used`, `healing_done`, `wrong_support_skills_used`, `wrong_healing_done`, "
			"`sp_used`, `zeny_used`, `spiritb_used`, `ammo_used`)"
			" VALUES "
			"('%d',"
			"'%u','%u','%u',"
			"'%d','%d','%d','%d',"
			"'%d','%d','%d','%d','%d',"
			"'%d','%u','%d','%d','%d','%d',"
			"'%d','%d','%d','%d','%d','%d',"
			"'%d','%d','%d','%d','%d',"
			"'%d','%d','%d','%d','%d','%d',"
			"'%d','%d','%d','%d','%d','%d',"
			"'%d','%d','%d','%d','%d',"
			"'%d','%d','%d',"
			"'%d','%d','%d','%d','%d','%d','%d','%d','%d','%d','%d','%d','%d',"
			"'%d','%d','%d','%d','%d','%d','%d','%d',"
			"'%d','%d','%d','%d',"
			"'%d','%d','%d','%d')",
			p->char_id,
			p->bgstats.top_damage, p->bgstats.damage_done, p->bgstats.damage_received,
			p->bgstats.skulls,p->bgstats.ti_wins,p->bgstats.ti_lost,p->bgstats.ti_tie,
			p->bgstats.eos_flags,p->bgstats.eos_bases,p->bgstats.eos_wins,p->bgstats.eos_lost,p->bgstats.eos_tie,
			p->bgstats.boss_killed,p->bgstats.boss_damage,p->bgstats.boss_flags,p->bgstats.boss_wins,p->bgstats.boss_lost,p->bgstats.boss_tie,
			p->bgstats.dom_bases,p->bgstats.dom_off_kills,p->bgstats.dom_def_kills,p->bgstats.dom_wins,p->bgstats.dom_lost,p->bgstats.dom_tie,
			p->bgstats.td_kills,p->bgstats.td_deaths,p->bgstats.td_wins,p->bgstats.td_lost,p->bgstats.td_tie,
			p->bgstats.sc_stole,p->bgstats.sc_captured,p->bgstats.sc_droped,p->bgstats.sc_wins,p->bgstats.sc_lost,p->bgstats.sc_tie,
			p->bgstats.ctf_taken,p->bgstats.ctf_captured,p->bgstats.ctf_droped,p->bgstats.ctf_wins,p->bgstats.ctf_lost,p->bgstats.ctf_tie,
			p->bgstats.emperium_kill,p->bgstats.barricade_kill,p->bgstats.gstone_kill,p->bgstats.cq_wins,p->bgstats.cq_lost,
			p->bgstats.ru_captures,p->bgstats.ru_wins,p->bgstats.ru_lost,
			p->bgstats.kill_count,p->bgstats.death_count,p->bgstats.win,p->bgstats.lost,p->bgstats.tie,p->bgstats.leader_win,p->bgstats.leader_lost,p->bgstats.leader_tie,p->bgstats.deserter,p->bgstats.score,p->bgstats.points,p->bgstats.rank_points,p->bgstats.rank_games,
			p->bgstats.sp_heal_potions, p->bgstats.hp_heal_potions, p->bgstats.yellow_gemstones, p->bgstats.red_gemstones, p->bgstats.blue_gemstones, p->bgstats.poison_bottles, p->bgstats.acid_demostration, p->bgstats.acid_demostration_fail,
			p->bgstats.support_skills_used, p->bgstats.healing_done, p->bgstats.wrong_support_skills_used, p->bgstats.wrong_healing_done,
			p->bgstats.sp_used, p->bgstats.zeny_used, p->bgstats.spiritb_used, p->bgstats.ammo_used) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " bgstats");
	}

	/* WoE Stadistics */
	if( memcmp(&p->wstats, &cp->wstats, sizeof(struct s_woestats)) )
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "REPLACE INTO `char_wstats` (`char_id`, `kill_count`, `death_count`, `score`, `top_damage`, `damage_done`, `damage_received`, `emperium_damage`, `guardian_damage`, `barricade_damage`, `gstone_damage`, "
			"`emperium_kill`, `guardian_kill`, `barricade_kill`, `gstone_kill`, "
			"`sp_heal_potions`, `hp_heal_potions`, `yellow_gemstones`, `red_gemstones`, `blue_gemstones`, `poison_bottles`, `acid_demostration`, `acid_demostration_fail`, "
			"`support_skills_used`, `healing_done`, `wrong_support_skills_used`, `wrong_healing_done`, "
			"`sp_used`, `zeny_used`, `spiritb_used`, `ammo_used`) "
			"VALUES ('%d', '%d', '%d', '%d', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%d', '%d', '%d', '%d', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u')",
			p->char_id, p->wstats.kill_count, p->wstats.death_count, p->wstats.score, p->wstats.top_damage, p->wstats.damage_done, p->wstats.damage_received, p->wstats.emperium_damage, p->wstats.guardian_damage, p->wstats.barricade_damage, p->wstats.gstone_damage,
			p->wstats.emperium_kill, p->wstats.guardian_kill, p->wstats.barricade_kill, p->wstats.gstone_kill,
			p->wstats.sp_heal_potions, p->wstats.hp_heal_potions, p->wstats.yellow_gemstones, p->wstats.red_gemstones, p->wstats.blue_gemstones, p->wstats.poison_bottles, p->wstats.acid_demostration, p->wstats.acid_demostration_fail,
			p->wstats.support_skills_used, p->wstats.healing_done, p->wstats.wrong_support_skills_used, p->wstats.wrong_healing_done,
			p->wstats.sp_used, p->wstats.zeny_used, p->wstats.spiritb_used, p->wstats.ammo_used) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " woestats");
	}

	/* Skill Usage */
	if( memcmp(&p->skillcount, &cp->skillcount, sizeof(p->skillcount)) )
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `skill_count` WHERE `char_id` = '%d'", p->char_id) )
		{
			Sql_ShowDebug(sql_handle); // Clear Data
			errors++;
		}
		StringBuf_Clear(&buf);
		StringBuf_Printf(&buf, "INSERT INTO `skill_count` (`char_id`,`id`,`count`) VALUES ");
		//insert here.
		for( i = 0, count = 0; i < MAX_SKILL_TREE; ++i )
		{
			if( p->skillcount[i].id && p->skillcount[i].count > 0 )
			{
				if( count )
					StringBuf_AppendStr(&buf, ",");
				StringBuf_Printf(&buf, "('%d','%d','%d')", char_id, p->skillcount[i].id, p->skillcount[i].count);
				++count;
			}
		}
		if( count )
		{
			if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
			{
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}
		strcat(save_status, " skillcount");
	}

	/* BG Skill Usage */
	if( memcmp(&p->bg_skillcount, &cp->bg_skillcount, sizeof(p->bg_skillcount)) )
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `bg_skill_count` WHERE `char_id` = '%d'", p->char_id) )
		{
			Sql_ShowDebug(sql_handle); // Clear Data
			errors++;
		}
		StringBuf_Clear(&buf);
		StringBuf_Printf(&buf, "INSERT INTO `bg_skill_count` (`char_id`,`id`,`count`) VALUES ");
		//insert here.
		for( i = 0, count = 0; i < MAX_SKILL_TREE; ++i )
		{
			if( p->bg_skillcount[i].id && p->bg_skillcount[i].count > 0 )
			{
				if( count )
					StringBuf_AppendStr(&buf, ",");
				StringBuf_Printf(&buf, "('%d','%d','%d')", char_id, p->bg_skillcount[i].id, p->bg_skillcount[i].count);
				++count;
			}
		}
		if( count )
		{
			if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
			{
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}
		strcat(save_status, " bg_skillcount");
	}

	/* Mercenary Owner */
	if( (p->mer_id != cp->mer_id) ||
		(p->arch_calls != cp->arch_calls) || (p->arch_faith != cp->arch_faith) ||
		(p->spear_calls != cp->spear_calls) || (p->spear_faith != cp->spear_faith) ||
		(p->sword_calls != cp->sword_calls) || (p->sword_faith != cp->sword_faith) )
	{
		if (mercenary_owner_tosql(char_id, p))
			strcat(save_status, " mercenary");
		else
			errors++;
	}

	//memo points
	if( memcmp(p->memo_point, cp->memo_point, sizeof(p->memo_point)) )
	{
		char esc_mapname[NAME_LENGTH*2+1];

		//`memo` (`memo_id`,`char_id`,`map`,`x`,`y`)
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", schema_config.memo_db, p->char_id) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		}

		//insert here.
		StringBuf_Clear(&buf);
		StringBuf_Printf(&buf, "INSERT INTO `%s`(`char_id`,`map`,`x`,`y`) VALUES ", schema_config.memo_db);
		for( i = 0, count = 0; i < MAX_MEMOPOINTS; ++i )
		{
			if( p->memo_point[i].map )
			{
				if( count )
					StringBuf_AppendStr(&buf, ",");
				Sql_EscapeString(sql_handle, esc_mapname, mapindex_id2name(p->memo_point[i].map));
				StringBuf_Printf(&buf, "('%d', '%s', '%d', '%d')", char_id, esc_mapname, p->memo_point[i].x, p->memo_point[i].y);
				++count;
			}
		}
		if( count )
		{
			if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
			{
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}
		strcat(save_status, " memo");
	}

	//FIXME: is this neccessary? [ultramage]
	for(i=0;i<MAX_SKILL;i++)
		if ((p->skill[i].lv != 0) && (p->skill[i].id == 0))
			p->skill[i].id = i; // Fix skill tree


	//skills
	if( memcmp(p->skill, cp->skill, sizeof(p->skill)) )
	{
		//`skill` (`char_id`, `id`, `lv`)
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", schema_config.skill_db, p->char_id) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		}

		StringBuf_Clear(&buf);
		StringBuf_Printf(&buf, "INSERT INTO `%s`(`char_id`,`id`,`lv`,`flag`) VALUES ", schema_config.skill_db);
		//insert here.
		for( i = 0, count = 0; i < MAX_SKILL; ++i ) {
			if( p->skill[i].id != 0 && p->skill[i].flag != SKILL_FLAG_TEMPORARY ) {

				if( p->skill[i].lv == 0 && ( p->skill[i].flag == SKILL_FLAG_PERM_GRANTED || p->skill[i].flag == SKILL_FLAG_PERMANENT ) )
					continue;
				if( p->skill[i].flag != SKILL_FLAG_PERMANENT && p->skill[i].flag != SKILL_FLAG_PERM_GRANTED && (p->skill[i].flag - SKILL_FLAG_REPLACED_LV_0) == 0 )
					continue;
				if( count )
					StringBuf_AppendStr(&buf, ",");
				StringBuf_Printf(&buf, "('%d','%d','%d','%d')", char_id, p->skill[i].id,
								 ( (p->skill[i].flag == SKILL_FLAG_PERMANENT || p->skill[i].flag == SKILL_FLAG_PERM_GRANTED) ? p->skill[i].lv : p->skill[i].flag - SKILL_FLAG_REPLACED_LV_0),
								 p->skill[i].flag == SKILL_FLAG_PERM_GRANTED ? p->skill[i].flag : 0);/* other flags do not need to be saved */
				++count;
			}
		}
		if( count )
		{
			if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
			{
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}

		strcat(save_status, " skills");
	}

	diff = 0;
	for(i = 0; i < MAX_FRIENDS; i++){
		if(p->friends[i].char_id != cp->friends[i].char_id ||
			p->friends[i].account_id != cp->friends[i].account_id){
			diff = 1;
			break;
		}
	}

	if(diff == 1)
	{	//Save friends
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", schema_config.friend_db, char_id) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		}

		StringBuf_Clear(&buf);
		StringBuf_Printf(&buf, "INSERT INTO `%s` (`char_id`, `friend_account`, `friend_id`) VALUES ", schema_config.friend_db);
		for( i = 0, count = 0; i < MAX_FRIENDS; ++i )
		{
			if( p->friends[i].char_id > 0 )
			{
				if( count )
					StringBuf_AppendStr(&buf, ",");
				StringBuf_Printf(&buf, "('%d','%d','%d')", char_id, p->friends[i].account_id, p->friends[i].char_id);
				count++;
			}
		}
		if( count )
		{
			if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
			{
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}
		strcat(save_status, " friends");
	}

#ifdef HOTKEY_SAVING
	// hotkeys
	StringBuf_Clear(&buf);
	StringBuf_Printf(&buf, "REPLACE INTO `%s` (`char_id`, `hotkey`, `type`, `itemskill_id`, `skill_lvl`) VALUES ", schema_config.hotkey_db);
	diff = 0;
	for(i = 0; i < ARRAYLENGTH(p->hotkeys); i++){
		if(memcmp(&p->hotkeys[i], &cp->hotkeys[i], sizeof(struct hotkey)))
		{
			if( diff )
				StringBuf_AppendStr(&buf, ",");// not the first hotkey
			StringBuf_Printf(&buf, "('%d','%u','%u','%u','%u')", char_id, (unsigned int)i, (unsigned int)p->hotkeys[i].type, p->hotkeys[i].id , (unsigned int)p->hotkeys[i].lv);
			diff = 1;
		}
	}
	if(diff) {
		if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
		{
			Sql_ShowDebug(sql_handle);
			errors++;
		} else
			strcat(save_status, " hotkeys");
	}
#endif
	StringBuf_Destroy(&buf);
	if (save_status[0]!='\0' && charserv_config.save_log)
		ShowInfo("Saved char %d - %s:%s.\n", char_id, p->name, save_status);
	if (!errors)
		memcpy(cp, p, sizeof(struct mmo_charstatus));
	return 0;
}

/// Saves an array of 'item' entries into the specified table.
int char_memitemdata_to_sql(const struct item items[], int max, int id, int tableswitch){
	StringBuf buf;
	SqlStmt* stmt;
	int i;
	int j;
	const char* tablename;
	const char* selectoption;
	struct item item; // temp storage variable
	bool* flag; // bit array for inventory matching
	bool found;
	int errors = 0;

	switch (tableswitch) {
	case TABLE_INVENTORY:     tablename = schema_config.inventory_db;     selectoption = "char_id";    break;
	case TABLE_CART:          tablename = schema_config.cart_db;          selectoption = "char_id";    break;
	case TABLE_STORAGE:       tablename = schema_config.storage_db;       selectoption = "account_id"; break;
	case TABLE_GUILD_STORAGE: tablename = schema_config.guild_storage_db; selectoption = "guild_id";   break;
	case TABLE_EXT_STORAGE:   tablename = schema_config.rentstorage_db;   selectoption = "account_id"; break;
	default:
		ShowError("Invalid table name!\n");
		return 1;
	}


	// The following code compares inventory with current database values
	// and performs modification/deletion/insertion only on relevant rows.
	// This approach is more complicated than a trivial delete&insert, but
	// it significantly reduces cpu load on the database server.

	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `unique_id`, `bound`, `favorite`");
	for( j = 0; j < MAX_SLOTS; ++j )
		StringBuf_Printf(&buf, ", `card%d`", j);
	StringBuf_Printf(&buf, " FROM `%s` WHERE `%s`='%d'", tablename, selectoption, id);

	stmt = SqlStmt_Malloc(sql_handle);
	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf))
	||  SQL_ERROR == SqlStmt_Execute(stmt) )
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		StringBuf_Destroy(&buf);
		return 1;
	}

	SqlStmt_BindColumn(stmt, 0, SQLDT_INT,       &item.id,          0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,    &item.nameid,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,     &item.amount,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,      &item.equip,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,      &item.identify,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,      &item.refine,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,      &item.attribute,   0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,      &item.expire_time, 0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 8, SQLDT_ULONGLONG, &item.unique_id,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 9, SQLDT_CHAR,      &item.bound,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt,10, SQLDT_CHAR,      &item.favorite,    0, NULL, NULL);
	for( j = 0; j < MAX_SLOTS; ++j )
		SqlStmt_BindColumn(stmt, 11+j, SQLDT_USHORT, &item.card[j], 0, NULL, NULL);

	// bit array indicating which inventory items have already been matched
	flag = (bool*) aCalloc(max, sizeof(bool));

	while( SQL_SUCCESS == SqlStmt_NextRow(stmt) )
	{
		found = false;
		// search for the presence of the item in the char's inventory
		for( i = 0; i < max; ++i )
		{
			// skip empty and already matched entries
			if( items[i].nameid == 0 || flag[i] )
				continue;
			if( items[i].unique_id && items[i].unique_id != item.unique_id )
				continue;

			if( items[i].nameid == item.nameid
			&&  items[i].card[0] == item.card[0]
			&&  items[i].card[2] == item.card[2]
			&&  items[i].card[3] == item.card[3]
			) {	//They are the same item.
				ARR_FIND( 0, MAX_SLOTS, j, items[i].card[j] != item.card[j] );
				if( j == MAX_SLOTS &&
				    items[i].amount == item.amount &&
				    items[i].equip == item.equip &&
				    items[i].identify == item.identify &&
				    items[i].refine == item.refine &&
				    items[i].attribute == item.attribute &&
				    items[i].unique_id == item.unique_id &&
				    items[i].expire_time == item.expire_time &&
					items[i].favorite == item.favorite &&
					items[i].bound == item.bound )
				;	//Do nothing - Same data on DB and Memory
				else
				{ // Differences founds, updated required
					StringBuf_Clear(&buf);
					StringBuf_Printf(&buf, "UPDATE `%s` SET `amount`='%d', `equip`='%d', `identify`='%d', `refine`='%d',`attribute`='%d', `expire_time`='%u', `unique_id`='%"PRIu64"', `bound`='%d', `favorite`='%d'",
						tablename, items[i].amount, items[i].equip, items[i].identify, items[i].refine, items[i].attribute, items[i].expire_time, items[i].unique_id, items[i].bound, items[i].favorite);
					for( j = 0; j < MAX_SLOTS; ++j )
						StringBuf_Printf(&buf, ", `card%d`=%hu", j, items[i].card[j]);
					StringBuf_Printf(&buf, " WHERE `id`='%d' LIMIT 1", item.id);

					if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
					{
						Sql_ShowDebug(sql_handle);
						errors++;
					}
				}

				found = flag[i] = true; //Item dealt with,
				break; //skip to next item in the db.
			}
		}
		if( !found )
		{// Item not present in inventory, remove it.
			if( SQL_ERROR == Sql_Query(sql_handle, "DELETE from `%s` where `id`='%d' LIMIT 1", tablename, item.id) )
			{
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}
	}
	SqlStmt_Free(stmt);

	StringBuf_Clear(&buf);
	StringBuf_Printf(&buf, "INSERT INTO `%s`(`%s`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `unique_id`, `bound`, `favorite`", tablename, selectoption);
	for( j = 0; j < MAX_SLOTS; ++j )
		StringBuf_Printf(&buf, ", `card%d`", j);
	StringBuf_AppendStr(&buf, ") VALUES ");

	found = false;
	// insert non-matched items into the db as new items
	for( i = 0; i < max; ++i )
	{
		// skip empty and already matched entries
		if( items[i].nameid == 0 || flag[i] )
			continue;

		if( found )
			StringBuf_AppendStr(&buf, ",");
		else
			found = true;

		StringBuf_Printf(&buf, "('%d', '%hu', '%d', '%d', '%d', '%d', '%d', '%u', '%"PRIu64"', '%d', '%d'",
			id, items[i].nameid, items[i].amount, items[i].equip, items[i].identify, items[i].refine, items[i].attribute, items[i].expire_time, items[i].unique_id, items[i].bound, items[i].favorite);
		for( j = 0; j < MAX_SLOTS; ++j )
			StringBuf_Printf(&buf, ", '%hu'", items[i].card[j]);
		StringBuf_AppendStr(&buf, ")");
	}

	if( found && SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) )
	{
		Sql_ShowDebug(sql_handle);
		errors++;
	}

	StringBuf_Destroy(&buf);
	aFree(flag);

	return errors;
}
/* pretty much a copy of memitemdata_to_sql except it handles inventory_db exclusively,
 * - this is required because inventory db is the only one with the 'favorite' column. */
int char_inventory_to_sql(const struct item items[], int max, int id) {
	StringBuf buf;
	SqlStmt* stmt;
	int i;
	int j;
	struct item item; // temp storage variable
	bool* flag; // bit array for inventory matching
	bool found;
	int errors = 0;


	// The following code compares inventory with current database values
	// and performs modification/deletion/insertion only on relevant rows.
	// This approach is more complicated than a trivial delete&insert, but
	// it significantly reduces cpu load on the database server.

	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `favorite`, `bound`");
	for( j = 0; j < MAX_SLOTS; ++j )
		StringBuf_Printf(&buf, ", `card%d`", j);
	StringBuf_Printf(&buf, " FROM `%s` WHERE `char_id`='%d'", schema_config.inventory_db, id);

	stmt = SqlStmt_Malloc(sql_handle);
	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf))
	   ||  SQL_ERROR == SqlStmt_Execute(stmt) )
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		StringBuf_Destroy(&buf);
		return 1;
	}

	SqlStmt_BindColumn(stmt, 0, SQLDT_INT,       &item.id,          0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,    &item.nameid,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,     &item.amount,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,      &item.equip,       0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,      &item.identify,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,      &item.refine,      0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,      &item.attribute,   0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,      &item.expire_time, 0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 8, SQLDT_CHAR,      &item.favorite,    0, NULL, NULL);
	SqlStmt_BindColumn(stmt, 9, SQLDT_CHAR,      &item.bound,       0, NULL, NULL);
	for( j = 0; j < MAX_SLOTS; ++j )
		SqlStmt_BindColumn(stmt, 10+j, SQLDT_USHORT, &item.card[j], 0, NULL, NULL);

	// bit array indicating which inventory items have already been matched
	flag = (bool*) aCalloc(max, sizeof(bool));

	while( SQL_SUCCESS == SqlStmt_NextRow(stmt) ) {
		found = false;
		// search for the presence of the item in the char's inventory
		for( i = 0; i < max; ++i ) {
			// skip empty and already matched entries
			if( items[i].nameid == 0 || flag[i] )
				continue;

			if( items[i].nameid == item.nameid
			   &&  items[i].card[0] == item.card[0]
			   &&  items[i].card[2] == item.card[2]
			   &&  items[i].card[3] == item.card[3]
			   ) {	//They are the same item.
				ARR_FIND( 0, MAX_SLOTS, j, items[i].card[j] != item.card[j] );
				if( j == MAX_SLOTS &&
				   items[i].amount == item.amount &&
				   items[i].equip == item.equip &&
				   items[i].identify == item.identify &&
				   items[i].refine == item.refine &&
				   items[i].attribute == item.attribute &&
				   items[i].expire_time == item.expire_time &&
				   items[i].favorite == item.favorite &&
				   items[i].bound == item.bound )
					;	//Do nothing.
				else {
					// update all fields.
					StringBuf_Clear(&buf);
					StringBuf_Printf(&buf, "UPDATE `%s` SET `amount`='%d', `equip`='%d', `identify`='%d', `refine`='%d',`attribute`='%d', `expire_time`='%u', `favorite`='%d', `bound`='%d'",
					    schema_config.inventory_db, items[i].amount, items[i].equip, items[i].identify, items[i].refine, items[i].attribute, items[i].expire_time, items[i].favorite, items[i].bound);
					for( j = 0; j < MAX_SLOTS; ++j )
						StringBuf_Printf(&buf, ", `card%d`=%hu", j, items[i].card[j]);
					StringBuf_Printf(&buf, " WHERE `id`='%d' LIMIT 1", item.id);

					if( SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) ) {
						Sql_ShowDebug(sql_handle);
						errors++;
					}
				}

				found = flag[i] = true; //Item dealt with,
				break; //skip to next item in the db.
			}
		}
		if( !found ) {// Item not present in inventory, remove it.
			if( SQL_ERROR == Sql_Query(sql_handle, "DELETE from `%s` where `id`='%d' LIMIT 1", schema_config.inventory_db, item.id) ) {
				Sql_ShowDebug(sql_handle);
				errors++;
			}
		}
	}
	SqlStmt_Free(stmt);

	StringBuf_Clear(&buf);
	StringBuf_Printf(&buf, "INSERT INTO `%s` (`char_id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `favorite`, `bound`, `unique_id`", schema_config.inventory_db);
	for( j = 0; j < MAX_SLOTS; ++j )
		StringBuf_Printf(&buf, ", `card%d`", j);
	StringBuf_AppendStr(&buf, ") VALUES ");

	found = false;
	// insert non-matched items into the db as new items
	for( i = 0; i < max; ++i ) {
		// skip empty and already matched entries
		if( items[i].nameid == 0 || flag[i] )
			continue;

		if( found )
			StringBuf_AppendStr(&buf, ",");
		else
			found = true;

		StringBuf_Printf(&buf, "('%d', '%hu', '%d', '%d', '%d', '%d', '%d', '%u', '%d', '%d', '%"PRIu64"'",
						 id, items[i].nameid, items[i].amount, items[i].equip, items[i].identify, items[i].refine, items[i].attribute, items[i].expire_time, items[i].favorite, items[i].bound, items[i].unique_id);
		for( j = 0; j < MAX_SLOTS; ++j )
			StringBuf_Printf(&buf, ", '%hu'", items[i].card[j]);
		StringBuf_AppendStr(&buf, ")");
	}

	if( found && SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf)) ) {
		Sql_ShowDebug(sql_handle);
		errors++;
	}

	StringBuf_Destroy(&buf);
	aFree(flag);

	return errors;
}


int char_mmo_char_tobuf(uint8* buf, struct mmo_charstatus* p);

//=====================================================================================================
// Loads the basic character rooster for the given account. Returns total buffer used.
int char_mmo_chars_fromsql(struct char_session_data* sd, uint8* buf) {
	SqlStmt* stmt;
	struct mmo_charstatus p;
	int j = 0, i;
	char last_map[MAP_NAME_LENGTH_EXT];

	stmt = SqlStmt_Malloc(sql_handle);
	if( stmt == NULL )
	{
		SqlStmt_ShowDebug(stmt);
		return 0;
	}
	memset(&p, 0, sizeof(p));

	for( i = 0; i < MAX_CHARS; i++ ){
		sd->found_char[i] = -1;
		sd->unban_time[i] = 0;
	}

	// read char data
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT "
		"`char_id`,`char_num`,`name`,`class`,`base_level`,`job_level`,`base_exp`,`job_exp`,`zeny`,"
		"`str`,`agi`,`vit`,`int`,`dex`,`luk`,`max_hp`,`hp`,`max_sp`,`sp`,"
		"`status_point`,`skill_point`,`option`,`karma`,`manner`,`hair`,`hair_color`,"
		"`clothes_color`,`weapon`,`shield`,`head_top`,`head_mid`,`head_bottom`,`last_map`,`rename`,`delete_date`,"
		"`robe`,`moves`,`unban_time`,`font`,`uniqueitem_counter`"
		" FROM `%s` WHERE `account_id`='%d' AND `char_num` < '%d'", schema_config.char_db, sd->account_id, MAX_CHARS)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0,  SQLDT_INT,    &p.char_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1,  SQLDT_UCHAR,  &p.slot, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2,  SQLDT_STRING, &p.name, sizeof(p.name), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 3,  SQLDT_SHORT,  &p.class_, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 4,  SQLDT_UINT,   &p.base_level, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 5,  SQLDT_UINT,   &p.job_level, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 6,  SQLDT_UINT,   &p.base_exp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 7,  SQLDT_UINT,   &p.job_exp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 8,  SQLDT_INT,    &p.zeny, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 9,  SQLDT_SHORT,  &p.str, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 10, SQLDT_SHORT,  &p.agi, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 11, SQLDT_SHORT,  &p.vit, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 12, SQLDT_SHORT,  &p.int_, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 13, SQLDT_SHORT,  &p.dex, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 14, SQLDT_SHORT,  &p.luk, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 15, SQLDT_INT,    &p.max_hp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 16, SQLDT_INT,    &p.hp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 17, SQLDT_INT,    &p.max_sp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 18, SQLDT_INT,    &p.sp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 19, SQLDT_UINT,   &p.status_point, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 20, SQLDT_UINT,   &p.skill_point, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 21, SQLDT_UINT,   &p.option, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 22, SQLDT_UCHAR,  &p.karma, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 23, SQLDT_SHORT,  &p.manner, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 24, SQLDT_SHORT,  &p.hair, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 25, SQLDT_SHORT,  &p.hair_color, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 26, SQLDT_SHORT,  &p.clothes_color, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 27, SQLDT_SHORT,  &p.weapon, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 28, SQLDT_SHORT,  &p.shield, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 29, SQLDT_SHORT,  &p.head_top, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 30, SQLDT_SHORT,  &p.head_mid, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 31, SQLDT_SHORT,  &p.head_bottom, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 32, SQLDT_STRING, &last_map, sizeof(last_map), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 33, SQLDT_SHORT,	&p.rename, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 34, SQLDT_UINT32, &p.delete_date, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 35, SQLDT_SHORT,  &p.robe, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 36, SQLDT_UINT,   &p.character_moves, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 37, SQLDT_LONG,   &p.unban_time, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 38, SQLDT_UCHAR,  &p.font, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 39, SQLDT_UINT,   &p.uniqueitem_counter, 0, NULL, NULL)
	)
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		return 0;
	}

	for( i = 0; i < MAX_CHARS && SQL_SUCCESS == SqlStmt_NextRow(stmt); i++ )
	{
		p.last_point.map = mapindex_name2id(last_map);
		sd->found_char[p.slot] = p.char_id;
		sd->unban_time[p.slot] = p.unban_time;
		j += char_mmo_char_tobuf(WBUFP(buf, j), &p);

		// Addon System
		// store the required info into the session
		sd->char_moves[p.slot] = p.character_moves;
	}

	memset(sd->new_name,0,sizeof(sd->new_name));

	SqlStmt_Free(stmt);
	return j;
}

//=====================================================================================================
void char_ip_premium(uint32 ip, struct mmo_charstatus* p)
{
	char* data = NULL;
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `level` FROM `ippremium` WHERE `ip` = '%s'", ip2str(ip, NULL)) )
	{
		Sql_ShowDebug(sql_handle);
		p->iprank = 0;
		return;
	}

	if( SQL_SUCCESS == Sql_NextRow(sql_handle) )
	{
		Sql_GetData(sql_handle, 0, &data, NULL);
		if( (p->iprank = atoi(data)) > 0 )
			ShowStatus("IP Premium System: CharID %d Name %s using level %d.", p->char_id, p->name, p->iprank);
	}

	Sql_FreeResult(sql_handle);
}

int char_mmo_char_fromsql(int char_id, struct mmo_charstatus* p, bool load_everything) {
	int i,j;
	char t_msg[128] = "";
	struct mmo_charstatus* cp;
	StringBuf buf;
	SqlStmt* stmt;
	char last_map[MAP_NAME_LENGTH_EXT];
	char save_map[MAP_NAME_LENGTH_EXT];
	char point_map[MAP_NAME_LENGTH_EXT];
	struct point tmp_point;
	struct item tmp_item;
	struct s_skill tmp_skill;
	struct s_skillcount tmp_skillcount;
	struct s_friend tmp_friend;
#ifdef HOTKEY_SAVING
	struct hotkey tmp_hotkey;
	int hotkey_num;
#endif

	memset(p, 0, sizeof(struct mmo_charstatus));

	if (charserv_config.save_log) ShowInfo("Char load request (%d)\n", char_id);

	stmt = SqlStmt_Malloc(sql_handle);
	if( stmt == NULL )
	{
		SqlStmt_ShowDebug(stmt);
		return 0;
	}

	// read char data
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT "
		"`char_id`,`account_id`,`char_num`,`name`,`class`,`base_level`,`job_level`,`base_exp`,`job_exp`,`zeny`,"
		"`str`,`agi`,`vit`,`int`,`dex`,`luk`,`max_hp`,`hp`,`max_sp`,`sp`,"
		"`status_point`,`skill_point`,`option`,`karma`,`manner`,`party_id`,`guild_id`,`pet_id`,`homun_id`,`elemental_id`,`hair`,"
		"`hair_color`,`clothes_color`,`weapon`,`shield`,`head_top`,`head_mid`,`head_bottom`,`last_map`,`last_x`,`last_y`,"
		"`save_map`,`save_x`,`save_y`,`partner_id`,`father`,`mother`,`child`,`fame`,`rename`,`delete_date`,`robe`, `moves`,"
		"`unban_time`,`font`,`uniqueitem_counter`,`playtime`,`faction_id`"
		" FROM `%s` WHERE `char_id`=? LIMIT 1", schema_config.char_db)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0,  SQLDT_INT,    &p->char_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1,  SQLDT_INT,    &p->account_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2,  SQLDT_UCHAR,  &p->slot, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 3,  SQLDT_STRING, &p->name, sizeof(p->name), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 4,  SQLDT_SHORT,  &p->class_, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 5,  SQLDT_UINT,   &p->base_level, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 6,  SQLDT_UINT,   &p->job_level, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 7,  SQLDT_UINT,   &p->base_exp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 8,  SQLDT_UINT,   &p->job_exp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 9,  SQLDT_INT,    &p->zeny, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 10, SQLDT_SHORT,  &p->str, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 11, SQLDT_SHORT,  &p->agi, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 12, SQLDT_SHORT,  &p->vit, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 13, SQLDT_SHORT,  &p->int_, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 14, SQLDT_SHORT,  &p->dex, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 15, SQLDT_SHORT,  &p->luk, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 16, SQLDT_INT,    &p->max_hp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 17, SQLDT_INT,    &p->hp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 18, SQLDT_INT,    &p->max_sp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 19, SQLDT_INT,    &p->sp, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 20, SQLDT_UINT,   &p->status_point, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 21, SQLDT_UINT,   &p->skill_point, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 22, SQLDT_UINT,   &p->option, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 23, SQLDT_UCHAR,  &p->karma, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 24, SQLDT_SHORT,  &p->manner, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 25, SQLDT_INT,    &p->party_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 26, SQLDT_INT,    &p->guild_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 27, SQLDT_INT,    &p->pet_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 28, SQLDT_INT,    &p->hom_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 29, SQLDT_INT,    &p->ele_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 30, SQLDT_SHORT,  &p->hair, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 31, SQLDT_SHORT,  &p->hair_color, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 32, SQLDT_SHORT,  &p->clothes_color, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 33, SQLDT_SHORT,  &p->weapon, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 34, SQLDT_SHORT,  &p->shield, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 35, SQLDT_SHORT,  &p->head_top, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 36, SQLDT_SHORT,  &p->head_mid, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 37, SQLDT_SHORT,  &p->head_bottom, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 38, SQLDT_STRING, &last_map, sizeof(last_map), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 39, SQLDT_SHORT,  &p->last_point.x, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 40, SQLDT_SHORT,  &p->last_point.y, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 41, SQLDT_STRING, &save_map, sizeof(save_map), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 42, SQLDT_SHORT,  &p->save_point.x, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 43, SQLDT_SHORT,  &p->save_point.y, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 44, SQLDT_INT,    &p->partner_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 45, SQLDT_INT,    &p->father, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 46, SQLDT_INT,    &p->mother, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 47, SQLDT_INT,    &p->child, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 48, SQLDT_INT,    &p->fame, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 49, SQLDT_SHORT,  &p->rename, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 50, SQLDT_UINT32, &p->delete_date, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 51, SQLDT_SHORT,  &p->robe, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 52, SQLDT_UINT32, &p->character_moves, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 53, SQLDT_LONG,   &p->unban_time, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 54, SQLDT_UCHAR,  &p->font, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 55, SQLDT_UINT,   &p->uniqueitem_counter, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 56, SQLDT_UINT,   &p->playtime, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 57, SQLDT_INT,	&p->faction_id, 0, NULL, NULL)
	)
	{
		SqlStmt_ShowDebug(stmt);
		SqlStmt_Free(stmt);
		return 0;
	}
	if( SQL_ERROR == SqlStmt_NextRow(stmt) )
	{
		ShowError("Requested non-existant character id: %d!\n", char_id);
		SqlStmt_Free(stmt);
		return 0;
	}
	p->last_point.map = mapindex_name2id(last_map);
	p->save_point.map = mapindex_name2id(save_map);

	if( p->last_point.map == 0 ) {
		p->last_point.map = mapindex_name2id(MAP_DEFAULT);
		p->last_point.x = MAP_DEFAULT_X;
		p->last_point.y = MAP_DEFAULT_Y;
	}

	if( p->save_point.map == 0 ) {
		p->save_point.map = mapindex_name2id(MAP_DEFAULT);
		p->save_point.x = MAP_DEFAULT_X;
		p->save_point.y = MAP_DEFAULT_Y;
	}

	strcat(t_msg, " status");

	if (!load_everything) // For quick selection of data when displaying the char menu
	{
		SqlStmt_Free(stmt);
		return 1;
	}

	//read memo data
	//`memo` (`memo_id`,`char_id`,`map`,`x`,`y`)
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `map`,`x`,`y` FROM `%s` WHERE `char_id`=? ORDER by `memo_id` LIMIT %d", schema_config.memo_db, MAX_MEMOPOINTS)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_STRING, &point_map, sizeof(point_map), NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_SHORT,  &tmp_point.x, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,  &tmp_point.y, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	for( i = 0; i < MAX_MEMOPOINTS && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
	{
		tmp_point.map = mapindex_name2id(point_map);
		memcpy(&p->memo_point[i], &tmp_point, sizeof(tmp_point));
	}
	strcat(t_msg, " memo");

	//read inventory
	//`inventory` (`id`,`char_id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `card0`, `card1`, `card2`, `card3`, `expire_time`, `favorite`, `unique_id`)
	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `favorite`, `bound`, `unique_id`");
	for( i = 0; i < MAX_SLOTS; ++i )
		StringBuf_Printf(&buf, ", `card%d`", i);
	StringBuf_Printf(&buf, " FROM `%s` WHERE `char_id`=? LIMIT %d", schema_config.inventory_db, MAX_INVENTORY);

	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf))
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_INT,       &tmp_item.id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,    &tmp_item.nameid, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,     &tmp_item.amount, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,      &tmp_item.equip, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,      &tmp_item.identify, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,      &tmp_item.refine, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,      &tmp_item.attribute, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,      &tmp_item.expire_time, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 8, SQLDT_CHAR,      &tmp_item.favorite, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 9, SQLDT_CHAR,      &tmp_item.bound, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt,10, SQLDT_ULONGLONG, &tmp_item.unique_id, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);
	for( i = 0; i < MAX_SLOTS; ++i )
		if( SQL_ERROR == SqlStmt_BindColumn(stmt, 11+i, SQLDT_USHORT, &tmp_item.card[i], 0, NULL, NULL) )
			SqlStmt_ShowDebug(stmt);

	for( i = 0; i < MAX_INVENTORY && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->inventory[i], &tmp_item, sizeof(tmp_item));

	strcat(t_msg, " inventory");

	//read cart
	//`cart_inventory` (`id`,`char_id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `card0`, `card1`, `card2`, `card3`, expire_time`, `unique_id`)
	StringBuf_Clear(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `expire_time`, `unique_id`, `bound`, `favorite`");
	for( j = 0; j < MAX_SLOTS; ++j )
		StringBuf_Printf(&buf, ", `card%d`", j);
	StringBuf_Printf(&buf, " FROM `%s` WHERE `char_id`=? LIMIT %d", schema_config.cart_db, MAX_CART);

	if( SQL_ERROR == SqlStmt_PrepareStr(stmt, StringBuf_Value(&buf))
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_INT,         &tmp_item.id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT,      &tmp_item.nameid, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_SHORT,       &tmp_item.amount, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 3, SQLDT_UINT,        &tmp_item.equip, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 4, SQLDT_CHAR,        &tmp_item.identify, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 5, SQLDT_CHAR,        &tmp_item.refine, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 6, SQLDT_CHAR,        &tmp_item.attribute, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 7, SQLDT_UINT,        &tmp_item.expire_time, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 8, SQLDT_ULONGLONG,   &tmp_item.unique_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 9, SQLDT_CHAR,        &tmp_item.bound, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt,10, SQLDT_CHAR,        &tmp_item.favorite, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);
	for( i = 0; i < MAX_SLOTS; ++i )
		if( SQL_ERROR == SqlStmt_BindColumn(stmt, 11+i, SQLDT_USHORT, &tmp_item.card[i], 0, NULL, NULL) )
			SqlStmt_ShowDebug(stmt);

	for( i = 0; i < MAX_CART && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->cart[i], &tmp_item, sizeof(tmp_item));
	strcat(t_msg, " cart");

	//read storage
	storage_fromsql(p->account_id, &p->storage);
	strcat(t_msg, " storage");

	//read rentstorage
	ext_storage_fromsql(p->account_id, &p->ext_storage);
	strcat(t_msg, " rentstorage");

	//read skill
	//`skill` (`char_id`, `id`, `lv`)
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `id`, `lv`,`flag` FROM `%s` WHERE `char_id`=? LIMIT %d", schema_config.skill_db, MAX_SKILL)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_USHORT, &tmp_skill.id  , 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_UCHAR , &tmp_skill.lv  , 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_UCHAR , &tmp_skill.flag, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	if( tmp_skill.flag != SKILL_FLAG_PERM_GRANTED )
		tmp_skill.flag = SKILL_FLAG_PERMANENT;

	for( i = 0; i < MAX_SKILL && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
	{
		if( tmp_skill.id < ARRAYLENGTH(p->skill) )
			memcpy(&p->skill[tmp_skill.id], &tmp_skill, sizeof(tmp_skill));
		else
			ShowWarning("mmo_char_fromsql: ignoring invalid skill (id=%u,lv=%u) of character %s (AID=%d,CID=%d)\n", tmp_skill.id, tmp_skill.lv, p->name, p->account_id, p->char_id);
	}
	strcat(t_msg, " skills");

	//read friends
	//`friends` (`char_id`, `friend_account`, `friend_id`)
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT c.`account_id`, c.`char_id`, c.`name` FROM `%s` c LEFT JOIN `%s` f ON f.`friend_account` = c.`account_id` AND f.`friend_id` = c.`char_id` WHERE f.`char_id`=? LIMIT %d", schema_config.char_db, schema_config.friend_db, MAX_FRIENDS)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_INT,    &tmp_friend.account_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_INT,    &tmp_friend.char_id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_STRING, &tmp_friend.name, sizeof(tmp_friend.name), NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	for( i = 0; i < MAX_FRIENDS && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->friends[i], &tmp_friend, sizeof(tmp_friend));
	strcat(t_msg, " friends");

#ifdef HOTKEY_SAVING
	//read hotkeys
	//`hotkey` (`char_id`, `hotkey`, `type`, `itemskill_id`, `skill_lvl`
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `hotkey`, `type`, `itemskill_id`, `skill_lvl` FROM `%s` WHERE `char_id`=?", schema_config.hotkey_db)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_INT,    &hotkey_num, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_UCHAR,  &tmp_hotkey.type, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_UINT,   &tmp_hotkey.id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 3, SQLDT_USHORT, &tmp_hotkey.lv, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	while( SQL_SUCCESS == SqlStmt_NextRow(stmt) )
	{
		if( hotkey_num >= 0 && hotkey_num < MAX_HOTKEYS )
			memcpy(&p->hotkeys[hotkey_num], &tmp_hotkey, sizeof(tmp_hotkey));
		else
			ShowWarning("mmo_char_fromsql: ignoring invalid hotkey (hotkey=%d,type=%u,id=%u,lv=%u) of character %s (AID=%d,CID=%d)\n", hotkey_num, tmp_hotkey.type, tmp_hotkey.id, tmp_hotkey.lv, p->name, p->account_id, p->char_id);
	}
	strcat(t_msg, " hotkeys");
#endif

	/* Character PVP Ranking */
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `kill_count`, `death_count`, `score` FROM `char_pvp` WHERE `char_id` = ?")
		|| SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
		|| SQL_ERROR == SqlStmt_Execute(stmt)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_USHORT, &p->pvp.kill_count, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT, &p->pvp.death_count, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_USHORT, &p->pvp.score, 0, NULL, NULL)
		|| SQL_SUCCESS != SqlStmt_NextRow(stmt) )
	{
		p->pvp.score = 2000; // Base Score
	}
	strcat(t_msg, " pvprank");

	/* Character PK Ranking */
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `kill_count`, `death_count`, `score` FROM `char_pk` WHERE `char_id` = ?")
		|| SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
		|| SQL_ERROR == SqlStmt_Execute(stmt)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_USHORT, &p->pk.kill_count, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT, &p->pk.death_count, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 2, SQLDT_USHORT, &p->pk.score, 0, NULL, NULL)
		|| SQL_SUCCESS != SqlStmt_NextRow(stmt) )
	{
		; // Database Error or Row don't exists
	}
	strcat(t_msg, " pkrank");

	/* Character Battleground Standings */
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `top_damage`,`damage_done`,`damage_received`,`skulls`,`ti_wins`,`ti_lost`,`ti_tie`,`eos_flags`,`eos_bases`,`eos_wins`,`eos_lost`,`eos_tie`,`boss_killed`,`boss_damage`,`boss_flags`,`boss_wins`,`boss_lost`,`boss_tie`,`td_kills`,`td_deaths`,`td_wins`,`td_lost`,`td_tie`,`sc_stole`,`sc_captured`,`sc_droped`,`sc_wins`,`sc_lost`,`sc_tie`,`ctf_taken`,`ctf_captured`,`ctf_droped`,`ctf_wins`,`ctf_lost`,`ctf_tie`,`emperium_kill`,`barricade_kill`,`gstone_kill`,`cq_wins`,`cq_lost`,`kill_count`,`death_count`,`win`,`lost`,`tie`,`leader_win`,`leader_lost`,`leader_tie`,`deserter`,`score`,`points`,`sp_heal_potions`,`hp_heal_potions`,`yellow_gemstones`,`red_gemstones`,`blue_gemstones`,`poison_bottles`,`acid_demostration`,`acid_demostration_fail`,`support_skills_used`,`healing_done`,`wrong_support_skills_used`,`wrong_healing_done`,`sp_used`,`zeny_used`,`spiritb_used`,`ammo_used`,`rank_points`,`rank_games`,`ru_wins`,`ru_lost`,`ru_captures`,`dom_bases`,`dom_off_kills`,`dom_def_kills`,`dom_wins`,`dom_lost`,`dom_tie` FROM `char_bg` WHERE `char_id` = ?")
		|| SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
		|| SQL_ERROR == SqlStmt_Execute(stmt)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  0, SQLDT_UINT,   &p->bgstats.top_damage, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  1, SQLDT_UINT,   &p->bgstats.damage_done, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  2, SQLDT_UINT,   &p->bgstats.damage_received, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  3, SQLDT_USHORT, &p->bgstats.skulls, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  4, SQLDT_USHORT, &p->bgstats.ti_wins, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  5, SQLDT_USHORT, &p->bgstats.ti_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  6, SQLDT_USHORT, &p->bgstats.ti_tie, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  7, SQLDT_USHORT, &p->bgstats.eos_flags, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  8, SQLDT_USHORT, &p->bgstats.eos_bases, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  9, SQLDT_USHORT, &p->bgstats.eos_wins, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 10, SQLDT_USHORT, &p->bgstats.eos_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 11, SQLDT_USHORT, &p->bgstats.eos_tie, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 12, SQLDT_USHORT, &p->bgstats.boss_killed, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 13, SQLDT_UINT,   &p->bgstats.boss_damage, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 14, SQLDT_USHORT, &p->bgstats.boss_flags, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 15, SQLDT_USHORT, &p->bgstats.boss_wins, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 16, SQLDT_USHORT, &p->bgstats.boss_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 17, SQLDT_USHORT, &p->bgstats.boss_tie, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 18, SQLDT_USHORT, &p->bgstats.td_kills, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 19, SQLDT_USHORT, &p->bgstats.td_deaths, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 20, SQLDT_USHORT, &p->bgstats.td_wins, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 21, SQLDT_USHORT, &p->bgstats.td_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 22, SQLDT_USHORT, &p->bgstats.td_tie, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 23, SQLDT_USHORT, &p->bgstats.sc_stole, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 24, SQLDT_USHORT, &p->bgstats.sc_captured, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 25, SQLDT_USHORT, &p->bgstats.sc_droped, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 26, SQLDT_USHORT, &p->bgstats.sc_wins, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 27, SQLDT_USHORT, &p->bgstats.sc_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 28, SQLDT_USHORT, &p->bgstats.sc_tie, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 29, SQLDT_USHORT, &p->bgstats.ctf_taken, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 30, SQLDT_USHORT, &p->bgstats.ctf_captured, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 31, SQLDT_USHORT, &p->bgstats.ctf_droped, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 32, SQLDT_USHORT, &p->bgstats.ctf_wins, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 33, SQLDT_USHORT, &p->bgstats.ctf_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 34, SQLDT_USHORT, &p->bgstats.ctf_tie, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 35, SQLDT_USHORT, &p->bgstats.emperium_kill, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 36, SQLDT_USHORT, &p->bgstats.barricade_kill, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 37, SQLDT_USHORT, &p->bgstats.gstone_kill, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 38, SQLDT_USHORT, &p->bgstats.cq_wins, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 39, SQLDT_USHORT, &p->bgstats.cq_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 40, SQLDT_USHORT, &p->bgstats.kill_count, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 41, SQLDT_USHORT, &p->bgstats.death_count, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 42, SQLDT_USHORT, &p->bgstats.win, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 43, SQLDT_USHORT, &p->bgstats.lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 44, SQLDT_USHORT, &p->bgstats.tie, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 45, SQLDT_USHORT, &p->bgstats.leader_win, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 46, SQLDT_USHORT, &p->bgstats.leader_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 47, SQLDT_USHORT, &p->bgstats.leader_tie, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 48, SQLDT_USHORT, &p->bgstats.deserter, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 49, SQLDT_USHORT, &p->bgstats.score, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 50, SQLDT_USHORT, &p->bgstats.points, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 51, SQLDT_UINT,   &p->bgstats.sp_heal_potions, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 52, SQLDT_UINT,   &p->bgstats.hp_heal_potions, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 53, SQLDT_UINT,   &p->bgstats.yellow_gemstones, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 54, SQLDT_UINT,   &p->bgstats.red_gemstones, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 55, SQLDT_UINT,   &p->bgstats.blue_gemstones, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 56, SQLDT_UINT,   &p->bgstats.poison_bottles, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 57, SQLDT_UINT,   &p->bgstats.acid_demostration, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 58, SQLDT_UINT,   &p->bgstats.acid_demostration_fail, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 59, SQLDT_UINT,   &p->bgstats.support_skills_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 60, SQLDT_UINT,   &p->bgstats.healing_done, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 61, SQLDT_UINT,   &p->bgstats.wrong_support_skills_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 62, SQLDT_UINT,   &p->bgstats.wrong_healing_done, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 63, SQLDT_UINT,   &p->bgstats.sp_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 64, SQLDT_UINT,   &p->bgstats.zeny_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 65, SQLDT_UINT,   &p->bgstats.spiritb_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 66, SQLDT_UINT,   &p->bgstats.ammo_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 67, SQLDT_USHORT, &p->bgstats.rank_points, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 68, SQLDT_USHORT, &p->bgstats.rank_games, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 69, SQLDT_USHORT, &p->bgstats.ru_wins, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 70, SQLDT_USHORT, &p->bgstats.ru_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 71, SQLDT_USHORT, &p->bgstats.ru_captures, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 72, SQLDT_USHORT, &p->bgstats.dom_bases, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 73, SQLDT_USHORT, &p->bgstats.dom_off_kills, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 74, SQLDT_USHORT, &p->bgstats.dom_def_kills, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 75, SQLDT_USHORT, &p->bgstats.dom_wins, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 76, SQLDT_USHORT, &p->bgstats.dom_lost, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 77, SQLDT_USHORT, &p->bgstats.dom_tie, 0, NULL, NULL)
		|| SQL_SUCCESS != SqlStmt_NextRow(stmt) )
	{
		p->bgstats.score = 2000;
	}
	strcat(t_msg, " bgstats");

	/* Character WoE Standings */
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `top_damage`, `damage_done`, `damage_received`, `emperium_damage`, `guardian_damage`, `barricade_damage`, `gstone_damage`, `emperium_kill`, `guardian_kill`, `barricade_kill`, `gstone_kill`, `sp_heal_potions`, `hp_heal_potions`, `yellow_gemstones`, `red_gemstones`, `blue_gemstones`, `poison_bottles`, `acid_demostration`, `acid_demostration_fail`, `support_skills_used`, `healing_done`, `wrong_support_skills_used`, `wrong_healing_done`, `sp_used`, `zeny_used`, `spiritb_used`, `ammo_used`, `kill_count`, `death_count`, `score` FROM `char_wstats` WHERE `char_id` = ?")
		|| SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
		|| SQL_ERROR == SqlStmt_Execute(stmt)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  0, SQLDT_UINT, &p->wstats.top_damage, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  1, SQLDT_UINT, &p->wstats.damage_done, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  2, SQLDT_UINT, &p->wstats.damage_received, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  3, SQLDT_UINT, &p->wstats.emperium_damage, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  4, SQLDT_UINT, &p->wstats.guardian_damage, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  5, SQLDT_UINT, &p->wstats.barricade_damage, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  6, SQLDT_UINT, &p->wstats.gstone_damage, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  7, SQLDT_USHORT, &p->wstats.emperium_kill, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  8, SQLDT_USHORT, &p->wstats.guardian_kill, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt,  9, SQLDT_USHORT, &p->wstats.barricade_kill, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 10, SQLDT_USHORT, &p->wstats.gstone_kill, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 11, SQLDT_UINT, &p->wstats.sp_heal_potions, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 12, SQLDT_UINT, &p->wstats.hp_heal_potions, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 13, SQLDT_UINT, &p->wstats.yellow_gemstones, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 14, SQLDT_UINT, &p->wstats.red_gemstones, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 15, SQLDT_UINT, &p->wstats.blue_gemstones, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 16, SQLDT_UINT, &p->wstats.poison_bottles, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 17, SQLDT_UINT, &p->wstats.acid_demostration, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 18, SQLDT_UINT, &p->wstats.acid_demostration_fail, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 19, SQLDT_UINT, &p->wstats.support_skills_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 20, SQLDT_UINT, &p->wstats.healing_done, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 21, SQLDT_UINT, &p->wstats.wrong_support_skills_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 22, SQLDT_UINT, &p->wstats.wrong_healing_done, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 23, SQLDT_UINT, &p->wstats.sp_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 24, SQLDT_UINT, &p->wstats.zeny_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 25, SQLDT_UINT, &p->wstats.spiritb_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 26, SQLDT_UINT, &p->wstats.ammo_used, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 27, SQLDT_USHORT, &p->wstats.kill_count, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 28, SQLDT_USHORT, &p->wstats.death_count, 0, NULL, NULL)
		|| SQL_ERROR == SqlStmt_BindColumn(stmt, 29, SQLDT_USHORT, &p->wstats.score, 0, NULL, NULL)
		|| SQL_SUCCESS != SqlStmt_NextRow(stmt) )
	{
		p->wstats.score = 2000;
	}
	strcat(t_msg, " woestats");

	/* Skill Usage */
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `id`, `count` FROM `skill_count` WHERE `char_id` = ? LIMIT %d", MAX_SKILL_TREE)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_USHORT, &tmp_skillcount.id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT, &tmp_skillcount.count, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	for( i = 0; i < MAX_SKILL_TREE && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->skillcount[i], &tmp_skillcount, sizeof(tmp_skillcount));

	/* BG Skill Usage */
	if( SQL_ERROR == SqlStmt_Prepare(stmt, "SELECT `id`, `count` FROM `bg_skill_count` WHERE `char_id` = ? LIMIT %d", MAX_SKILL_TREE)
	||	SQL_ERROR == SqlStmt_BindParam(stmt, 0, SQLDT_INT, &char_id, 0)
	||	SQL_ERROR == SqlStmt_Execute(stmt)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 0, SQLDT_USHORT, &tmp_skillcount.id, 0, NULL, NULL)
	||	SQL_ERROR == SqlStmt_BindColumn(stmt, 1, SQLDT_USHORT, &tmp_skillcount.count, 0, NULL, NULL) )
		SqlStmt_ShowDebug(stmt);

	for( i = 0; i < MAX_SKILL_TREE && SQL_SUCCESS == SqlStmt_NextRow(stmt); ++i )
		memcpy(&p->bg_skillcount[i], &tmp_skillcount, sizeof(tmp_skillcount));
	strcat(t_msg, " skillcount");

	/* Mercenary Owner DataBase */
	mercenary_owner_fromsql(char_id, p);
	strcat(t_msg, " mercenary");


	if (charserv_config.save_log) ShowInfo("Loaded char (%d - %s): %s\n", char_id, p->name, t_msg);	//ok. all data load successfuly!
	SqlStmt_Free(stmt);
	StringBuf_Destroy(&buf);

	cp = idb_ensure(char_db_, char_id, char_create_charstatus);
	memcpy(cp, p, sizeof(struct mmo_charstatus));
	return 1;
}

//==========================================================================================================
int char_mmo_sql_init(void) {
	char_db_= idb_alloc(DB_OPT_RELEASE_DATA);

	ShowStatus("Characters per Account: '%d'.\n", charserv_config.char_config.char_per_account);

	//the 'set offline' part is now in check_login_conn ...
	//if the server connects to loginserver
	//it will dc all off players
	//and send the loginserver the new state....

	// Force all users offline in sql when starting char-server
	// (useful when servers crashs and don't clean the database)
	char_set_all_offline_sql();

	return 0;
}

//-----------------------------------
// Function to change chararcter's names
//-----------------------------------
int char_rename_char_sql(struct char_session_data *sd, int char_id)
{
	struct mmo_charstatus char_dat;
	char esc_name[NAME_LENGTH*2+1];

	if( sd->new_name[0] == 0 ) // Not ready for rename
		return 2;

	if( !char_mmo_char_fromsql(char_id, &char_dat, false) ) // Only the short data is needed.
		return 2;

	if( char_dat.rename == 0 )
		return 1;

	Sql_EscapeStringLen(sql_handle, esc_name, sd->new_name, strnlen(sd->new_name, NAME_LENGTH));

	// check if the char exist
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT 1 FROM `%s` WHERE `name` LIKE '%s' LIMIT 1", schema_config.char_db, esc_name) )
	{
		Sql_ShowDebug(sql_handle);
		return 4;
	}

	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `name` = '%s', `rename` = '%d' WHERE `char_id` = '%d'", schema_config.char_db, esc_name, --char_dat.rename, char_id) )
	{
		Sql_ShowDebug(sql_handle);
		return 3;
	}

	// Change character's name into guild_db.
	if( char_dat.guild_id )
		inter_guild_charname_changed(char_dat.guild_id, sd->account_id, char_id, sd->new_name);

	safestrncpy(char_dat.name, sd->new_name, NAME_LENGTH);
	memset(sd->new_name,0,sizeof(sd->new_name));

	// log change
	if( charserv_config.log_char )
	{
		if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`time`, `char_msg`,`account_id`,`char_num`,`name`,`str`,`agi`,`vit`,`int`,`dex`,`luk`,`hair`,`hair_color`)"
			"VALUES (NOW(), '%s', '%d', '%d', '%s', '0', '0', '0', '0', '0', '0', '0', '0')",
			schema_config.charlog_db, "change char name", sd->account_id, char_dat.slot, esc_name) )
			Sql_ShowDebug(sql_handle);
	}

	return 0;
}

//-----------------------------------
// Save Character Data into SQL File
//-----------------------------------
int char_dump2sql(int char_id)
{	
	char filename[128], esc_name[NAME_LENGTH*2+1], str[32], esc_str[65], value[256], esc_value[513];
	struct mmo_charstatus cp;
	struct item *i_data;
	struct s_skill *s_data;
	char* data;

	int i = 0;
	FILE* fp;

	if( char_mmo_char_fromsql(char_id, &cp, true) == 0 )
		return 0; // Non existant or error.

	sprintf(filename, "dumps/%d_%d.sql", cp.account_id, char_id);
	if( (fp = fopen(filename, "w")) == NULL )
		return 0;

	Sql_EscapeStringLen(sql_handle, esc_name, cp.name, strnlen(cp.name, NAME_LENGTH));
	fprintf(fp, "-- Character Information --\n");
	fprintf(fp, "INSERT INTO `char` "
		"(`account_id`, `char_num`, `name`, `class`, `base_level`, `job_level`, `base_exp`, `job_exp`, `zeny`, `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`, `max_sp`, `sp`, `status_point`, `skill_point`, `option`, `karma`, `manner`, `party_id`, `guild_id`, `pet_id`, `homun_id`, `hair`, `hair_color`, `clothes_color`, `weapon`, `shield`, `head_top`, `head_mid`, `head_bottom`, `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`, `partner_id`, `online`, `father`, `mother`, `child`, `fame`, `playtime`)"
		" VALUES "
		"('ACC', '%d', '@%s', '%d', '%d', '%d', '%u', '%u', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%s', '%d', '%d', '%s', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%u');\n",
		cp.slot, esc_name, cp.class_, cp.base_level, cp.job_level, cp.base_exp, cp.job_exp, cp.zeny, cp.str, cp.agi, cp.vit, cp.int_, cp.dex, cp.luk, cp.max_hp, cp.hp, cp.max_sp, cp.sp, cp.status_point, cp.skill_point, cp.option, cp.karma, cp.manner, 0, 0, 0, 0, cp.hair, cp.hair_color, cp.clothes_color, cp.weapon, cp.shield, cp.head_top, cp.head_mid, cp.head_bottom, mapindex_id2name(cp.last_point.map), cp.last_point.x, cp.last_point.y, mapindex_id2name(cp.save_point.map), cp.save_point.x, cp.save_point.y, 0, 0, 0, 0, 0, cp.fame, cp.playtime);

	if( cp.hom_id )
	{ // Homunculus Backup
		struct s_homunculus hd;
		if( mapif_homunculus_load(cp.hom_id, &hd) )
		{
			Sql_EscapeStringLen(sql_handle, esc_name, hd.name, strnlen(hd.name, NAME_LENGTH));

			fprintf(fp, "-- Homunculus Information --\n");
			fprintf(fp, "INSERT INTO `homunculus` "
				"(`char_id`, `class`,`name`,`level`,`exp`,`intimacy`,`hunger`, `str`, `agi`, `vit`, `int`, `dex`, `luk`, `hp`,`max_hp`,`sp`,`max_sp`,`skill_point`, `rename_flag`, `vaporize`) "
				"VALUES "
				"('CHR', '%d', '%s', '%d', '%u', '%u', '%d', '%d', %d, '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d');\n\n",
				hd.class_, esc_name, hd.level, hd.exp, hd.intimacy, hd.hunger, hd.str, hd.agi, hd.vit, hd.int_, hd.dex, hd.luk, hd.hp, hd.max_hp, hd.sp, hd.max_sp, hd.skillpts, hd.rename_flag, hd.vaporize);
			
			i = 0;
			while( i < MAX_HOMUNSKILL )
			{
				if( hd.hskill[i].id > 0 )
					fprintf(fp, "INSERT INTO `skill_homunculus` (`homun_id`, `id`, `lv`) VALUES ('HOM', '%d', '%d');\n", hd.hskill[i].id, hd.hskill[i].lv);

				i++;
			}
		}
	}

	if( !charserv_config.char_new )
	{ // Backup Account Information
		fprintf(fp, "\n-- Account Storage --\n");
		i = 0;
		while( i < MAX_STORAGE && cp.storage.items[i].nameid > 0 )
		{
			i_data = &cp.storage.items[i];

			fprintf(fp, "INSERT INTO `storage` "
				"(`account_id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `card0`, `card1`, `card2`, `card3`, `expire_time`, `unique_id`, `bound`)"
				" VALUES "
				"('ACC', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%u', '%"PRIu64"', '%d');\n",
				i_data->nameid, i_data->amount, i_data->equip, i_data->identify, i_data->refine, i_data->attribute, i_data->card[0], i_data->card[1], i_data->card[2], i_data->card[3], i_data->expire_time, i_data->unique_id, i_data->bound);
			i++;
		}

		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `storage` WHERE `account_id` = '%d'", cp.account_id) )
			Sql_ShowDebug(sql_handle); // Clear Storage Data to Avoid Multiple Backups.

		fprintf(fp, "\n-- Global Reg Value Account Level --\n");
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `str`, `value` FROM `%s` WHERE `type` = 2 AND `account_id` = '%d'", schema_config.reg_db, cp.account_id) )
			Sql_ShowDebug(sql_handle);

		while( SQL_SUCCESS == Sql_NextRow(sql_handle) )
		{
			Sql_GetData(sql_handle, 0, &data, NULL); safestrncpy(str, data, sizeof(str));
			Sql_EscapeStringLen(sql_handle, esc_str, str, strnlen(str, 32));

			Sql_GetData(sql_handle, 1, &data, NULL); safestrncpy(value, data, sizeof(value));
			Sql_EscapeStringLen(sql_handle, esc_value, value, strnlen(value, 256));

			fprintf(fp, "INSERT INTO `global_reg_value` (`char_id`, `str`, `value`, `type`, `account_id`) VALUES ('0', '%s', '%s', '2', 'ACC');\n",
				esc_str, esc_value);
		}

		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `global_reg_value` WHERE `account_id` = '%d' AND `type` = 2", cp.account_id) )
			Sql_ShowDebug(sql_handle); // Clear Global Reg Value Data to Avoid Multiple Backups.

		fprintf(fp, "\n\n-- End of Account Related Backup --\n\n\n");
	}

	i = 0;
	fprintf(fp, "\n-- Character Inventory --\n\n");
	while( i < MAX_INVENTORY && cp.inventory[i].nameid > 0 )
	{
		i_data = &cp.inventory[i];

		fprintf(fp, "INSERT INTO `inventory` "
			"(`char_id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `card0`, `card1`, `card2`, `card3`, `expire_time`, `unique_id`, `bound`)"
			" VALUES "
			"('CHR', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%u', '%"PRIu64"', '%d');\n",
			i_data->nameid, i_data->amount, i_data->equip, i_data->identify, i_data->refine, i_data->attribute, i_data->card[0], i_data->card[1], i_data->card[2], i_data->card[3], i_data->expire_time, i_data->unique_id, i_data->bound);
		i++;
	}

	fprintf(fp, "\n-- Character Cart Inventory --\n\n");
	i = 0;
	while( i < MAX_CART && cp.cart[i].nameid > 0 )
	{
		i_data = &cp.cart[i];

		fprintf(fp, "INSERT INTO `cart_inventory` "
			"(`char_id`, `nameid`, `amount`, `equip`, `identify`, `refine`, `attribute`, `card0`, `card1`, `card2`, `card3`, `expire_time`, `unique_id`, `bound`)"
			" VALUES "
			"('CHR', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%u', '%"PRIu64"', '%d');\n",
			i_data->nameid, i_data->amount, i_data->equip, i_data->identify, i_data->refine, i_data->attribute, i_data->card[0], i_data->card[1], i_data->card[2], i_data->card[3], i_data->expire_time, i_data->unique_id, i_data->bound);
		i++;
	}

	fprintf(fp, "\n-- Character Skills --\n\n");
	i = 0;
	while( i < MAX_SKILL )
	{
		if( cp.skill[i].id > 0 )
		{
			s_data = &cp.skill[i];
			fprintf(fp, "INSERT INTO `skill` (`char_id`, `id`, `lv`) VALUES ('CHR', '%d', '%d');\n", s_data->id, s_data->lv);
		}
		i++;
	}

	fprintf(fp, "\n-- Character Global Reg Value --\n\n");
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `str`, `value` FROM `%s` WHERE `type` = 3 AND `char_id` = '%d'", schema_config.reg_db, char_id) )
		Sql_ShowDebug(sql_handle);

	while( SQL_SUCCESS == Sql_NextRow(sql_handle) )
	{
		Sql_GetData(sql_handle, 0, &data, NULL); safestrncpy(str, data, sizeof(str));
		Sql_EscapeStringLen(sql_handle, esc_str, str, strnlen(str, 32));

		Sql_GetData(sql_handle, 1, &data, NULL); safestrncpy(value, data, sizeof(value));
		Sql_EscapeStringLen(sql_handle, esc_value, value, strnlen(value, 256));

		fprintf(fp, "INSERT INTO `global_reg_value` (`char_id`, `str`, `value`, `type`, `account_id`) VALUES ('CHR', '%s', '%s', '3', '0');\n",
			esc_str, esc_value);
	}

	Sql_FreeResult(sql_handle);

	fclose(fp);
	return 1;
}

int char_check_char_name(char * name, char * esc_name)
{
	int i;

	// check length of character name
	if( name[0] == '\0' )
		return -2; // empty character name
	/**
	 * The client does not allow you to create names with less than 4 characters, however,
	 * the use of WPE can bypass this, and this fixes the exploit.
	 **/
	if( strlen( name ) < 4 )
		return -2;
	// check content of character name
	if( remove_control_chars(name) )
		return -2; // control chars in name

	// check for reserved names
	if( !strcmpi(name, charserv_config.wisp_server_name) || name[0] == '#' )
		return -1; // nick reserved for internal server messages

	// Check Authorised letters/symbols in the name of the character
	if( charserv_config.char_config.char_name_option == 1 )
	{ // only letters/symbols in char_name_letters are authorised
		for( i = 0; i < NAME_LENGTH && name[i]; i++ )
			if( strchr(charserv_config.char_config.char_name_letters, name[i]) == NULL )
				return -2;
	}
	else if( charserv_config.char_config.char_name_option == 2 )
	{ // letters/symbols in char_name_letters are forbidden
		for( i = 0; i < NAME_LENGTH && name[i]; i++ )
			if( strchr(charserv_config.char_config.char_name_letters, name[i]) != NULL )
				return -2;
	}
	if( charserv_config.char_config.name_ignoring_case ) {
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT 1 FROM `%s` WHERE BINARY `name` = '%s' LIMIT 1", schema_config.char_db, esc_name) ) {
			Sql_ShowDebug(sql_handle);
			return -2;
		}
	} else {
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT 1 FROM `%s` WHERE `name` = '%s' LIMIT 1", schema_config.char_db, esc_name) ) {
			Sql_ShowDebug(sql_handle);
			return -2;
		}
	}
	if( Sql_NumRows(sql_handle) > 0 )
		return -1; // name already exists

	return 0;
}

//-----------------------------------
// Function to create a new character
//-----------------------------------
#if PACKETVER >= 20120307
int char_make_new_char_sql(struct char_session_data* sd, char* name_, int slot, int hair_color, int hair_style) {
	int str = 1, agi = 1, vit = 1, int_ = 1, dex = 1, luk = 1;
#else
int char_make_new_char_sql(struct char_session_data* sd, char* name_, int str, int agi, int vit, int int_, int dex, int luk, int slot, int hair_color, int hair_style) {
#endif
	char name[NAME_LENGTH];
	char esc_name[NAME_LENGTH*2+1];
	int char_id, flag, k;

	safestrncpy(name, name_, NAME_LENGTH);
	normalize_name(name,TRIM_CHARS);
	Sql_EscapeStringLen(sql_handle, esc_name, name, strnlen(name, NAME_LENGTH));

	flag = char_check_char_name(name,esc_name);
	if( flag < 0 )
		return flag;

	//check other inputs
#if PACKETVER >= 20120307
	if(slot < 0 || slot >= sd->char_slots)
#else
	if((slot < 0 || slot >= sd->char_slots) // slots
	|| (str + agi + vit + int_ + dex + luk != 6*5 ) // stats
	|| (str < 1 || str > 9 || agi < 1 || agi > 9 || vit < 1 || vit > 9 || int_ < 1 || int_ > 9 || dex < 1 || dex > 9 || luk < 1 || luk > 9) // individual stat values
	|| (str + int_ != 10 || agi + luk != 10 || vit + dex != 10) ) // pairs
#endif
#if PACKETVER >= 20100413
		return -4; // invalid slot
#else
		return -2; // invalid input
#endif


	// check the number of already existing chars in this account
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT 1 FROM `%s` WHERE `account_id` = '%d'", schema_config.char_db, sd->account_id) )
		Sql_ShowDebug(sql_handle);
	if( Sql_NumRows(sql_handle) >= sd->char_slots )
		return -2; // character account limit exceeded

	// check char slot
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT 1 FROM `%s` WHERE `account_id` = '%d' AND `char_num` = '%d' LIMIT 1", schema_config.char_db, sd->account_id, slot) )
		Sql_ShowDebug(sql_handle);
	if( Sql_NumRows(sql_handle) > 0 )
		return -2; // slot already in use

	// validation success, log result
	if (charserv_config.log_char) {
		if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`time`, `char_msg`,`account_id`,`char_num`,`name`,`str`,`agi`,`vit`,`int`,`dex`,`luk`,`hair`,`hair_color`)"
			"VALUES (NOW(), '%s', '%d', '%d', '%s', '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d')",
			schema_config.charlog_db, "make new char", sd->account_id, slot, esc_name, str, agi, vit, int_, dex, luk, hair_style, hair_color) )
			Sql_ShowDebug(sql_handle);
	}
#if PACKETVER >= 20120307
	//Insert the new char entry to the database
	if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`account_id`, `char_num`, `name`, `zeny`, `status_point`,`str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`,"
		"`max_sp`, `sp`, `hair`, `hair_color`, `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES ("
		"'%d', '%d', '%s', '%d',  '%d','%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d','%d', '%d','%d', '%d', '%s', '%d', '%d', '%s', '%d', '%d')",
		schema_config.char_db, sd->account_id , slot, esc_name, charserv_config.start_zeny, 48, str, agi, vit, int_, dex, luk,
		(40 * (100 + vit)/100) , (40 * (100 + vit)/100 ),  (11 * (100 + int_)/100), (11 * (100 + int_)/100), hair_style, hair_color,
		mapindex_id2name(charserv_config.start_point.map), charserv_config.start_point.x, charserv_config.start_point.y, mapindex_id2name(charserv_config.start_point.map), charserv_config.start_point.x, charserv_config.start_point.y) )
	{
		Sql_ShowDebug(sql_handle);
		return -2; //No, stop the procedure!
	}
#else
	//Insert the new char entry to the database
	if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`account_id`, `char_num`, `name`, `zeny`, `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`,"
		"`max_sp`, `sp`, `hair`, `hair_color`, `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES ("
		"'%d', '%d', '%s', '%d',  '%d', '%d', '%d', '%d', '%d', '%d', '%d', '%d','%d', '%d','%d', '%d', '%s', '%d', '%d', '%s', '%d', '%d')",
		schema_config.char_db, sd->account_id , slot, esc_name, charserv_config.start_zeny, str, agi, vit, int_, dex, luk,
		(40 * (100 + vit)/100) , (40 * (100 + vit)/100 ),  (11 * (100 + int_)/100), (11 * (100 + int_)/100), hair_style, hair_color,
		mapindex_id2name(charserv_config.start_point.map), charserv_config.start_point.x, charserv_config.start_point.y, mapindex_id2name(charserv_config.start_point.map), charserv_config.start_point.x, charserv_config.start_point.y) )
	{
		Sql_ShowDebug(sql_handle);
		return -2; //No, stop the procedure!
	}
#endif
	//Retrieve the newly auto-generated char id
	char_id = (int)Sql_LastInsertId(sql_handle);
	//Give the char the default items
	for (k = 0; k <= MAX_STARTITEM && start_items[k].nameid != 0; k ++) {
		if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s` (`char_id`,`nameid`, `amount`, `equip`, `identify`) VALUES ('%d', '%hu', '%d', '%d', '%d')", schema_config.inventory_db, char_id, start_items[k].nameid, start_items[k].amount, start_items[k].pos, 1) )
			Sql_ShowDebug(sql_handle);
	}

	ShowInfo("Created char: account: %d, char: %d, slot: %d, name: %s\n", sd->account_id, char_id, slot, name);
	return char_id;
}

/*----------------------------------------------------------------------------------------------------------*/
/* Divorce Players */
/*----------------------------------------------------------------------------------------------------------*/
int char_divorce_char_sql(int partner_id1, int partner_id2){
	if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `partner_id`='0' WHERE `char_id`='%d' OR `char_id`='%d' LIMIT 2", schema_config.char_db, partner_id1, partner_id2) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE (`nameid`='%hu' OR `nameid`='%hu') AND (`char_id`='%d' OR `char_id`='%d') LIMIT 2", schema_config.inventory_db, WEDDING_RING_M, WEDDING_RING_F, partner_id1, partner_id2) )
		Sql_ShowDebug(sql_handle);
	chmapif_send_ackdivorce(partner_id1, partner_id2);
	return 0;
}

/*----------------------------------------------------------------------------------------------------------*/
/* Item Removal */
/*----------------------------------------------------------------------------------------------------------*/
int char_item_remove4all(int nameid)
{
	unsigned char buf[4];
	ShowInfo("Destroying item ID %d on all Users...\n", nameid);

	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `nameid` = '%d'", schema_config.inventory_db, nameid) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `nameid` = '%d'", schema_config.cart_db, nameid) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `nameid` = '%d'", schema_config.storage_db, nameid) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `nameid` = '%d'", schema_config.rentstorage_db, nameid) )
		Sql_ShowDebug(sql_handle);

	WBUFW(buf,0) = 0x2b33;
	WBUFW(buf,2) = nameid;
	chmapif_sendall(buf,4);

	return 0;
}

/*----------------------------------------------------------------------------------------------------------*/
/* Delete char - davidsiaw */
/*----------------------------------------------------------------------------------------------------------*/
/* Returns 0 if successful
 * Returns < 0 for error
 */
int char_delete_char_sql(int char_id){
	char name[NAME_LENGTH];
	char esc_name[NAME_LENGTH*2+1]; //Name needs be escaped.
	int account_id, party_id, guild_id, hom_id, base_level, partner_id, father_id, mother_id, elemental_id;
	char *data;
	size_t len;

	if (SQL_ERROR == Sql_Query(sql_handle, "SELECT `name`,`account_id`,`party_id`,`guild_id`,`base_level`,`homun_id`,`partner_id`,`father`,`mother`,`elemental_id` FROM `%s` WHERE `char_id`='%d'", schema_config.char_db, char_id))
		Sql_ShowDebug(sql_handle);

	if( SQL_SUCCESS != Sql_NextRow(sql_handle) )
	{
		ShowError("delete_char_sql: Unable to fetch character data, deletion aborted.\n");
		Sql_FreeResult(sql_handle);
		return -1;
	}

	Sql_GetData(sql_handle, 0, &data, &len); safestrncpy(name, data, NAME_LENGTH);
	Sql_GetData(sql_handle, 1, &data, NULL); account_id = atoi(data);
	Sql_GetData(sql_handle, 2, &data, NULL); party_id = atoi(data);
	Sql_GetData(sql_handle, 3, &data, NULL); guild_id = atoi(data);
	Sql_GetData(sql_handle, 4, &data, NULL); base_level = atoi(data);
	Sql_GetData(sql_handle, 5, &data, NULL); hom_id = atoi(data);
	Sql_GetData(sql_handle, 6, &data, NULL); partner_id = atoi(data);
	Sql_GetData(sql_handle, 7, &data, NULL); father_id = atoi(data);
	Sql_GetData(sql_handle, 8, &data, NULL); mother_id = atoi(data);
	Sql_GetData(sql_handle, 9, &data, NULL); elemental_id = atoi(data);

	Sql_EscapeStringLen(sql_handle, esc_name, name, min(len, NAME_LENGTH));
	Sql_FreeResult(sql_handle);

	//check for config char del condition [Lupus]
	// TODO: Move this out to packet processing (0x68/0x1fb).
	if( ( charserv_config.char_config.char_del_level > 0 && base_level >= charserv_config.char_config.char_del_level )
	 || ( charserv_config.char_config.char_del_level < 0 && base_level <= -charserv_config.char_config.char_del_level )
	) {
			ShowInfo("Char deletion aborted: %s, BaseLevel: %i\n", name, base_level);
			return -1;
	}

	char_dump2sql(char_id); // [Zephyrus] Backup previous to delete

	/* Divorce [Wizputer] */
	if( partner_id )
		char_divorce_char_sql(char_id, partner_id);

	/* De-addopt [Zephyrus] */
	if( father_id || mother_id )
	{ // Char is Baby
		unsigned char buf[64];

		if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `child`='0' WHERE `char_id`='%d' OR `char_id`='%d'", schema_config.char_db, father_id, mother_id) )
			Sql_ShowDebug(sql_handle);
		if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `id` = '410'AND (`char_id`='%d' OR `char_id`='%d')", schema_config.skill_db, father_id, mother_id) )
			Sql_ShowDebug(sql_handle);

		WBUFW(buf,0) = 0x2b25;
		WBUFL(buf,2) = father_id;
		WBUFL(buf,6) = mother_id;
		WBUFL(buf,10) = char_id; // Baby
		chmapif_sendall(buf,14);
	}

	//Make the character leave the party [Skotlex]
	if (party_id)
		inter_party_leave(party_id, account_id, char_id);

	/* delete char's pet */
	//Delete the hatched pet if you have one...
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d' AND `incubate` = '0'", schema_config.pet_db, char_id) )
		Sql_ShowDebug(sql_handle);

	//Delete all pets that are stored in eggs (inventory + cart)
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` USING `%s` JOIN `%s` ON `pet_id` = `card1`|`card2`<<16 WHERE `%s`.char_id = '%d' AND card0 = 256", schema_config.pet_db, schema_config.pet_db, schema_config.inventory_db, schema_config.inventory_db, char_id) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` USING `%s` JOIN `%s` ON `pet_id` = `card1`|`card2`<<16 WHERE `%s`.char_id = '%d' AND card0 = 256", schema_config.pet_db, schema_config.pet_db, schema_config.cart_db, schema_config.cart_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* remove homunculus */
	if( hom_id )
		mapif_homunculus_delete(hom_id);

	/* remove elemental */
	if (elemental_id)
		mapif_elemental_delete(elemental_id);

	/* remove mercenary data */
	mercenary_owner_delete(char_id);

	/* Char Ranking */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `char_pvp` WHERE `char_id` = '%d'", char_id) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `char_pk` WHERE `char_id` = '%d'", char_id) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `char_bg` WHERE `char_id` = '%d'", char_id) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `char_bg_log` WHERE `killer_id` = '%d' OR `killed_id` = '%d'", char_id, char_id) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `char_wstats` WHERE `char_id` = '%d'", char_id) )
		Sql_ShowDebug(sql_handle);
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `char_woe_log` WHERE `killer_id` = '%d' OR `killed_id` = '%d'", char_id, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Quest Data */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id` = '%d'", schema_config.quest_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* Achievement Data */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id` = '%d'", schema_config.achievement_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* delete char's friends list */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id` = '%d'", schema_config.friend_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* delete char from other's friend list */
	//NOTE: Won't this cause problems for people who are already online? [Skotlex]
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `friend_id` = '%d'", schema_config.friend_db, char_id) )
		Sql_ShowDebug(sql_handle);

#ifdef HOTKEY_SAVING
	/* delete hotkeys */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", schema_config.hotkey_db, char_id) )
		Sql_ShowDebug(sql_handle);
#endif

	/* delete inventory */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", schema_config.inventory_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* delete cart inventory */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", schema_config.cart_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* delete memo areas */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", schema_config.memo_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* delete character registry */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `type`=3 AND `char_id`='%d'", schema_config.reg_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* delete skills */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", schema_config.skill_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* delete mails (only received) */
	if (SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `dest_id`='%d'", schema_config.mail_db, char_id))
		Sql_ShowDebug(sql_handle);

#ifdef ENABLE_SC_SAVING
	/* status changes */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `account_id` = '%d' AND `char_id`='%d'", schema_config.scdata_db, account_id, char_id) )
		Sql_ShowDebug(sql_handle);
#endif

	/* bonus_scripts */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id` = '%d'", schema_config.bonus_script_db, char_id) )
		Sql_ShowDebug(sql_handle);

	if (charserv_config.log_char) {
		if( SQL_ERROR == Sql_Query(sql_handle, "INSERT INTO `%s`(`time`, `account_id`,`char_num`,`char_msg`,`name`) VALUES (NOW(), '%d', '%d', 'Deleted char (CID %d)', '%s')",
			schema_config.charlog_db, account_id, 0, char_id, esc_name) )
			Sql_ShowDebug(sql_handle);
	}

	/* delete character */
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", schema_config.char_db, char_id) )
		Sql_ShowDebug(sql_handle);

	/* No need as we used inter_guild_leave [Skotlex]
	// Also delete info from guildtables.
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `char_id`='%d'", guild_member_db, char_id) )
		Sql_ShowDebug(sql_handle);
	*/

	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `guild_id` FROM `%s` WHERE `char_id` = '%d'", schema_config.guild_db, char_id) )
		Sql_ShowDebug(sql_handle);
	else if( Sql_NumRows(sql_handle) > 0 )
		mapif_parse_BreakGuild(0,guild_id);
	else if( guild_id )
		inter_guild_leave(guild_id, account_id, char_id);// Leave your guild.
	return 0;
}

/**
 * This function parse all map-serv attached to this char-serv and increase user count
 * @return numbers of total users
 */
int char_count_users(void)
{
	int i, users;

	users = 0;
	for(i = 0; i < ARRAYLENGTH(map_server); i++) {
		if (map_server[i].fd > 0) {
			users += map_server[i].users;
		}
	}
	return users;
}

// Writes char data to the buffer in the format used by the client.
// Used in packets 0x6b (chars info) and 0x6d (new char info)
// Returns the size
int char_mmo_char_tobuf(uint8* buffer, struct mmo_charstatus* p)
{
	unsigned short offset = 0;
	uint8* buf;

	if( buffer == NULL || p == NULL )
		return 0;

	buf = WBUFP(buffer,0);
	WBUFL(buf,0) = p->char_id;
	WBUFL(buf,4) = min(p->base_exp, INT32_MAX);
	WBUFL(buf,8) = p->zeny;
	WBUFL(buf,12) = min(p->job_exp, INT32_MAX);
	WBUFL(buf,16) = p->job_level;
	WBUFL(buf,20) = 0; // probably opt1
	WBUFL(buf,24) = 0; // probably opt2
	WBUFL(buf,28) = p->option;
	WBUFL(buf,32) = p->karma;
	WBUFL(buf,36) = p->manner;
	WBUFW(buf,40) = min(p->status_point, INT16_MAX);
	WBUFL(buf,42) = p->hp;
	WBUFL(buf,46) = p->max_hp;
	offset+=4;
	buf = WBUFP(buffer,offset);
	WBUFW(buf,46) = min(p->sp, INT16_MAX);
	WBUFW(buf,48) = min(p->max_sp, INT16_MAX);
	WBUFW(buf,50) = DEFAULT_WALK_SPEED; // p->speed;
	WBUFW(buf,52) = p->class_;
	WBUFW(buf,54) = p->hair;

	//When the weapon is sent and your option is riding, the client crashes on login!?
	WBUFW(buf,56) = p->option&(0x20|0x80000|0x100000|0x200000|0x400000|0x800000|0x1000000|0x2000000|0x4000000|0x8000000) ? 0 : p->weapon;

	WBUFW(buf,58) = p->base_level;
	WBUFW(buf,60) = min(p->skill_point, INT16_MAX);
	WBUFW(buf,62) = p->head_bottom;
	WBUFW(buf,64) = p->shield;
	WBUFW(buf,66) = p->head_top;
	WBUFW(buf,68) = p->head_mid;
	WBUFW(buf,70) = p->hair_color;
	WBUFW(buf,72) = p->clothes_color;
	memcpy(WBUFP(buf,74), p->name, NAME_LENGTH);
	WBUFB(buf,98) = min(p->str, UINT8_MAX);
	WBUFB(buf,99) = min(p->agi, UINT8_MAX);
	WBUFB(buf,100) = min(p->vit, UINT8_MAX);
	WBUFB(buf,101) = min(p->int_, UINT8_MAX);
	WBUFB(buf,102) = min(p->dex, UINT8_MAX);
	WBUFB(buf,103) = min(p->luk, UINT8_MAX);
	WBUFW(buf,104) = p->slot;
	WBUFW(buf,106) = ( p->rename > 0 ) ? 0 : 1;
	offset += 2;
#if (PACKETVER >= 20100720 && PACKETVER <= 20100727) || PACKETVER >= 20100803
	mapindex_getmapname_ext(mapindex_id2name(p->last_point.map), (char*)WBUFP(buf,108));
	offset += MAP_NAME_LENGTH_EXT;
#endif
#if PACKETVER >= 20100803
#if PACKETVER > 20130000
	WBUFL(buf,124) = (p->delete_date?TOL(p->delete_date-time(NULL)):0);
#else
	WBUFL(buf,124) = TOL(p->delete_date);
#endif
	offset += 4;
#endif
#if PACKETVER >= 20110111
	WBUFL(buf,128) = p->robe;
	offset += 4;
#endif
#if PACKETVER != 20111116 //2011-11-16 wants 136, ask gravity.
	#if PACKETVER >= 20110928
		// change slot feature (0 = disabled, otherwise enabled)
		if( (charserv_config.charmove_config.char_move_enabled)==0 )
			WBUFL(buf,132) = 0;
		else if( charserv_config.charmove_config.char_moves_unlimited )
			WBUFL(buf,132) = 1;
		else
			WBUFL(buf,132) = max( 0, (int)p->character_moves );
		offset += 4;
	#endif
	#if PACKETVER >= 20111025
		WBUFL(buf,136) = ( p->rename > 0 ) ? 1 : 0;  // (0 = disabled, otherwise displays "Add-Ons" sidebar)
		offset += 4;
	#endif
#endif

	return 106+offset;
}


int char_married(int pl1, int pl2)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `partner_id` FROM `%s` WHERE `char_id` = '%d'", schema_config.char_db, pl1) )
		Sql_ShowDebug(sql_handle);
	else if( SQL_SUCCESS == Sql_NextRow(sql_handle) )
	{
		char* data;

		Sql_GetData(sql_handle, 0, &data, NULL);
		if( pl2 == atoi(data) )
		{
			Sql_FreeResult(sql_handle);
			return 1;
		}
	}
	Sql_FreeResult(sql_handle);
	return 0;
}

int char_child(int parent_id, int child_id)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `child` FROM `%s` WHERE `char_id` = '%d'", schema_config.char_db, parent_id) )
		Sql_ShowDebug(sql_handle);
	else if( SQL_SUCCESS == Sql_NextRow(sql_handle) )
	{
		char* data;

		Sql_GetData(sql_handle, 0, &data, NULL);
		if( child_id == atoi(data) )
		{
			Sql_FreeResult(sql_handle);
			return 1;
		}
	}
	Sql_FreeResult(sql_handle);
	return 0;
}

int char_family(int cid1, int cid2, int cid3)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`partner_id`,`child` FROM `%s` WHERE `char_id` IN ('%d','%d','%d')", schema_config.char_db, cid1, cid2, cid3) )
		Sql_ShowDebug(sql_handle);
	else while( SQL_SUCCESS == Sql_NextRow(sql_handle) )
	{
		int charid;
		int partnerid;
		int childid;
		char* data;

		Sql_GetData(sql_handle, 0, &data, NULL); charid = atoi(data);
		Sql_GetData(sql_handle, 1, &data, NULL); partnerid = atoi(data);
		Sql_GetData(sql_handle, 2, &data, NULL); childid = atoi(data);

		if( (cid1 == charid    && ((cid2 == partnerid && cid3 == childid  ) || (cid2 == childid   && cid3 == partnerid))) ||
			(cid1 == partnerid && ((cid2 == charid    && cid3 == childid  ) || (cid2 == childid   && cid3 == charid   ))) ||
			(cid1 == childid   && ((cid2 == charid    && cid3 == partnerid) || (cid2 == partnerid && cid3 == charid   ))) )
		{
			Sql_FreeResult(sql_handle);
			return childid;
		}
	}
	Sql_FreeResult(sql_handle);
	return 0;
}

//----------------------------------------------------------------------
// Force disconnection of an online player (with account value) by [Yor]
//----------------------------------------------------------------------
void char_disconnect_player(int account_id)
{
	int i;
	struct char_session_data* sd;

	// disconnect player if online on char-server
	ARR_FIND( 0, fd_max, i, session[i] && (sd = (struct char_session_data*)session[i]->session_data) && sd->account_id == account_id );
	if( i < fd_max )
		set_eof(i);
}



void char_auth_ok(int fd, struct char_session_data *sd) {
	struct online_char_data* character;

	if( (character = (struct online_char_data*)idb_get(online_char_db, sd->account_id)) != NULL )
	{	// check if character is not online already. [Skotlex]
		if (character->server > -1)
		{	//Character already online. KICK KICK KICK
			mapif_disconnectplayer(map_server[character->server].fd, character->account_id, character->char_id, 2);
			if (character->waiting_disconnect == INVALID_TIMER)
				character->waiting_disconnect = add_timer(gettick()+20000, char_chardb_waiting_disconnect, character->account_id, 0);
			chclif_send_auth_result(fd,8);
			return;
		}
		if (character->fd >= 0 && character->fd != fd)
		{	//There's already a connection from this account that hasn't picked a char yet.
			chclif_send_auth_result(fd,8);
			return;
		}
		character->fd = fd;
	}

	chlogif_send_reqaccdata(login_fd,sd); // request account data

	// mark session as 'authed'
	sd->auth = true;

	// set char online on charserver
	char_set_charselect(sd->account_id);

	// continues when account data is received...
}

void char_read_fame_list(void)
{
	int i;
	char* data;
	size_t len;

	// Empty ranking lists
	memset(smith_fame_list, 0, sizeof(smith_fame_list));
	memset(chemist_fame_list, 0, sizeof(chemist_fame_list));
	memset(taekwon_fame_list, 0, sizeof(taekwon_fame_list));
	memset(pvprank_fame_list, 0, sizeof(pvprank_fame_list));
	memset(bgrank_fame_list, 0, sizeof(bgrank_fame_list));
	memset(bg_fame_list, 0, sizeof(bg_fame_list));

	// Build Blacksmith ranking list
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`fame`,`name` FROM `%s` WHERE `fame`>0 AND (`class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d') ORDER BY `fame` DESC LIMIT 0,%d", schema_config.char_db, JOB_BLACKSMITH, JOB_WHITESMITH, JOB_BABY_BLACKSMITH, JOB_MECHANIC, JOB_MECHANIC_T, JOB_BABY_MECHANIC, fame_list_size_smith) )
		Sql_ShowDebug(sql_handle);
	for( i = 0; i < fame_list_size_smith && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		Sql_GetData(sql_handle, 0, &data, NULL); smith_fame_list[i].id = atoi(data);
		Sql_GetData(sql_handle, 1, &data, &len); smith_fame_list[i].fame = atoi(data);
		Sql_GetData(sql_handle, 2, &data, &len); memcpy(smith_fame_list[i].name, data, min(len, NAME_LENGTH));
	}
	// Build Alchemist ranking list
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`fame`,`name` FROM `%s` WHERE `fame`>0 AND (`class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d' OR `class`='%d') ORDER BY `fame` DESC LIMIT 0,%d", schema_config.char_db, JOB_ALCHEMIST, JOB_CREATOR, JOB_BABY_ALCHEMIST, JOB_GENETIC, JOB_GENETIC_T, JOB_BABY_GENETIC, fame_list_size_chemist) )
		Sql_ShowDebug(sql_handle);
	for( i = 0; i < fame_list_size_chemist && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		Sql_GetData(sql_handle, 0, &data, NULL); chemist_fame_list[i].id = atoi(data);
		Sql_GetData(sql_handle, 1, &data, &len); chemist_fame_list[i].fame = atoi(data);
		Sql_GetData(sql_handle, 2, &data, &len); memcpy(chemist_fame_list[i].name, data, min(len, NAME_LENGTH));
	}
	// Build Taekwon ranking list
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`fame`,`name` FROM `%s` WHERE `fame`>0 AND (`class`='%d') ORDER BY `fame` DESC LIMIT 0,%d", schema_config.char_db, JOB_TAEKWON, fame_list_size_taekwon) )
		Sql_ShowDebug(sql_handle);
	for( i = 0; i < fame_list_size_taekwon && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		Sql_GetData(sql_handle, 0, &data, NULL); taekwon_fame_list[i].id = atoi(data);
		Sql_GetData(sql_handle, 1, &data, &len); taekwon_fame_list[i].fame = atoi(data);
		Sql_GetData(sql_handle, 2, &data, &len); memcpy(taekwon_fame_list[i].name, data, min(len, NAME_LENGTH));
	}
	// Build PK Rank ranking list [Zephyrus]
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_pk`.`char_id`, `char_pk`.`score`, `%s`.`name` FROM `char_pk` LEFT JOIN `%s` ON `%s`.`char_id` = `char_pk`.`char_id` WHERE `char_pk`.`score` > '0' ORDER BY `char_pk`.`score` DESC LIMIT 0,%d", schema_config.char_db, schema_config.char_db, schema_config.char_db, fame_list_size_pvprank) )
		Sql_ShowDebug(sql_handle);
	for( i = 0; i < fame_list_size_pvprank && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		Sql_GetData(sql_handle, 0, &data, NULL); pvprank_fame_list[i].id = atoi(data);
		Sql_GetData(sql_handle, 1, &data, &len); pvprank_fame_list[i].fame = atoi(data);
		Sql_GetData(sql_handle, 2, &data, &len); memcpy(pvprank_fame_list[i].name, data, min(len, NAME_LENGTH));
	}
	Sql_FreeResult(sql_handle);
	// Build BG Rank ranking list [Zephyrus]
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_bg`.`char_id`, `char_bg`.`rank_points`, `%s`.`name` FROM `char_bg` LEFT JOIN `%s` ON `%s`.`char_id` = `char_bg`.`char_id` WHERE `char_bg`.`rank_points` > '0' ORDER BY `char_bg`.`rank_points` DESC LIMIT 0,%d", schema_config.char_db, schema_config.char_db, schema_config.char_db, fame_list_size_bgrank) )
		Sql_ShowDebug(sql_handle);
	for( i = 0; i < fame_list_size_bgrank && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		Sql_GetData(sql_handle, 0, &data, NULL); bgrank_fame_list[i].id = atoi(data);
		Sql_GetData(sql_handle, 1, &data, &len); bgrank_fame_list[i].fame = atoi(data);
		Sql_GetData(sql_handle, 2, &data, &len); memcpy(bgrank_fame_list[i].name, data, min(len, NAME_LENGTH));
	}
	Sql_FreeResult(sql_handle);
	// Build BG Normal ranking list [Zephyrus]
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_bg`.`char_id`, `char_bg`.`points`, `%s`.`name` FROM `char_bg` LEFT JOIN `%s` ON `%s`.`char_id` = `char_bg`.`char_id` WHERE `char_bg`.`points` > '0' ORDER BY `char_bg`.`points` DESC LIMIT 0,%d", schema_config.char_db, schema_config.char_db, schema_config.char_db, fame_list_size_bg) )
		Sql_ShowDebug(sql_handle);
	for( i = 0; i < fame_list_size_bg && SQL_SUCCESS == Sql_NextRow(sql_handle); ++i )
	{
		Sql_GetData(sql_handle, 0, &data, NULL); bg_fame_list[i].id = atoi(data);
		Sql_GetData(sql_handle, 1, &data, &len); bg_fame_list[i].fame = atoi(data);
		Sql_GetData(sql_handle, 2, &data, &len); memcpy(bg_fame_list[i].name, data, min(len, NAME_LENGTH));
	}
	Sql_FreeResult(sql_handle);
}

/*----------------------------------------------------------------------------------------------------------*/
/* Ranking Reset */
/*----------------------------------------------------------------------------------------------------------*/
int char_ranking_reset(int type)
{
	unsigned char buf[4];

	ShowInfo("Ranking Reset Request from Map Server...\n");
	switch( type )
	{
	case 0:
		if( SQL_ERROR == Sql_Query(sql_handle, "TRUNCATE TABLE `char_wstats`") )
			Sql_ShowDebug(sql_handle);
		if( SQL_ERROR == Sql_Query(sql_handle, "TRUNCATE TABLE `char_woe_log`") )
			Sql_ShowDebug(sql_handle); // Kill log cleanup
		if( SQL_ERROR == Sql_Query(sql_handle, "TRUNCATE TABLE `skill_count`") )
			Sql_ShowDebug(sql_handle);
		break;
	case 1:
		if( SQL_ERROR == Sql_Query(sql_handle, "TRUNCATE TABLE `char_bg`") )
			Sql_ShowDebug(sql_handle); // Data Cleanup
		else
		{ // Trophy Updates
			struct item it;
			const char* position[] = { "1st", "2nd", "3rd" };
			const char* medal[] = { "bg_gold", "bg_silver", "bg_bronze" };
			char title[40], body[200];
			int i;

			memset(&it,0,sizeof(it));
			it.amount = it.identify = 1;

			for( i = 0; i < 3; i++ )
			{ // Ranked Rewards
				if( !bgrank_fame_list[i].id )
					continue;

				it.nameid = charserv_config.bg_ranked_rewards[i];
				sprintf(title,"%s Place Ranked Battleground",position[i]);
				sprintf(body,"Congratulations, you won the %s place in the Battleground Ranked Matchs of the week.",position[i]);
				mail_sendmail(0,"Server",bgrank_fame_list[i].id,bgrank_fame_list[i].name,title,body,0,(charserv_config.bg_ranked_rewards[i] ? &it : NULL));
				if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `%s` = `%s` + 1 WHERE `char_id` = '%d'", schema_config.char_db, medal[i], medal[i], bgrank_fame_list[i].id) )
					Sql_ShowDebug(sql_handle);
			}

			for( i = 0; i < 3; i++ )
			{ // Regular Rewards
				if( !bg_fame_list[i].id )
					continue;

				it.nameid = charserv_config.bg_regular_rewards[i];
				sprintf(title,"%s Place Regular Battleground",position[i]);
				sprintf(body,"Congratulations, you won the %s place in the Battleground Regular Matchs of the week.",position[i]);
				mail_sendmail(0,"Server",bg_fame_list[i].id,bg_fame_list[i].name,title,body,0,(charserv_config.bg_regular_rewards[i] ? &it : NULL));
				if( SQL_ERROR == Sql_Query(sql_handle, "UPDATE `%s` SET `%s` = `%s` + 1 WHERE `char_id` = '%d'", schema_config.char_db, medal[i], medal[i], bg_fame_list[i].id) )
					Sql_ShowDebug(sql_handle);
			}

			if( SQL_ERROR == Sql_Query(sql_handle, "TRUNCATE TABLE `char_bg_log`") )
				Sql_ShowDebug(sql_handle); // Kill log cleanup

			memset(bgrank_fame_list, 0, sizeof(bgrank_fame_list)); // Reset BG Ranked
			memset(bg_fame_list, 0, sizeof(bg_fame_list)); // Reset BG Normal

			// Send Lists to Map Servers
			chmapif_send_fame_list_single(-1,5);
			chmapif_send_fame_list_single(-1,6);
		}
		break;
	case 2:
		if( SQL_ERROR == Sql_Query(sql_handle, "TRUNCATE TABLE `char_pvp`") )
			Sql_ShowDebug(sql_handle);
		break;
	}

	WBUFW(buf,0) = 0x2b31;
	WBUFW(buf,2) = type;
	chmapif_sendall(buf,4);

	return 0;
}

//Loads a character's name and stores it in the buffer given (must be NAME_LENGTH in size)
//Returns 1 on found, 0 on not found (buffer is filled with Unknown char name)
int char_loadName(int char_id, char* name){
	char* data;
	size_t len;

	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `name` FROM `%s` WHERE `char_id`='%d'", schema_config.char_db, char_id) )
		Sql_ShowDebug(sql_handle);
	else if( SQL_SUCCESS == Sql_NextRow(sql_handle) )
	{
		Sql_GetData(sql_handle, 0, &data, &len);
		safestrncpy(name, data, NAME_LENGTH);
		return 1;
	}
	else
	{
		safestrncpy(name, charserv_config.char_config.unknown_char_name, NAME_LENGTH);
	}
	return 0;
}

// Searches for the mapserver that has a given map (and optionally ip/port, if not -1).
// If found, returns the server's index in the 'server' array (otherwise returns -1).
int char_search_mapserver(unsigned short map, uint32 ip, uint16 port){
	int i, j;

	for(i = 0; i < ARRAYLENGTH(map_server); i++)
	{
		if (map_server[i].fd > 0
		&& (ip == (uint32)-1 || map_server[i].ip == ip)
		&& (port == (uint16)-1 || map_server[i].port == port))
		{
			for (j = 0; map_server[i].map[j]; j++)
				if (map_server[i].map[j] == map)
					return i;
		}
	}

	return -1;
}

/**
 * Test to know if an IP come from LAN or WAN.
 * @param ip: ip to check if in auth network
 * @return 0 if from wan, or subnet_map_ip if lan
 **/
int char_lan_subnetcheck(uint32 ip){
	int i;
	ARR_FIND( 0, subnet_count, i, (subnet[i].char_ip & subnet[i].mask) == (ip & subnet[i].mask) );
	if( i < subnet_count ) {
		ShowInfo("Subnet check [%u.%u.%u.%u]: Matches "CL_CYAN"%u.%u.%u.%u/%u.%u.%u.%u"CL_RESET"\n", CONVIP(ip), CONVIP(subnet[i].char_ip & subnet[i].mask), CONVIP(subnet[i].mask));
		return subnet[i].map_ip;
	} else {
		ShowInfo("Subnet check [%u.%u.%u.%u]: "CL_CYAN"WAN"CL_RESET"\n", CONVIP(ip));
		return 0;
	}
}

// Console Command Parser [Wizputer]
//FIXME to be remove (moved to cnslif / will be done once map/char/login, all have their cnslif interface ready)
int parse_console(const char* buf){
	return cnslif_parse(buf);
}

//------------------------------------------------
//Pincode system
//------------------------------------------------
int char_pincode_compare( int fd, struct char_session_data* sd, char* pin ){
	if( strcmp( sd->pincode, pin ) == 0 ){
		sd->pincode_try = 0;
		return 1;
	}else{
		chclif_pincode_sendstate( fd, sd, PINCODE_WRONG );

		if( charserv_config.pincode_config.pincode_maxtry && ++sd->pincode_try >= charserv_config.pincode_config.pincode_maxtry ){
			chlogif_pincode_notifyLoginPinError( sd->account_id );
		}

		return 0;
	}
}


void char_pincode_decrypt( uint32 userSeed, char* pin ){
	int i;
	char tab[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	char *buf;
	
	for( i = 1; i < 10; i++ ){
		int pos;
		uint32 multiplier = 0x3498, baseSeed = 0x881234;
		
		userSeed = baseSeed + userSeed * multiplier;
		pos = userSeed % ( i + 1 );
		if( i != pos ){
			tab[i] ^= tab[pos];
			tab[pos] ^= tab[i];
			tab[i] ^= tab[pos];
		}
	}

	buf = (char *)aMalloc( sizeof(char) * ( PINCODE_LENGTH + 1 ) );
	memset( buf, 0, PINCODE_LENGTH + 1 );
	for( i = 0; i < PINCODE_LENGTH; i++ ){
		sprintf( buf + i, "%d", tab[pin[i] - '0'] );
	}
	strcpy( pin, buf );
	aFree( buf );
}

//------------------------------------------------
//Invoked 15 seconds after mapif_disconnectplayer in case the map server doesn't
//replies/disconnect the player we tried to kick. [Skotlex]
//------------------------------------------------
int char_chardb_waiting_disconnect(int tid, unsigned int tick, int id, intptr_t data)
{
	struct online_char_data* character;
	if ((character = (struct online_char_data*)idb_get(online_char_db, id)) != NULL && character->waiting_disconnect == tid)
	{	//Mark it offline due to timeout.
		character->waiting_disconnect = INVALID_TIMER;
		char_set_char_offline(character->char_id, character->account_id);
	}
	return 0;
}

/**
 * @see DBApply
 */
static int char_online_data_cleanup_sub(DBKey key, DBData *data, va_list ap)
{
	struct online_char_data *character= db_data2ptr(data);
	if (character->fd != -1)
		return 0; //Character still connected
	if (character->server == -2) //Unknown server.. set them offline
		char_set_char_offline(character->char_id, character->account_id);
	if (character->server < 0)
		//Free data from players that have not been online for a while.
		db_remove(online_char_db, key);
	return 0;
}

static int char_online_data_cleanup(int tid, unsigned int tick, int id, intptr_t data){
	online_char_db->foreach(online_char_db, char_online_data_cleanup_sub);
	return 0;
}



//----------------------------------
// Reading Lan Support configuration
// Rewrote: Anvanced subnet check [LuzZza]
//----------------------------------
int char_lan_config_read(const char *lancfgName) {
	FILE *fp;
	int line_num = 0, s_subnet=ARRAYLENGTH(subnet);
	char line[1024], w1[64], w2[64], w3[64], w4[64];

	if((fp = fopen(lancfgName, "r")) == NULL) {
		ShowWarning("LAN Support configuration file is not found: %s\n", lancfgName);
		return 1;
	}

	while(fgets(line, sizeof(line), fp)) {
		line_num++;
		if ((line[0] == '/' && line[1] == '/') || line[0] == '\n' || line[1] == '\n')
			continue;

		if(sscanf(line,"%63[^:]: %63[^:]:%63[^:]:%63[^\r\n]", w1, w2, w3, w4) != 4) {

			ShowWarning("Error syntax of configuration file %s in line %d.\n", lancfgName, line_num);
			continue;
		}

		remove_control_chars(w1);
		remove_control_chars(w2);
		remove_control_chars(w3);
		remove_control_chars(w4);

		if( strcmpi(w1, "subnet") == 0 ){
			if(subnet_count>=s_subnet) { //We skip instead of break in case we want to add other conf in that file.
				ShowError("%s: Too many subnets defined, skipping line %d...\n", lancfgName, line_num);
				continue;
			}
			subnet[subnet_count].mask = str2ip(w2);
			subnet[subnet_count].char_ip = str2ip(w3);
			subnet[subnet_count].map_ip = str2ip(w4);
			if( (subnet[subnet_count].char_ip & subnet[subnet_count].mask) != (subnet[subnet_count].map_ip & subnet[subnet_count].mask) )
			{
				ShowError("%s: Configuration Error: The char server (%s) and map server (%s) belong to different subnetworks!\n", lancfgName, w3, w4);
				continue;
			}
			subnet_count++;
		}
	}

	if( subnet_count > 1 ) /* only useful if there is more than 1 */
		ShowStatus("Read information about %d subnetworks.\n", subnet_count);

	fclose(fp);
	return 0;
}

/**
 * Check if our table are all ok in sqlserver
 * Char tables to check
 * @return 0:fail, 1:success
 */
bool char_checkdb(void){
	int i;
	const char* sqltable[] = {
		schema_config.char_db, schema_config.hotkey_db, schema_config.scdata_db, schema_config.cart_db, 
                schema_config.inventory_db, schema_config.charlog_db, schema_config.storage_db, 
                schema_config.reg_db, schema_config.skill_db, schema_config.interlog_db, schema_config.memo_db,
		schema_config.guild_db, schema_config.guild_alliance_db, schema_config.guild_castle_db, 
                schema_config.guild_expulsion_db, schema_config.guild_member_db, 
                schema_config.guild_skill_db, schema_config.guild_position_db, schema_config.guild_storage_db,
		schema_config.party_db, schema_config.pet_db, schema_config.friend_db, schema_config.mail_db, 
                schema_config.auction_db, schema_config.quest_db, schema_config.homunculus_db, schema_config.skill_homunculus_db,
                schema_config.mercenary_db, schema_config.mercenary_owner_db,
		schema_config.elemental_db, schema_config.ragsrvinfo_db, schema_config.skillcooldown_db, schema_config.bonus_script_db
	};
	ShowInfo("Start checking DB integrity\n");
	for (i=0; i<ARRAYLENGTH(sqltable); i++){ //check if they all exist and we can acces them in sql-server
		if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  * from `%s`;", sqltable[i]) )
			return false;
	}
	//checking char_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`account_id`,`char_num`,`name`,`class`,"
		"`base_level`,`job_level`,`base_exp`,`job_exp`,`zeny`,`str`,`agi`,`vit`,`int`,`dex`,`luk`,"
		"`max_hp`,`hp`,`max_sp`,`sp`,`status_point`,`skill_point`,`option`,`karma`,`manner`,`party_id`,"
		"`guild_id`,`pet_id`,`homun_id`,`elemental_id`,`hair`,`hair_color`,`clothes_color`,`weapon`,"
		"`shield`,`head_top`,`head_mid`,`head_bottom`,`robe`,`last_map`,`last_x`,`last_y`,`save_map`,"
		"`save_x`,`save_y`,`partner_id`,`online`,`father`,`mother`,`child`,`fame`,`rename`,`delete_date`,"
		"`moves`,`unban_time`,`font`"
		" from `%s`;", schema_config.char_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking charlog_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `time`,`char_msg`,`account_id`,`char_num`,`name`,"
			 "`str`,`agi`,`vit`,`int`,`dex`,`luk`,`hair`,`hair_color`"
		" from `%s`;", schema_config.charlog_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking reg_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT `char_id`,`str`,`value`,`type`,`account_id` from `%s`;", schema_config.reg_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking hotkey_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `char_id`,`hotkey`,`type`,`itemskill_id`,`skill_lvl`"
		" from `%s`;", schema_config.hotkey_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking scdata_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `account_id`,`char_id`,`type`,`tick`,`val1`,`val2`,`val3`,`val4`"
		" from `%s`;", schema_config.scdata_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking skill_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `char_id`,`id`,`lv`,`flag` from `%s`;", schema_config.skill_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking interlog_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `time`,`log` from `%s`;", schema_config.interlog_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking memo_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `memo_id`,`char_id`,`map`,`x`,`y` from `%s`;", schema_config.memo_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking guild_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `guild_id`,`name`,`char_id`,`master`,`guild_lv`,"
			"`connect_member`,`max_member`,`average_lv`,`exp`,`next_exp`,`skill_point`,`mes1`,`mes2`,"
			"`emblem_len`,`emblem_id`,`emblem_data`"
			" from `%s`;", schema_config.guild_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking guild_alliance_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `guild_id`,`opposition`,`alliance_id`,`name` from `%s`;", schema_config.guild_alliance_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking guild_castle_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `castle_id`,`guild_id`,`economy`,`defense`,`triggerE`,"
			"`triggerD`,`nextTime`,`payTime`,`createTime`,`visibleC`,`visibleG0`,`visibleG1`,`visibleG2`,"
			"`visibleG3`,`visibleG4`,`visibleG5`,`visibleG6`,`visibleG7` "
			" from `%s`;", schema_config.guild_castle_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking guild_expulsion_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `guild_id`,`account_id`,`name`,`mes` from `%s`;", schema_config.guild_expulsion_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking guild_member_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `guild_id`,`account_id`,`char_id`,`hair`,"
			"`hair_color`,`gender`,`class`,`lv`,`exp`,`exp_payper`,`online`,`position`,`name`"
			" from `%s`;", schema_config.guild_member_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking guild_skill_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `guild_id`,`id`,`lv` from `%s`;", schema_config.guild_skill_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking guild_position_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `guild_id`,`position`,`name`,`mode`,`exp_mode` from `%s`;", schema_config.guild_position_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking party_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `party_id`,`name`,`exp`,`item`,`leader_id`,`leader_char` from `%s`;", schema_config.party_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking pet_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `pet_id`,`class`,`name`,`account_id`,`char_id`,`level`,"
			"`egg_id`,`equip`,`intimate`,`hungry`,`rename_flag`,`incubate`"
			" from `%s`;", schema_config.pet_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking friend_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `char_id`,`friend_account`,`friend_id` from `%s`;", schema_config.friend_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking mail_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `id`,`send_name`,`send_id`,`dest_name`,`dest_id`,"
			"`title`,`message`,`time`,`status`,`zeny`,`nameid`,`amount`,`refine`,`attribute`,`identify`,"
			"`card0`,`card1`,`card2`,`card3`,`unique_id`, `bound`"
			" from `%s`;", schema_config.mail_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking auction_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `auction_id`,`seller_id`,`seller_name`,`buyer_id`,`buyer_name`,"
			"`price`,`buynow`,`hours`,`timestamp`,`nameid`,`item_name`,`type`,`refine`,`attribute`,`card0`,`card1`,"
			"`card2`,`card3`,`unique_id` "
			"from `%s`;", schema_config.auction_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking quest_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `char_id`,`quest_id`,`state`,`time`,`count1`,`count2`,`count3` from `%s`;", schema_config.quest_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking homunculus_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `homun_id`,`char_id`,`class`,`prev_class`,`name`,`level`,`exp`,`intimacy`,`hunger`,"
			"`str`,`agi`,`vit`,`int`,`dex`,`luk`,`hp`,`max_hp`,`sp`,`max_sp`,`skill_point`,`alive`,`rename_flag`,`vaporize` "
			" from `%s`;", schema_config.homunculus_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking skill_homunculus_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `homun_id`,`id`,`lv` from `%s`;", schema_config.skill_homunculus_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking mercenary_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `mer_id`,`char_id`,`class`,`hp`,`sp`,`kill_counter`,`life_time` from `%s`;", schema_config.mercenary_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking mercenary_owner_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `char_id`,`merc_id`,`arch_calls`,`arch_faith`,"
			"`spear_calls`,`spear_faith`,`sword_calls`,`sword_faith`"
			" from `%s`;", schema_config.mercenary_owner_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking elemental_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `ele_id`,`char_id`,`class`,`mode`,`hp`,`sp`,`max_hp`,`max_sp`,"
			"`atk1`,`atk2`,`matk`,`aspd`,`def`,`mdef`,`flee`,`hit`,`life_time` "
			" from `%s`;", schema_config.elemental_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking ragsrvinfo_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `index`,`name`,`exp`,`jexp`,`drop` from `%s`;", schema_config.ragsrvinfo_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking skillcooldown_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `account_id`,`char_id`,`skill`,`tick` from `%s`;", schema_config.skillcooldown_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking bonus_script_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `char_id`,`script`,`tick`,`flag`,`type`,`icon` from `%s`;", schema_config.bonus_script_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	
	//checking cart_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `id`,`char_id`,`nameid`,`amount`,`equip`,`identify`,`refine`,"
			"`attribute`,`card0`,`card1`,`card2`,`card3`,`expire_time`,`bound`,`unique_id`"
		" from `%s`;", schema_config.cart_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking inventory_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `id`,`char_id`,`nameid`,`amount`,`equip`,`identify`,`refine`,"
			"`attribute`,`card0`,`card1`,`card2`,`card3`,`expire_time`,`favorite`,`bound`,`unique_id`"
		" from `%s`;", schema_config.inventory_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking storage_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `id`,`account_id`,`nameid`,`amount`,`equip`,`identify`,`refine`,"
			"`attribute`,`card0`,`card1`,`card2`,`card3`,`expire_time`,`bound`,`unique_id`"
		" from `%s`;", schema_config.storage_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	//checking guild_storage_db
	if( SQL_ERROR == Sql_Query(sql_handle, "SELECT  `id`,`guild_id`,`nameid`,`amount`,`equip`,`identify`,`refine`,"
			"`attribute`,`card0`,`card1`,`card2`,`card3`,`expire_time`,`bound`,`unique_id`"
		" from `%s`;", schema_config.guild_storage_db) ){
		Sql_ShowDebug(sql_handle);
		return false;
	}
	
	ShowInfo("DB integrity check finished with success\n");
	return true;
}

void char_sql_config_read(const char* cfgName) {
	char line[1024], w1[1024], w2[1024];
	FILE* fp;

	if ((fp = fopen(cfgName, "r")) == NULL) {
		ShowError("File not found: %s\n", cfgName);
		return;
	}

	while(fgets(line, sizeof(line), fp)) {
		if(line[0] == '/' && line[1] == '/')
			continue;

		if (sscanf(line, "%1023[^:]: %1023[^\r\n]", w1, w2) != 2)
			continue;

		if(!strcmpi(w1,"char_db"))
			safestrncpy(schema_config.char_db, w2, sizeof(schema_config.char_db));
		else if(!strcmpi(w1,"scdata_db"))
			safestrncpy(schema_config.scdata_db, w2, sizeof(schema_config.scdata_db));
		else if(!strcmpi(w1,"cart_db"))
			safestrncpy(schema_config.cart_db, w2, sizeof(schema_config.cart_db));
		else if(!strcmpi(w1,"inventory_db"))
			safestrncpy(schema_config.inventory_db, w2, sizeof(schema_config.inventory_db));
		else if(!strcmpi(w1,"charlog_db"))
			safestrncpy(schema_config.charlog_db, w2, sizeof(schema_config.charlog_db));
		else if(!strcmpi(w1,"storage_db"))
			safestrncpy(schema_config.storage_db, w2, sizeof(schema_config.storage_db));
		else if(!strcmpi(w1,"reg_db"))
			safestrncpy(schema_config.reg_db, w2, sizeof(schema_config.reg_db));
		else if(!strcmpi(w1,"skill_db"))
			safestrncpy(schema_config.skill_db, w2, sizeof(schema_config.skill_db));
		else if(!strcmpi(w1,"interlog_db"))
			safestrncpy(schema_config.interlog_db, w2, sizeof(schema_config.interlog_db));
		else if(!strcmpi(w1,"memo_db"))
			safestrncpy(schema_config.memo_db, w2, sizeof(schema_config.memo_db));
		else if(!strcmpi(w1,"guild_db"))
			safestrncpy(schema_config.guild_db, w2, sizeof(schema_config.guild_db));
		else if(!strcmpi(w1,"guild_alliance_db"))
			safestrncpy(schema_config.guild_alliance_db, w2, sizeof(schema_config.guild_alliance_db));
		else if(!strcmpi(w1,"guild_castle_db"))
			safestrncpy(schema_config.guild_castle_db, w2, sizeof(schema_config.guild_castle_db));
		else if(!strcmpi(w1,"guild_expulsion_db"))
			safestrncpy(schema_config.guild_expulsion_db, w2, sizeof(schema_config.guild_expulsion_db));
		else if(!strcmpi(w1,"guild_member_db"))
			safestrncpy(schema_config.guild_member_db, w2, sizeof(schema_config.guild_member_db));
		else if(!strcmpi(w1,"guild_skill_db"))
			safestrncpy(schema_config.guild_skill_db, w2, sizeof(schema_config.guild_skill_db));
		else if(!strcmpi(w1,"guild_position_db"))
			safestrncpy(schema_config.guild_position_db, w2, sizeof(schema_config.guild_position_db));
		else if(!strcmpi(w1,"guild_storage_db"))
			safestrncpy(schema_config.guild_storage_db, w2, sizeof(schema_config.guild_storage_db));
		else if(!strcmpi(w1,"party_db"))
			safestrncpy(schema_config.party_db, w2, sizeof(schema_config.party_db));
		else if(!strcmpi(w1,"pet_db"))
			safestrncpy(schema_config.pet_db, w2, sizeof(schema_config.pet_db));
		else if(!strcmpi(w1,"mail_db"))
			safestrncpy(schema_config.mail_db, w2, sizeof(schema_config.mail_db));
		else if(!strcmpi(w1,"auction_db"))
			safestrncpy(schema_config.auction_db, w2, sizeof(schema_config.auction_db));
		else if(!strcmpi(w1,"friend_db"))
			safestrncpy(schema_config.friend_db, w2, sizeof(schema_config.friend_db));
		else if(!strcmpi(w1,"hotkey_db"))
			safestrncpy(schema_config.hotkey_db, w2, sizeof(schema_config.hotkey_db));
		else if(!strcmpi(w1,"quest_db"))
			safestrncpy(schema_config.quest_db,w2,sizeof(schema_config.quest_db));
		else if(!strcmpi(w1,"homunculus_db"))
			safestrncpy(schema_config.homunculus_db,w2,sizeof(schema_config.homunculus_db));
		else if(!strcmpi(w1,"skill_homunculus_db"))
			safestrncpy(schema_config.skill_homunculus_db,w2,sizeof(schema_config.skill_homunculus_db));
		else if(!strcmpi(w1,"mercenary_db"))
			safestrncpy(schema_config.mercenary_db,w2,sizeof(schema_config.mercenary_db));
		else if(!strcmpi(w1,"mercenary_owner_db"))
			safestrncpy(schema_config.mercenary_owner_db,w2,sizeof(schema_config.mercenary_owner_db));
		else if(!strcmpi(w1,"elemental_db"))
			safestrncpy(schema_config.elemental_db,w2,sizeof(schema_config.elemental_db));
		else if(!strcmpi(w1,"skillcooldown_db"))
			safestrncpy(schema_config.skillcooldown_db, w2, sizeof(schema_config.skillcooldown_db));
		else if(!strcmpi(w1,"bonus_script_db"))
			safestrncpy(schema_config.bonus_script_db, w2, sizeof(schema_config.bonus_script_db));
		// eAmod
		else if(!strcmpi(w1,"rentstorage_db"))
			safestrncpy(schema_config.rentstorage_db, w2, sizeof(schema_config.rentstorage_db));
		else if(!strcmpi(w1,"achievement_db"))
			safestrncpy(schema_config.achievement_db, w2, sizeof(schema_config.achievement_db));
		//support the import command, just like any other config
		else if(!strcmpi(w1,"import"))
			char_sql_config_read(w2);
	}
	fclose(fp);
	ShowInfo("Done reading %s.\n", cfgName);
}


void char_set_default_sql(){
//	schema_config.db_use_sqldbs;
	safestrncpy(schema_config.db_path,"db",sizeof(schema_config.db_path));
	safestrncpy(schema_config.char_db,"char",sizeof(schema_config.char_db));
	safestrncpy(schema_config.scdata_db,"sc_data",sizeof(schema_config.scdata_db));
	safestrncpy(schema_config.cart_db,"cart_inventory",sizeof(schema_config.cart_db));
	safestrncpy(schema_config.inventory_db,"inventory",sizeof(schema_config.inventory_db));
	safestrncpy(schema_config.charlog_db,"charlog",sizeof(schema_config.charlog_db));
	safestrncpy(schema_config.storage_db,"storage",sizeof(schema_config.storage_db));
	safestrncpy(schema_config.interlog_db,"interlog",sizeof(schema_config.interlog_db));
	safestrncpy(schema_config.reg_db,"global_reg_value",sizeof(schema_config.reg_db));
	safestrncpy(schema_config.skill_db,"skill",sizeof(schema_config.skill_db));
	safestrncpy(schema_config.memo_db,"memo",sizeof(schema_config.memo_db));
	safestrncpy(schema_config.guild_db,"guild",sizeof(schema_config.guild_db));
	safestrncpy(schema_config.guild_alliance_db,"guild_alliance",sizeof(schema_config.guild_alliance_db));
	safestrncpy(schema_config.guild_castle_db,"guild_castle",sizeof(schema_config.guild_castle_db));
	safestrncpy(schema_config.guild_expulsion_db,"guild_expulsion",sizeof(schema_config.guild_expulsion_db));
	safestrncpy(schema_config.guild_member_db,"guild_member",sizeof(schema_config.guild_member_db));
	safestrncpy(schema_config.guild_position_db,"guild_position",sizeof(schema_config.guild_position_db));
	safestrncpy(schema_config.guild_skill_db,"guild_skill",sizeof(schema_config.guild_skill_db));
	safestrncpy(schema_config.guild_storage_db,"guild_storage",sizeof(schema_config.guild_storage_db));
	safestrncpy(schema_config.party_db,"party",sizeof(schema_config.party_db));
	safestrncpy(schema_config.pet_db,"pet",sizeof(schema_config.pet_db));
	safestrncpy(schema_config.mail_db,"mail",sizeof(schema_config.mail_db)); // MAIL SYSTEM
	safestrncpy(schema_config.auction_db,"auction",sizeof(schema_config.auction_db)); // Auctions System
	safestrncpy(schema_config.friend_db,"friends",sizeof(schema_config.friend_db));
	safestrncpy(schema_config.hotkey_db,"hotkey",sizeof(schema_config.hotkey_db));
	safestrncpy(schema_config.quest_db,"quest",sizeof(schema_config.quest_db));
	safestrncpy(schema_config.homunculus_db,"homunculus",sizeof(schema_config.homunculus_db));
	safestrncpy(schema_config.skill_homunculus_db,"skill_homunculus",sizeof(schema_config.skill_homunculus_db));
	safestrncpy(schema_config.mercenary_db,"mercenary",sizeof(schema_config.mercenary_db));
	safestrncpy(schema_config.mercenary_owner_db,"mercenary_owner",sizeof(schema_config.mercenary_owner_db));
	safestrncpy(schema_config.ragsrvinfo_db,"ragsrvinfo",sizeof(schema_config.ragsrvinfo_db));
	safestrncpy(schema_config.skillcooldown_db,"skillcooldown",sizeof(schema_config.skillcooldown_db));
	safestrncpy(schema_config.bonus_script_db,"bonus_script",sizeof(schema_config.bonus_script_db));
	// eAmod
	safestrncpy(schema_config.rentstorage_db,"rentstorage",sizeof(schema_config.rentstorage_db));
	safestrncpy(schema_config.achievement_db,"achievement",sizeof(schema_config.achievement_db));
}

//set default config
void char_set_defaults(){
	int i;
	charserv_config.pincode_config.pincode_enabled = true;
	charserv_config.pincode_config.pincode_changetime = 0;
	charserv_config.pincode_config.pincode_maxtry = 3;
	charserv_config.pincode_config.pincode_force = true;

	charserv_config.charmove_config.char_move_enabled = true;
	charserv_config.charmove_config.char_movetoused = true;
	charserv_config.charmove_config.char_moves_unlimited = false;

	charserv_config.char_config.char_per_account = 0; //Maximum chars per account (default unlimited) [Sirius]
	charserv_config.char_config.char_del_level = 0; //From which level u can delete character [Lupus]
	charserv_config.char_config.char_del_delay = 86400;
#if PACKETVER >= 20100803
	charserv_config.char_config.char_del_option = 2;
#else
	charserv_config.char_config.char_del_option = 1;
#endif

//	charserv_config.userid[24];
//	charserv_config.passwd[24];
//	charserv_config.server_name[20];
	safestrncpy(charserv_config.wisp_server_name,"Server",sizeof(charserv_config.wisp_server_name));
//	charserv_config.login_ip_str[128];
	charserv_config.login_ip = 0;
	charserv_config.login_port = 6900;
//	charserv_config.char_ip_str[128];
	charserv_config.char_ip = 0;
//	charserv_config.bind_ip_str[128];
	charserv_config.bind_ip = INADDR_ANY;
	charserv_config.char_port = 6121;
	charserv_config.char_maintenance = 0;
	charserv_config.char_new = true;
	charserv_config.char_new_display = 0;

	charserv_config.char_config.name_ignoring_case = false; // Allow or not identical name for characters but with a different case by [Yor]
	charserv_config.char_config.char_name_option = 0; // Option to know which letters/symbols are authorised in the name of a character (0: all, 1: only those in char_name_letters, 2: all EXCEPT those in char_name_letters) by [Yor]
	safestrncpy(charserv_config.char_config.unknown_char_name,"Unknown",sizeof(charserv_config.char_config.unknown_char_name)); // Name to use when the requested name cannot be determined
	safestrncpy(charserv_config.char_config.char_name_letters,"",sizeof(charserv_config.char_config.char_name_letters)); // list of letters/symbols allowed (or not) in a character name. by [Yor]

	charserv_config.save_log = 1; // show loading/saving messages
	charserv_config.log_char = 1;	// loggin char or not [devil]
	charserv_config.log_inter = 1;	// loggin inter or not [devil]
	charserv_config.char_check_db =1;

	charserv_config.start_point.map = mapindex_name2id("new_zone01"); //mapindex_name2id(MAP_DEFAULT);
	charserv_config.start_point.x = 53; //MAP_DEFAULT_X
	charserv_config.start_point.y = 111; //MAP_DEFAULT_Y
	charserv_config.console = 0;
	charserv_config.max_connect_user = -1;
	charserv_config.gm_allow_group = -1;
	charserv_config.autosave_interval = DEFAULT_AUTOSAVE_INTERVAL;
	charserv_config.start_zeny = 0;
	charserv_config.guild_exp_rate = 100;

	// eAmod
	charserv_config.guild_base_members = 16;
	charserv_config.guild_add_members = 6;
	for( i = 0; i < 3; i++ )
	{
		charserv_config.bg_regular_rewards[i] = 0;
		charserv_config.bg_ranked_rewards[i] = 0;
	}
}

bool char_config_read(const char* cfgName, bool normal){
	char line[1024], w1[1024], w2[1024];
	FILE* fp = fopen(cfgName, "r");

	if (fp == NULL) {
		ShowError("Configuration file not found: %s.\n", cfgName);
		return false;
	}

	while(fgets(line, sizeof(line), fp)) {
		if (line[0] == '/' && line[1] == '/')
			continue;

		if (sscanf(line, "%1023[^:]: %1023[^\r\n]", w1, w2) != 2)
			continue;

		remove_control_chars(w1);
		remove_control_chars(w2);

		// Config that loaded only when server started, not by reloading config file
		if (normal) {
			if (strcmpi(w1, "userid") == 0) {
				safestrncpy(charserv_config.userid, w2, sizeof(charserv_config.userid));
			} else if (strcmpi(w1, "passwd") == 0) {
				safestrncpy(charserv_config.passwd, w2, sizeof(charserv_config.passwd));
			} else if (strcmpi(w1, "server_name") == 0) {
				safestrncpy(charserv_config.server_name, w2, sizeof(charserv_config.server_name));
			} else if (strcmpi(w1, "wisp_server_name") == 0) {
				if (strlen(w2) >= 4) {
					safestrncpy(charserv_config.wisp_server_name, w2, sizeof(charserv_config.wisp_server_name));
				}
			} else if (strcmpi(w1, "login_ip") == 0) {
				charserv_config.login_ip = host2ip(w2);
				if (charserv_config.login_ip) {
					char ip_str[16];
					safestrncpy(charserv_config.login_ip_str, w2, sizeof(charserv_config.login_ip_str));
					ShowStatus("Login server IP address : %s -> %s\n", w2, ip2str(charserv_config.login_ip, ip_str));
				}
			} else if (strcmpi(w1, "login_port") == 0) {
				charserv_config.login_port = atoi(w2);
			} else if (strcmpi(w1, "char_ip") == 0) {
				charserv_config.char_ip = host2ip(w2);
				if (charserv_config.char_ip) {
					char ip_str[16];
					safestrncpy(charserv_config.char_ip_str, w2, sizeof(charserv_config.char_ip_str));
					ShowStatus("Character server IP address : %s -> %s\n", w2, ip2str(charserv_config.char_ip, ip_str));
				}
			} else if (strcmpi(w1, "bind_ip") == 0) {
				charserv_config.bind_ip = host2ip(w2);
				if (charserv_config.bind_ip) {
					char ip_str[16];
					safestrncpy(charserv_config.bind_ip_str, w2, sizeof(charserv_config.bind_ip_str));
					ShowStatus("Character server binding IP address : %s -> %s\n", w2, ip2str(charserv_config.bind_ip, ip_str));
				}
			} else if (strcmpi(w1, "char_port") == 0) {
				charserv_config.char_port = atoi(w2);
			} else if (strcmpi(w1, "console") == 0) {
				charserv_config.console = config_switch(w2);
			}
		}

		if(strcmpi(w1,"timestamp_format") == 0) {
			safestrncpy(timestamp_format, w2, sizeof(timestamp_format));
		} else if(strcmpi(w1,"console_silent")==0){
			msg_silent = atoi(w2);
			if( msg_silent ) /* only bother if its actually enabled */
				ShowInfo("Console Silent Setting: %d\n", atoi(w2));
		} else if(strcmpi(w1,"stdout_with_ansisequence")==0){
			stdout_with_ansisequence = config_switch(w2);
		} else if (strcmpi(w1, "char_maintenance") == 0) {
			charserv_config.char_maintenance = atoi(w2);
		} else if (strcmpi(w1, "char_new") == 0) {
			charserv_config.char_new = (bool)atoi(w2);
		} else if (strcmpi(w1, "char_new_display") == 0) {
			charserv_config.char_new_display = atoi(w2);
		} else if (strcmpi(w1, "max_connect_user") == 0) {
			charserv_config.max_connect_user = atoi(w2);
			if (charserv_config.max_connect_user < -1)
				charserv_config.max_connect_user = -1;
		} else if(strcmpi(w1, "gm_allow_group") == 0) {
			charserv_config.gm_allow_group = atoi(w2);
		} else if (strcmpi(w1, "autosave_time") == 0) {
			charserv_config.autosave_interval = atoi(w2)*1000;
			if (charserv_config.autosave_interval <= 0)
				charserv_config.autosave_interval = DEFAULT_AUTOSAVE_INTERVAL;
		} else if (strcmpi(w1, "save_log") == 0) {
			charserv_config.save_log = config_switch(w2);
		} else if (strcmpi(w1, "start_point") == 0) {
			char map[MAP_NAME_LENGTH_EXT];
			int x, y;
			if (sscanf(w2, "%15[^,],%d,%d", map, &x, &y) < 3)
				continue;
			charserv_config.start_point.map = mapindex_name2id(map);
			if (!charserv_config.start_point.map)
				ShowError("Specified start_point %s not found in map-index cache.\n", map);
			charserv_config.start_point.x = x;
			charserv_config.start_point.y = y;
		} else if (strcmpi(w1, "start_zeny") == 0) {
			charserv_config.start_zeny = atoi(w2);
			if (charserv_config.start_zeny < 0)
				charserv_config.start_zeny = 0;
		} else if (strcmpi(w1, "start_items") == 0) {
			int i=0;
			char *lineitem, **fields;
			int fields_length = 3+1;
			fields = (char**)aMalloc(fields_length*sizeof(char*));

			lineitem = strtok(w2, ":");
			while (lineitem != NULL) {
				int n = sv_split(lineitem, strlen(lineitem), 0, ',', fields, fields_length, SV_NOESCAPE_NOTERMINATE);
				if(n+1 < fields_length){
					ShowDebug("start_items: not enough arguments for %s! Skipping...\n",lineitem);
					lineitem = strtok(NULL, ":"); //next itemline
					continue;
				}
				if(i > MAX_STARTITEM){
					ShowDebug("start_items: too many items, only %d are allowed! Ignoring parameter %s...\n",MAX_STARTITEM,lineitem);
				} else {
					start_items[i].nameid = max(0,atoi(fields[1]));
					start_items[i].amount = max(0,atoi(fields[2]));
					start_items[i].pos = max(0,atoi(fields[3]));
				}
				lineitem = strtok(NULL, ":"); //next itemline
				i++;
			}
			aFree(fields);
		} else if(strcmpi(w1,"log_char")==0) {		//log char or not [devil]
			charserv_config.log_char = atoi(w2);

		} else if (strcmpi(w1, "guild_base_members") == 0) {
			charserv_config.guild_base_members = atoi(w2);
		} else if (strcmpi(w1, "guild_add_members") == 0) {
			charserv_config.guild_add_members = atoi(w2);

		} else if (strcmpi(w1, "unknown_char_name") == 0) {
			safestrncpy(charserv_config.char_config.unknown_char_name, w2, sizeof(charserv_config.char_config.unknown_char_name));
			charserv_config.char_config.unknown_char_name[NAME_LENGTH-1] = '\0';
		} else if (strcmpi(w1, "name_ignoring_case") == 0) {
			charserv_config.char_config.name_ignoring_case = (bool)config_switch(w2);
		} else if (strcmpi(w1, "char_name_option") == 0) {
			charserv_config.char_config.char_name_option = atoi(w2);
		} else if (strcmpi(w1, "char_name_letters") == 0) {
			safestrncpy(charserv_config.char_config.char_name_letters, w2, sizeof(charserv_config.char_config.char_name_letters));
		} else if (strcmpi(w1, "char_del_level") == 0) { //disable/enable char deletion by its level condition [Lupus]
			charserv_config.char_config.char_del_level = atoi(w2);
		} else if (strcmpi(w1, "char_del_delay") == 0) {
			charserv_config.char_config.char_del_delay = atoi(w2);
		} else if (strcmpi(w1, "char_del_option") == 0) {
			charserv_config.char_config.char_del_option = atoi(w2);
		} else if(strcmpi(w1,"db_path")==0) {
			safestrncpy(schema_config.db_path, w2, sizeof(schema_config.db_path));
		} else if (strcmpi(w1, "fame_list_alchemist") == 0) {
			fame_list_size_chemist = atoi(w2);
			if (fame_list_size_chemist > MAX_FAME_LIST) {
				ShowWarning("Max fame list size is %d (fame_list_alchemist)\n", MAX_FAME_LIST);
				fame_list_size_chemist = MAX_FAME_LIST;
			}
		} else if (strcmpi(w1, "fame_list_blacksmith") == 0) {
			fame_list_size_smith = atoi(w2);
			if (fame_list_size_smith > MAX_FAME_LIST) {
				ShowWarning("Max fame list size is %d (fame_list_blacksmith)\n", MAX_FAME_LIST);
				fame_list_size_smith = MAX_FAME_LIST;
			}
		} else if (strcmpi(w1, "fame_list_taekwon") == 0) {
			fame_list_size_taekwon = atoi(w2);
			if (fame_list_size_taekwon > MAX_FAME_LIST) {
				ShowWarning("Max fame list size is %d (fame_list_taekwon)\n", MAX_FAME_LIST);
				fame_list_size_taekwon = MAX_FAME_LIST;
			}

		} else if (strcmpi(w1, "bg_regular_rewards") == 0 ) {
			int i;
			memset(charserv_config.bg_regular_rewards, 0, sizeof(charserv_config.bg_regular_rewards));
			sscanf(w2, "%d,%d,%d", &charserv_config.bg_regular_rewards[0], &charserv_config.bg_regular_rewards[1], &charserv_config.bg_regular_rewards[2]);
			for( i = 0; i < 3; i++ )
				if( charserv_config.bg_regular_rewards[i] < 0 ) charserv_config.bg_regular_rewards[i] = 0;
		} else if (strcmpi(w1, "bg_ranked_rewards") == 0 ) {
			int i;
			memset(charserv_config.bg_ranked_rewards, 0, sizeof(charserv_config.bg_ranked_rewards));
			sscanf(w2, "%d,%d,%d", &charserv_config.bg_ranked_rewards[0], &charserv_config.bg_ranked_rewards[1], &charserv_config.bg_ranked_rewards[2]);
			for( i = 0; i < 3; i++ )
				if( charserv_config.bg_ranked_rewards[i] < 0 ) charserv_config.bg_ranked_rewards[i] = 0;

		} else if (strcmpi(w1, "guild_exp_rate") == 0) {
			charserv_config.guild_exp_rate = atoi(w2);
		} else if (strcmpi(w1, "pincode_enabled") == 0) {
			charserv_config.pincode_config.pincode_enabled = config_switch(w2);
#if PACKETVER < 20110309
			if( charserv_config.pincode_config.pincode_enabled ) {
				ShowWarning("pincode_enabled requires PACKETVER 20110309 or higher. Disabling...\n");
				charserv_config.pincode_config.pincode_enabled = false;
			}
#endif
		} else if (strcmpi(w1, "pincode_changetime") == 0) {
			charserv_config.pincode_config.pincode_changetime = atoi(w2)*60*60*24;
		} else if (strcmpi(w1, "pincode_maxtry") == 0) {
			charserv_config.pincode_config.pincode_maxtry = atoi(w2);
		} else if (strcmpi(w1, "pincode_force") == 0) {
			charserv_config.pincode_config.pincode_force = config_switch(w2);
		} else if (strcmpi(w1, "char_move_enabled") == 0) {
			charserv_config.charmove_config.char_move_enabled = config_switch(w2);
		} else if (strcmpi(w1, "char_movetoused") == 0) {
			charserv_config.charmove_config.char_movetoused = config_switch(w2);
		} else if (strcmpi(w1, "char_moves_unlimited") == 0) {
			charserv_config.charmove_config.char_moves_unlimited = config_switch(w2);
		} else if (strcmpi(w1, "char_checkdb") == 0) {
			charserv_config.char_check_db = config_switch(w2);
		} else if (strcmpi(w1, "import") == 0) {
			char_config_read(w2, normal);
		}
	}
	fclose(fp);

	ShowInfo("Done reading %s.\n", cfgName);
	return true;
}


/*
 * Message conf function
 */
int char_msg_config_read(char *cfgName){
	return _msg_config_read(cfgName,CHAR_MAX_MSG,msg_table);
}
const char* char_msg_txt(int msg_number){
	return _msg_txt(msg_number,CHAR_MAX_MSG,msg_table);
}
void char_do_final_msg(void){
	_do_final_msg(CHAR_MAX_MSG,msg_table);
}


void do_final(void)
{
	ShowStatus("Terminating...\n");

	char_set_all_offline(-1);
	char_set_all_offline_sql();

	inter_final();

	flush_fifos();

	do_final_msg();
	do_final_chmapif();
	do_final_chlogif();

	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s`", schema_config.ragsrvinfo_db) )
		Sql_ShowDebug(sql_handle);

	char_db_->destroy(char_db_, NULL);
	online_char_db->destroy(online_char_db, NULL);
	auth_db->destroy(auth_db, NULL);

	if( char_fd != -1 )
	{
		do_close(char_fd);
		char_fd = -1;
	}

	Sql_Free(sql_handle);
	mapindex_final();

	ShowStatus("Finished.\n");
}


void set_server_type(void){
	SERVER_TYPE = ATHENA_SERVER_CHAR;
}

//------------------------------
// Function called when the server
// has received a crash signal.
//------------------------------
void do_abort(void)
{
}

/// Called when a terminate signal is received.
void do_shutdown(void) {
	if( runflag != CHARSERVER_ST_SHUTDOWN )
	{
		int id;
		runflag = CHARSERVER_ST_SHUTDOWN;
		ShowStatus("Shutting down...\n");
		// TODO proper shutdown procedure; wait for acks?, kick all characters, ... [FlavoJS]
		for( id = 0; id < ARRAYLENGTH(map_server); ++id )
			chmapif_server_reset(id);
		chlogif_check_shutdown();
		flush_fifos();
		runflag = CORE_ST_STOP;
	}
}


int do_init(int argc, char **argv)
{
	//Read map indexes
	runflag = CHARSERVER_ST_STARTING;
	mapindex_init();

	CHAR_CONF_NAME =   "conf/char_athena.conf";
	LAN_CONF_NAME =    "conf/subnet_athena.conf";
	SQL_CONF_NAME =    "conf/inter_athena.conf";
	MSG_CONF_NAME_EN = "conf/msg_conf/char_msg.conf";

	cli_get_options(argc,argv);

	char_set_defaults();
	char_config_read(CHAR_CONF_NAME, true);
	char_lan_config_read(LAN_CONF_NAME);
	char_set_default_sql();
	char_sql_config_read(SQL_CONF_NAME);
	msg_config_read(MSG_CONF_NAME_EN);

	if (strcmp(charserv_config.userid, "s1")==0 && strcmp(charserv_config.passwd, "p1")==0) {
		ShowWarning("Using the default user/password s1/p1 is NOT RECOMMENDED.\n");
		ShowNotice("Please edit your 'login' table to create a proper inter-server user/password (gender 'S')\n");
		ShowNotice("And then change the user/password to use in conf/char_athena.conf (or conf/import/char_conf.txt)\n");
	}

	inter_init_sql((argc > 2) ? argv[2] : inter_cfgName); // inter server configuration

	auth_db = idb_alloc(DB_OPT_RELEASE_DATA);
	online_char_db = idb_alloc(DB_OPT_RELEASE_DATA);
	char_mmo_sql_init();
	char_read_fame_list(); //Read fame lists.

	if ((naddr_ != 0) && (!(charserv_config.login_ip) || !(charserv_config.char_ip) ))
	{
		char ip_str[16];
		ip2str(addr_[0], ip_str);

		if (naddr_ > 1)
			ShowStatus("Multiple interfaces detected..  using %s as our IP address\n", ip_str);
		else
			ShowStatus("Defaulting to %s as our IP address\n", ip_str);
		if (!(charserv_config.login_ip) ) {
			safestrncpy(charserv_config.login_ip_str, ip_str, sizeof(charserv_config.login_ip_str));
			charserv_config.login_ip = str2ip(charserv_config.login_ip_str);
		}
		if (!(charserv_config.char_ip)) {
			safestrncpy(charserv_config.char_ip_str, ip_str, sizeof(charserv_config.char_ip_str));
			charserv_config.char_ip = str2ip(charserv_config.char_ip_str);
		}
	}

	do_init_chlogif();
	do_init_chmapif();

	// periodically update the overall user count on all mapservers + login server
	add_timer_func_list(chlogif_broadcast_user_count, "broadcast_user_count");
	add_timer_interval(gettick() + 1000, chlogif_broadcast_user_count, 0, 0, 5 * 1000);

	// Timer to clear (online_char_db)
	add_timer_func_list(char_chardb_waiting_disconnect, "chardb_waiting_disconnect");

	// Online Data timers (checking if char still connected)
	add_timer_func_list(char_online_data_cleanup, "online_data_cleanup");
	add_timer_interval(gettick() + 1000, char_online_data_cleanup, 0, 0, 600 * 1000);

	//chek db tables
	if(charserv_config.char_check_db && char_checkdb() == 0){
		ShowFatalError("char : A tables is missing in sql-server, please fix it, see (sql-files main.sql for structure) \n");
		exit(EXIT_FAILURE);
	}
	//Cleaning the tables for NULL entrys @ startup [Sirius]
	//Chardb clean
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `account_id` = '0'", schema_config.char_db) )
		Sql_ShowDebug(sql_handle);

	//guilddb clean
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `guild_lv` = '0' AND `max_member` = '0' AND `exp` = '0' AND `next_exp` = '0' AND `average_lv` = '0'", schema_config.guild_db) )
		Sql_ShowDebug(sql_handle);

	//guildmemberdb clean
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `%s` WHERE `guild_id` = '0' AND `account_id` = '0' AND `char_id` = '0'", schema_config.guild_member_db) )
		Sql_ShowDebug(sql_handle);

	set_defaultparse(chclif_parse);

	if( (char_fd = make_listen_bind(charserv_config.bind_ip,charserv_config.char_port)) == -1 ) {
		ShowFatalError("Failed to bind to port '"CL_WHITE"%d"CL_RESET"'\n",charserv_config.char_port);
		exit(EXIT_FAILURE);
	}

	if( runflag != CORE_ST_STOP )
	{
		shutdown_callback = do_shutdown;
		runflag = CHARSERVER_ST_RUNNING;
	}

	do_init_chcnslif();

	ShowStatus("The char-server is "CL_GREEN"ready"CL_RESET" (Server is listening on the port %d).\n\n", charserv_config.char_port);

	return 0;
}