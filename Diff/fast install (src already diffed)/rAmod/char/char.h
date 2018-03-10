// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _CHAR_SQL_H_
#define _CHAR_SQL_H_

#define DB_NAME_LEN 256 //max len of dbs

#include "../config/core.h"
#include "../common/core.h" // CORE_ST_LAST
#include "../common/msg_conf.h"
#include "../common/mmo.h"


extern int login_fd; //login file descriptor
extern int char_fd; //char file descriptor

enum E_CHARSERVER_ST {
	CHARSERVER_ST_RUNNING = CORE_ST_LAST,
	CHARSERVER_ST_STARTING,
	CHARSERVER_ST_SHUTDOWN,
	CHARSERVER_ST_LAST
};

enum {
	TABLE_INVENTORY,
	TABLE_CART,
	TABLE_STORAGE,
	TABLE_GUILD_STORAGE,
	TABLE_EXT_STORAGE,
};

struct Schema_Config {
	int db_use_sqldbs;
	char db_path[1024];
	char char_db[DB_NAME_LEN];
	char scdata_db[DB_NAME_LEN];
	char skillcooldown_db[DB_NAME_LEN];
	char cart_db[DB_NAME_LEN];
	char inventory_db[DB_NAME_LEN];
	char charlog_db[DB_NAME_LEN];
	char storage_db[DB_NAME_LEN];
	char interlog_db[DB_NAME_LEN];
	char reg_db[DB_NAME_LEN];
	char skill_db[DB_NAME_LEN];
	char memo_db[DB_NAME_LEN];
	char guild_db[DB_NAME_LEN];
	char guild_alliance_db[DB_NAME_LEN];
	char guild_castle_db[DB_NAME_LEN];
	char guild_expulsion_db[DB_NAME_LEN];
	char guild_member_db[DB_NAME_LEN];
	char guild_position_db[DB_NAME_LEN];
	char guild_skill_db[DB_NAME_LEN];
	char guild_storage_db[DB_NAME_LEN];
	char party_db[DB_NAME_LEN];
	char pet_db[DB_NAME_LEN];
	char mail_db[DB_NAME_LEN]; // MAIL SYSTEM
	char auction_db[DB_NAME_LEN]; // Auctions System
	char friend_db[DB_NAME_LEN];
	char hotkey_db[DB_NAME_LEN];
	char quest_db[DB_NAME_LEN];
	char homunculus_db[DB_NAME_LEN];
	char skill_homunculus_db[DB_NAME_LEN];
	char mercenary_db[DB_NAME_LEN];
	char mercenary_owner_db[DB_NAME_LEN];
	char ragsrvinfo_db[DB_NAME_LEN];
	char elemental_db[DB_NAME_LEN];
	char bonus_script_db[DB_NAME_LEN];
	char rentstorage_db[DB_NAME_LEN];
	char achievement_db[DB_NAME_LEN];
};
extern struct Schema_Config schema_config;

/// Pincode system
enum pincode_state {
	PINCODE_OK		= 0,
	PINCODE_ASK		= 1,
	PINCODE_NOTSET	= 2,
	PINCODE_EXPIRED	= 3,
	PINCODE_NEW		= 4,
	PINCODE_PASSED	= 7,
	PINCODE_WRONG	= 8,
	PINCODE_MAXSTATE
};
struct Pincode_Config {
	bool pincode_enabled;
	int pincode_changetime;
	int pincode_maxtry;
	bool pincode_force;
};
struct CharMove_Config {
	bool char_move_enabled;
	bool char_movetoused;
	bool char_moves_unlimited;
};
struct Char_Config {
	int char_per_account; //Maximum chars per account (default unlimited) [Sirius]
	int char_del_level; //From which level u can delete character [Lupus]
	int char_del_delay; //minimum delay before effectly do the deletion
	bool name_ignoring_case; // Allow or not identical name for characters but with a different case by [Yor]
	char unknown_char_name[NAME_LENGTH]; // Name to use when the requested name cannot be determined
	char char_name_letters[1024]; // list of letters/symbols allowed (or not) in a character name. by [Yor]
	int char_name_option; // Option to know which letters/symbols are authorised in the name of a character (0: all, 1: only those in char_name_letters, 2: all EXCEPT those in char_name_letters) by [Yor]
	int char_del_option;	// Character deletion type, email = 1, birthdate = 2 (default)
};

