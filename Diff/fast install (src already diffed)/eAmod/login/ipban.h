// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef __IPBAN_H_INCLUDED__
#define __IPBAN_H_INCLUDED__

#include "../common/cbasetypes.h"

// initialize
void ipban_init(void);

// finalize
void ipban_final(void);

// check ip against ban list
bool ipban_check(uint32 ip);

// increases failure count for the specified IP
void ipban_log(uint32 ip);
void ipban_log_as_grf_integrity(uint32 ip); //isaac

// parses configuration option
bool ipban_config_read(const char* key, const char* value);

//CryptoGuardian by AsiaGenius
void CryptoGuard_Update_HWID(int account_id, char *unique_id);
bool CheckLastUnique(int account_id, char *unique_id);
void CryptoGuard_Update_Atual(int account_id, char *unique_id);
int Crypto_Check_Ban(int account_id, char *unique_id);
void CryptoGuard_MakeBAN(int account_id, char *unique_id, char *timedate, char *reason);
void Crypto_flag(int flag, char *unique_id);
//CryptoGuardian by AsiaGenius

#endif // __IPBAN_H_INCLUDED__
