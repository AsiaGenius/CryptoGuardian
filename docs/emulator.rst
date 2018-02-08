========
Diff CryptoGuardian
========

rAthena
====

Source DIFF
-------------

Open ``src\common\showmsg.h``

after: 

.. code-block:: bash

    extern char timestamp_format[20];

Replace:

.. code-block:: bash

    enum msg_type {
        MSG_NONE,
        MSG_STATUS,
        MSG_SQL,
        MSG_INFORMATION,
        MSG_NOTICE,
        MSG_WARNING,
        MSG_DEBUG,
        MSG_ERROR,
        MSG_FATALERROR
    };

to:

.. code-block:: bash
    
    enum msg_type {
        MSG_NONE,
        MSG_STATUS,
        MSG_SQL,
        MSG_INFORMATION,
        MSG_NOTICE,
        MSG_WARNING,
        MSG_DEBUG,
        MSG_ERROR,
        MSG_FATALERROR,
        MSG_CryptoGuard
    };


replace:

.. code-block:: bash

    extern void ClearScreen(void);

to:

.. code-block:: bash

    //RingSec by Gary
    extern void CryptoGuard(const char *string, ...);
    //RingSec by Gary
    extern void ClearScreen(void);


Open ``src\common\showmsg.c``

After:

.. code-block:: bash

    void ShowStatus(const char *string, ...) {
        va_list ap;
        va_start(ap, string);
        _vShowMessage(MSG_STATUS, string, ap);
        va_end(ap);
    }

Add:

.. code-block:: bash

    //RingSec by Gary
    void CryptoGuard(const char *string, ...) {
        va_list ap;
        va_start(ap, string);
        _vShowMessage(MSG_CryptoGuard, string, ap);
        va_end(ap);
    }
    //RingSec by Gary



After:

.. code-block:: bash

    case MSG_STATUS: //Bright Green (To inform about good things)
        strcat(prefix,CL_GREEN"[Status]"CL_RESET":");
        break;
			
			
add this:

.. code-block:: bash

    //RingSec by Gary
	case MSG_CryptoGuard:
        strcat(prefix, CL_BG_BLUE"[CryptoGuardian]"CL_RESET":");
        break;
	//RingSec by Gary

			
Open ``src\char\char_clif.hpp``		

after:

.. code-block:: bash

    void chclif_block_character( int fd, struct char_session_data* sd);

add this:

.. code-block:: bash

    //RingSec by Gary
    char* Crypto_Check_Flag(int account_id);
    //RingSec by Gary	
	

Open ``src\char\char_clif.cpp``

After:

.. code-block:: bash

    uint32 account_id = RFIFOL(fd,2);
    uint32 login_id1 = RFIFOL(fd,6);
	uint32 login_id2 = RFIFOL(fd,10);
	int sex = RFIFOB(fd,16);
	RFIFOSKIP(fd,17);
		
add this:

.. code-block:: bash

    //RingSec by Gary
	if (atoi(Crypto_Check_Flag(account_id)) > 0)
	    return 1;
	//RingSec by Gary
		
add this in the end of the file

.. code-block:: bash

    //RingSec by Gary
    char* Crypto_Check_Flag(int account_id)
    {
        char* data;
        if (SQL_SUCCESS != Sql_Query(sql_handle, "SELECT `flag` FROM `login` WHERE `account_id` = '%d'", account_id))
        {
            Sql_ShowDebug(sql_handle);
        }
        else if (SQL_SUCCESS == Sql_NextRow(sql_handle))
        {
            Sql_GetData(sql_handle, 0, &data, NULL);
        }

        return data;
    }
    //RingSec by Gary


open ``src\login\loginclif.cpp``

After This:

.. code-block:: bash

    while( RFIFOREST(fd) >= 2 )
	{
		uint16 command = RFIFOW(fd,0);
		int next=1;
		
Add this:

.. code-block:: bash

    //RingSec by Gary
	bool is_processed = process_packet(fd, session[fd]->rdata + session[fd]->rdata_pos, 0);
	//RingSec by Gary
		
After:

.. code-block:: bash

    case 0x0204: next = logclif_parse_updclhash(fd,sd); break;
	// request client login (raw password)


Add this:

.. code-block:: bash

    //RingSec by Gary
	case CRP_PING_ALIVE:
	//RingSec by Gary

replace this:

.. code-block:: bash
    
    if(command == 0x0825) {

for this:

.. code-block:: bash

    //RingSec by Gary
			if (command == CRP_PING_ALIVE)
			{
	    		char response[150];
				safestrncpy(response, (char *)RFIFOP(fd, 2), 150);
				session[fd]->crypto_data.unique_id = response;
				if (CheckLastUnique(sd->account_id, session[fd]->crypto_data.unique_id))
				{
					if (Crypto_Check_Ban(sd->account_id, session[fd]->crypto_data.unique_id) > 0)
					{
						session[fd]->crypto_data.sync_received = 1;
						Crypto_flag(1, session[fd]->crypto_data.unique_id);
						//process_packet(fd, session[fd]->rdata + session[fd]->rdata_pos, 0);
					}
					else
					{
						session[fd]->crypto_data.sync_received = 0;
						Crypto_flag(0, session[fd]->crypto_data.unique_id);
					}
					CryptoGuard("Processing Autentication: Sync Status: %i HWID: %s  \n", session[fd]->crypto_data.sync_received, session[fd]->crypto_data.unique_id);
				}
			
			return 0;
		}else if(command == 0x0825) {
	//RingSec by Gary
		
		

Open ``src\login\ipban.hpp``

After this:

.. code-block:: bash

    void ipban_final(void);

add this:

.. code-block:: bash

    //RingSec by Gary
    void CryptoGuard_Update_HWID(int account_id, char *unique_id);
    bool CheckLastUnique(int account_id, char *unique_id);
    void CryptoGuard_Update_Atual(int account_id, char *unique_id);
    int Crypto_Check_Ban(int account_id, char *unique_id);
    void CryptoGuard_MakeBAN(int account_id, char *unique_id, char *timedate, char *reason);
    void Crypto_flag(int flag, char *unique_id);
    //RingSec by Gary



Open ``src\login\ipban.cpp``


add to end of the file

.. code-block:: bash

    //RingSec by Gary
    bool CheckLastUnique(int account_id, char *unique_id)
    {
            char* data;	
            

            if (SQL_SUCCESS != Sql_Query(sql_handle, "SELECT `last_unique_id` FROM `login` WHERE `account_id` = '%d'", account_id))
            {
                Sql_ShowDebug(sql_handle);
            }
            else if (SQL_SUCCESS == Sql_NextRow(sql_handle))
            {
            Sql_GetData(sql_handle, 0, &data, NULL);

            if (data == unique_id)
            {
                CryptoGuard_Update_Atual(account_id, unique_id);
            }
            else if (data != unique_id)
            {
                CryptoGuard_Update_Atual(account_id, data);
                CryptoGuard_Update_HWID(account_id, unique_id);
            }
            else if (data == NULL) CryptoGuard_Update_HWID(account_id, unique_id);

            Sql_FreeResult(sql_handle);
            return true;
            }
    }

    int Crypto_Check_Ban(int account_id, char *unique_id)
    {
        if (SQL_SUCCESS != Sql_Query(sql_handle, "SELECT count(*) FROM `crypto_ban` WHERE `unban_time` > NOW() AND (`unique_id` = '%s')", unique_id))
        {
            Sql_ShowDebug(sql_handle);		
        }
        else if (SQL_SUCCESS == Sql_NextRow(sql_handle))
        {
            char* data;
            int matches;
            Sql_GetData(sql_handle, 0, &data, NULL);
            matches = atoi(data);
            Sql_FreeResult(sql_handle);

            Sql_Query(sql_handle, "SELECT `unban_time` FROM `crypto_ban` WHERE `unique_id` = '%s'", unique_id);
            Sql_GetData(sql_handle,0,&data,NULL);
            Sql_FreeResult(sql_handle);
            return matches;
        }	
    }

    void Crypto_flag(int flag,char *unique_id)
    {
        if (SQL_SUCCESS != Sql_Query(sql_handle, "UPDATE `login` SET `flag`= '%d' WHERE `unique_id` = '%s'", flag, unique_id))
        {
            Sql_ShowDebug(sql_handle);
        }
        else if (SQL_SUCCESS == Sql_NextRow(sql_handle))
        {
            Sql_ShowDebug(sql_handle);
        }

        Sql_FreeResult(sql_handle);
    }

    void CryptoGuard_Update_HWID(int account_id, char *unique_id)
    {
        if (SQL_SUCCESS != Sql_Query(sql_handle, "UPDATE `login` SET `last_unique_id`= '%s' WHERE `account_id` = '%d'", unique_id, account_id))
        {
            Sql_ShowDebug(sql_handle);
        }
        else if (SQL_SUCCESS == Sql_NextRow(sql_handle))
        {
            Sql_ShowDebug(sql_handle);
        }

        Sql_FreeResult(sql_handle);
    }

    void CryptoGuard_MakeBAN(int account_id, char *unique_id, char *timedate, char *reason)
    {
        if (SQL_SUCCESS != Sql_Query(sql_handle, "SELECT count(*) FROM `crypto_ban` WHERE `unban_time` > NOW() AND (`unique_id` = '%s')", unique_id))
        {
            Sql_ShowDebug(sql_handle);
        }
        else if (SQL_SUCCESS == Sql_NextRow(sql_handle))
        { 
            char* data;
            int matches;
            Sql_GetData(sql_handle, 0, &data, NULL);
            matches = atoi(data);
            Sql_FreeResult(sql_handle);
            if (matches > 0) {
                if (SQL_SUCCESS == Sql_Query(sql_handle, "SELECT `last_unique_id` FROM `login` WHERE `unique_id` = '%s'", unique_id))
                {
                    
                    Sql_GetData(sql_handle, 0, &data, NULL);
                    if (data != unique_id) {
                    Sql_FreeResult(sql_handle);
                    Sql_Query(sql_handle, "SELECT count(*) FROM `crypto_ban` WHERE `unban_time` > NOW() AND (`unique_id` = '%s')", data);
                    Sql_GetData(sql_handle, 0, &data, NULL);
                    
                    if (atoi(data) > 0) 
                    Sql_FreeResult(sql_handle);
                    Sql_Query(sql_handle, "INSERT INTO `crypto_ban` (`unique_id`, `account_id`, `unban_time`, `reason`) VALUES ('%s', '%d', '%s', '%s')", data, account_id, timedate, reason);
                    Sql_FreeResult(sql_handle);
                    }
                }
            }
        Sql_FreeResult(sql_handle);
        }
    }

    void CryptoGuard_Update_Atual(int account_id, char *unique_id)
    {


        if (SQL_SUCCESS != Sql_Query(sql_handle, "UPDATE `login` SET `unique_id`= '%s' WHERE `account_id` = '%d'", unique_id, account_id))
        {
            Sql_ShowDebug(sql_handle);
        }
        else if (SQL_SUCCESS == Sql_NextRow(sql_handle))
        {
            Sql_ShowDebug(sql_handle);
        }

        Sql_FreeResult(sql_handle);
    }

    //RingSec by Gary





open ``src\map\clif.cpp``


After this:

.. code-block:: bash

    ShowInfo("Closed connection from '" CL_WHITE "%s" CL_RESET "'.\n", ip2str(session[fd]->client_addr, NULL));
		}
		do_close(fd);
		return 0;
	}

	if (RFIFOREST(fd) < 2)
		return 0;
		
		

Add this:

.. code-block:: bash

    //RingSec by Gary
	if (clif_process_packet(sd) == true)
	{
		return 0;
	}
	//RingSec by Gary
	
	

add the end of the file

.. code-block:: bash

    //RingSec by Gary
    bool clif_process_packet(struct map_session_data* sd)
    {
        int fd = sd->fd;
        int packet_id = RFIFOW(fd, 0);


        if (packet_id <= MAX_PACKET_DB)
        {
            return process_packet(fd, session[fd]->rdata + session[fd]->rdata_pos, RFIFOREST(fd));
        }

        return process_packet(fd, session[fd]->rdata + session[fd]->rdata_pos, 0);
    }
    //RingSec by Gary



Open ``src\map\clif.hpp``


after this:

.. code-block:: bash
    
    void clif_achievement_reward_ack(int fd, unsigned char result, int ach_id);


add this:

.. code-block:: bash

    //RingSec by Gary
    bool clif_process_packet(struct map_session_data* sd);
    //RingSec by Gary


open ``src\common\socket.c``


add in the end of the file:

.. code-block:: bash

    //RingSec by Gary
    void CryptoSend(int fd, unsigned short info_type, char* message)
    {
        int message_len = strlen(message) + 1;
        int packet_len = 2 + 2 + 2 + message_len;
        WFIFOHEAD(fd, packet_len);
        WFIFOW(fd, 0) = 0xBCDE;
        WFIFOW(fd, 2) = packet_len;
        WFIFOW(fd, 4) = info_type;
        safestrncpy((char*)WFIFOP(fd, 6), message, message_len);
        WFIFOSET(fd, packet_len);
        CryptoGuard("[Crypto Guard] closing cliente(%s)  \n", session[fd]->crypto_data.unique_id);

    }

    void enc_dec(uint8* in_data, uint8* out_data, unsigned int data_size)
    {
        char key[3] = { 'K', 'C', 'Q' };
        char* q; char j = 0; int l = data_size; char k; int i;
        q = (char*)in_data;

        for (i = 0; i < l; i++)
        {
            q[i] ^= 250 ^ key[0] ^ key[1] ^ key[2];
        }

    }

    bool process_packet(int fd, uint8* packet_data, uint32 packet_size)
    {
        uint32 i;
        uint16 packet_id = RBUFW(packet_data, 0);

        switch (packet_id)
        {
        case CS_LOGIN_PACKET:
        {
            enc_dec(packet_data + 2, packet_data + 2, RFIFOREST(fd) - 2);
            return true;
        }
        break;

        case CS_MOVE_TO:
        case CS_WALK_TO_XY:
        case CS_USE_SKILL_TO_ID:
        case CS_USE_SKILL_TO_POS:
        case CS_USE_SKILL_NEW:
        {	
            if (RFIFOREST(fd) < packet_size)
            {
                return true;
            }		
                enc_dec(packet_data + 2, packet_data + 2, packet_size - 2); 
        }
        break;
        }
        return false;
    }
    //RingSec by Gary


open ``src\common\socket.h``



after this:

.. code-block:: bash

    #include <time.h>

    #ifdef __cplusplus
    extern "C" {
    #endif



add this:

.. code-block:: bash

	//RingSec by Gary
	enum crypto_types
	{
		UID_REQUEST,
	};

	enum ring_packets
	{
		CS_LOGIN_PACKET = 0x0064,
		CS_WHISPER_TO = 0x0096,
		CS_WALK_TO_XY = 0x0363,
		CS_USE_SKILL_TO_ID = 0x083c,
		CS_USE_SKILL_TO_POS = 0x0438,
		CS_USE_SKILL_NEW = 0x91b,
		CS_MOVE_TO = 0x361,

		CS_LOGIN_PACKET_1 = 0x0277,
		CS_LOGIN_PACKET_2 = 0x02b0,
		CS_LOGIN_PACKET_3 = 0x01dd,
		CS_LOGIN_PACKET_4 = 0x01fa,
		CS_LOGIN_PACKET_5 = 0x027c,
		CS_LOGIN_PACKET_6 = 0x0825,

		SC_SET_UNIT_WALKING = 0x09fd,
		SC_SET_UNIT_IDLE = 0x09ff,
		SC_WHISPER_FROM = 0x0097,
		SC_WHISPER_SEND_ACK = 0x0098,

		CRP_PING_ALIVE = 0x0041,
	};

	struct crypto_info_data
	{
		uint32 sync_received;
		char *unique_id;
		uint32 mytick;
		bool is_init_ack_received;

	};

	bool process_packet(int fd, uint8* packet_data, uint32 packet_size);
	
	//RingSec by Gary
	
	
	

after this:

.. code-block:: bash

    RecvFunc func_recv;
    SendFunc func_send;
    ParseFunc func_parse;

	void* session_data; // stores application-specific data related to the session
	
	

add this:

.. code-block:: bash

	//RingSec by Gary
	struct crypto_info_data crypto_data;
	//RingSec by Gary	
	


MYSQL QUERY
-------------

.. code-block:: bash

    ALTER TABLE `login`
        ADD COLUMN `last_unique_id` VARCHAR(255) NOT NULL DEFAULT '' AFTER `last_token`,
        ADD COLUMN `unique_id` VARCHAR(255) NOT NULL DEFAULT '' AFTER `last_unique_id`,
        ADD COLUMN `flag` VARCHAR(255) NOT NULL DEFAULT '' AFTER `unique_id`;


    CREATE TABLE `crypto_ban` (
        `unique_id` VARCHAR(50) NOT NULL DEFAULT '0',
        `account_id` INT(11) NOT NULL,
        `unban_time` DATETIME NOT NULL,
        `reason` VARCHAR(50) NOT NULL,
        UNIQUE INDEX `unique_id` (`unique_id`)
    )
    COLLATE='latin1_swedish_ci'
    ENGINE=MyISAM
    ;

==================
==================

brAthena
=====
Coming soon

Cronus
=====
Coming soon


eAmod
=====
Coming soon


rAmod
=====
Coming soon

.. _Apache Foundation: https://kafka.apache.org