#define TRIM_CHARS "\255\xA0\032\t\x0A\x0D " //The following characters are trimmed regardless because they cause confusion and problems on the servers. [Skotlex]
struct CharServ_Config {
	char userid[24];
	char passwd[24];
	char server_name[20];
	char wisp_server_name[NAME_LENGTH];
	char login_ip_str[128];
	uint32 login_ip;
	uint16 login_port;
	char char_ip_str[128];
	uint32 char_ip;
	char bind_ip_str[128];
	uint32 bind_ip;
	uint16 char_port;
	int char_maintenance;
	bool char_new;
	int char_new_display;

	struct CharMove_Config charmove_config;
	struct Char_Config char_config;
	struct Pincode_Config pincode_config;

	int save_log; // show loading/saving messages
	int log_char;	// loggin char or not [devil]
	int log_inter;	// loggin inter or not [devil]
	int char_check_db;	///cheking sql-table at begining ?

	struct point start_point; // Initial position the player will spawn on server
	int console;
	int max_connect_user;
	int gm_allow_group;
	int autosave_interval;
	int start_zeny;
	int guild_exp_rate;

	// eAmod
	int guild_base_members;
	int guild_add_members;
	int bg_regular_rewards[3];
	int bg_ranked_rewards[3];
};
extern struct CharServ_Config charserv_config;

#define MAX_MAP_SERVERS 30 //how many mapserver a char server can handle
struct mmo_map_server {
	int fd;
	uint32 ip;
	uint16 port;
	int users;
	unsigned short map[MAX_MAP_PER_SERVER];
};
extern struct mmo_map_server map_server[MAX_MAP_SERVERS];

#define AUTH_TIMEOUT 30000
struct auth_node {
	int account_id;
	int char_id;
	uint32 login_id1;
	uint32 login_id2;
	uint32 ip;
	int sex;
	time_t expiration_time; // # of seconds 1/1/1970 (timestamp): Validity limit of the account (0 = unlimited)
	int group_id;
	unsigned changing_mapservers : 1;
	uint8 version;
};
DBMap* char_get_authdb(); // int account_id -> struct auth_node*

struct online_char_data {
	int account_id;
	int char_id;
	int fd;
	int waiting_disconnect;
	short server; // -2: unknown server, -1: not connected, 0+: id of server
	bool pincode_success;
};
DBMap* char_get_onlinedb(); // int account_id -> struct online_char_data*

struct char_session_data {
	bool auth; // whether the session is authed or not
	int account_id, login_id1, login_id2, sex;
	int found_char[MAX_CHARS]; // ids of chars on this account
	char email[40]; // e-mail (default: a@a.com) by [Yor]
	time_t expiration_time; // # of seconds 1/1/1970 (timestamp): Validity limit of the account (0 = unlimited)
	int group_id; // permission
	uint8 char_slots; // total number of characters that can be created
	uint8 chars_vip;
	uint8 chars_billing;
	uint32 version;
	uint8 clienttype;
	char new_name[NAME_LENGTH];
	char birthdate[10+1];  // YYYY-MM-DD
	// Pincode system
	char pincode[PINCODE_LENGTH+1];
	uint32 pincode_seed;
	time_t pincode_change;
	uint16 pincode_try;
	// Addon system
	int bank_vault;
	unsigned int char_moves[MAX_CHARS]; // character moves left
	uint8 isvip;
	time_t unban_time[MAX_CHARS];
	int charblock_timer;
};


struct mmo_charstatus;
DBMap* char_get_chardb(); // int char_id -> struct mmo_charstatus*

//Custom limits for the fame lists. [Skotlex]
extern int fame_list_size_chemist;
extern int fame_list_size_smith;
extern int fame_list_size_taekwon;
// eAmod
extern int fame_list_size_pvprank;
extern int fame_list_size_bgrank;
extern int fame_list_size_bg;

// Char-server-side stored fame lists [DracoRPG]
extern struct fame_list smith_fame_list[MAX_FAME_LIST];
extern struct fame_list chemist_fame_list[MAX_FAME_LIST];
extern struct fame_list taekwon_fame_list[MAX_FAME_LIST];
// eAmod
extern struct fame_list pvprank_fame_list[MAX_FAME_LIST];
extern struct fame_list bgrank_fame_list[MAX_FAME_LIST];
extern struct fame_list bg_fame_list[MAX_FAME_LIST];

#define DEFAULT_AUTOSAVE_INTERVAL 300*1000
#define MAX_CHAR_BUF 144 //Max size (for WFIFOHEAD calls)

int char_search_mapserver(unsigned short map, uint32 ip, uint16 port);
int char_lan_subnetcheck(uint32 ip);

int char_count_users(void);
DBData char_create_online_data(DBKey key, va_list args);
int char_db_setoffline(DBKey key, DBData *data, va_list ap);
void char_set_char_online(int map_id, int char_id, int account_id);
void char_set_char_offline(int char_id, int account_id);
void char_set_all_offline(int id);
void char_disconnect_player(int account_id);
int char_chardb_waiting_disconnect(int tid, unsigned int tick, int id, intptr_t data);

int char_mmo_char_tobuf(uint8* buffer, struct mmo_charstatus* p);
int char_mmo_char_tosql(int char_id, struct mmo_charstatus* p);
int char_mmo_char_fromsql(int char_id, struct mmo_charstatus* p, bool load_everything);
int char_mmo_chars_fromsql(struct char_session_data* sd, uint8* buf);
int char_delete_char_sql(int char_id);
int char_rename_char_sql(struct char_session_data *sd, int char_id);
int char_divorce_char_sql(int partner_id1, int partner_id2);
int char_ranking_reset(int type); // eAmod Codes
int char_item_remove4all(int nameid); // eAmod Codes
int char_dump2sql(int char_id); // eAmod Codes
void char_ip_premium(uint32 ip, struct mmo_charstatus* p); // eAmod Codes
int char_memitemdata_to_sql(const struct item items[], int max, int id, int tableswitch);

void disconnect_player(int account_id);

int char_married(int pl1,int pl2);
int char_child(int parent_id, int child_id);
int char_family(int pl1,int pl2,int pl3);

int char_request_accreg2(int account_id, int char_id);
int char_save_accreg2(unsigned char* buf, int len);

//extern bool char_gm_read;
int char_loadName(int char_id, char* name);
int char_check_char_name(char * name, char * esc_name);

void char_pincode_decrypt( uint32 userSeed, char* pin );
int char_pincode_compare( int fd, struct char_session_data* sd, char* pin );
void char_auth_ok(int fd, struct char_session_data *sd);
void char_set_charselect(int account_id);
void char_read_fame_list(void);

#if PACKETVER >= 20120307
int char_make_new_char_sql(struct char_session_data* sd, char* name_, int slot, int hair_color, int hair_style);
#else
int char_make_new_char_sql(struct char_session_data* sd, char* name_, int str, int agi, int vit, int int_, int dex, int luk, int slot, int hair_color, int hair_style);
#endif

//For use in packets that depend on an sd being present [Skotlex]
#define FIFOSD_CHECK(rest) { if(RFIFOREST(fd) < rest) return 0; if (sd==NULL || !sd->auth) { RFIFOSKIP(fd,rest); return 0; } }

#define msg_config_read(cfgName) char_msg_config_read(cfgName)
#define msg_txt(msg_number) char_msg_txt(msg_number)
#define do_final_msg() char_do_final_msg()
int char_msg_config_read(char *cfgName);
const char* char_msg_txt(int msg_number);
void char_do_final_msg(void);
bool char_config_read(const char* cfgName, bool normal);


#endif /* _CHAR_SQL_H_ */
