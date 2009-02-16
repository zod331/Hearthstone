/*
 * Aspire Hearthstone
 * Copyright (C) 2008 - 2009 AspireDev <http://www.aspiredev.org/>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "StdAfx.h"
#include "AuthCodes.h"
#include "svn_revision.h"

bool VerifyName(const char * name, size_t nlen)
{
	const char * p;
	size_t i;

	static const char * bannedCharacters = "\t\v\b\f\a\n\r\\\"\'\? <>[](){}_=+-|/!@#$%^&*~`.,0123456789\0";
	static const char * allowedCharacters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	if(sWorld.m_limitedNames)
	{
		for(i = 0; i < nlen; ++i)
		{
			p = allowedCharacters;
			for(; *p != 0; ++p)
			{
				if(name[i] == *p)
					goto cont;
			}

			return false;
cont:
			continue;
		}
	}
	else
	{
		for(i = 0; i < nlen; ++i)
		{
			p = bannedCharacters;
			while(*p != 0 && name[i] != *p && name[i] != 0)
				++p;

			if(*p != 0)
				return false;
		}
	}
	
	return true;
}

bool ChatHandler::HandleRenameAllCharacter(const char * args, WorldSession * m_session)
{
	uint32 uCount = 0;
	uint32 ts = getMSTime();
	QueryResult * result = CharacterDatabase.Query("SELECT guid, name FROM characters");
	if( result )
	{
		do 
		{
			uint32 uGuid = result->Fetch()[0].GetUInt32();
			const char * pName = result->Fetch()[1].GetString();
			size_t szLen = strlen(pName);

			if( !VerifyName(pName, szLen) )
			{
				printf("renaming character %s, %u\n", pName,uGuid);
                PlayerPointer pPlayer = objmgr.GetPlayer(uGuid);
				if( pPlayer != NULL )
				{
					pPlayer->rename_pending = true;
					pPlayer->GetSession()->SystemMessage("Your character has had a force rename set, you will be prompted to rename your character at next login in conformance with server rules.");
				}

				CharacterDatabase.WaitExecute("UPDATE characters SET forced_rename_pending = 1 WHERE guid = %u", uGuid);
				++uCount;
			}

		} while (result->NextRow());
		delete result;
	}

	SystemMessage(m_session, "Procedure completed in %u ms. %u character(s) forced to rename.", getMSTime() - ts, uCount);
	return true;
}

void CapitalizeString(string& arg)
{
	if(arg.length() == 0) return;
	arg[0] = toupper(arg[0]);
	for(uint32 x = 1; x < arg.size(); ++x)
		arg[x] = tolower(arg[x]);
}

void WorldSession::CharacterEnumProc(QueryResult * result)
{
	struct player_item
	{
		uint32 displayid;
		uint8 invtype;
	};

	player_item items[20];
	int8 slot;
	int8 containerslot;
	uint32 i;
	ItemPrototype * proto;
	QueryResult * res;
	CreatureInfo *info = NULL;
	uint8 num = 0;
	uint8 race;

	m_asyncQuery = false;

	//Erm, reset it here in case player deleted his DK.
	m_hasDeathKnight= false;

	// should be more than enough.. 200 bytes per char..
	WorldPacket data(SMSG_CHAR_ENUM, (result ? result->GetRowCount() * 200 : 1));	

	// parse m_characters and build a mighty packet of
	// characters to send to the client.
	data << num;
	if( result )
	{
		uint64 guid;
		uint8 Class;
		uint32 bytes2;
		uint32 flags;
		uint32 banned;
		Field *fields;
		do
		{
			fields = result->Fetch();
			guid = fields[0].GetUInt32();
			bytes2 = fields[6].GetUInt32();
			Class = fields[3].GetUInt8();			
			flags = fields[17].GetUInt32();
			race = fields[3].GetUInt8();

			if( _side < 0 )
			{
				// work out the side
				static uint8 sides[RACE_DRAENEI+1] = { 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0 };
				_side = sides[race];
			}

			/* build character enum, w0000t :p */
			data << fields[0].GetUInt64();		// guid
			data << fields[7].GetString();		// name
			data << fields[2].GetUInt8();		// race
			data << race;						// class
			data << fields[4].GetUInt8();		// gender
			data << fields[5].GetUInt32();		// PLAYER_BYTES
			data << uint8(bytes2 & 0xFF);		// facial hair
			data << fields[1].GetUInt8();		// Level
			data << fields[12].GetUInt32();		// zoneid
			data << fields[11].GetUInt32();		// Mapid
			data << fields[8].GetFloat();		// X
			data << fields[9].GetFloat();		// Y
			data << fields[10].GetFloat();		// Z
			data << fields[18].GetUInt32();		// GuildID

			if( fields[1].GetUInt8() > m_highestLevel )
				m_highestLevel = fields[1].GetUInt8();

			if( Class == DEATHKNIGHT )
				m_hasDeathKnight = true;

			banned = fields[13].GetUInt32();
			if(banned && (banned<10 || banned > (uint32)UNIXTIME))
				data << uint32(0x01A04040);
			else
			{
				if(fields[16].GetUInt32() != 0)
					data << uint32(0x00A04342);
				else if(fields[15].GetUInt32() != 0)
					data << (uint32)8704; // Dead (displaying as Ghost)
				else
					data << uint32(1);		// alive
			}

			data << uint32(0);					//Added in 3.0.2
			data << fields[14].GetUInt8();		// Rest State

			if(Class==9 || Class==3)
			{
				res = CharacterDatabase.Query("SELECT entry FROM playerpets WHERE ownerguid="I64FMTD" AND active=1", guid);

				if(res)
				{
					info = CreatureNameStorage.LookupEntry(res->Fetch()[0].GetUInt32());
					delete res;
				}
				else
					info=NULL;
			}
			else
				info=NULL;

			if(info)  //PET INFO uint32 displayid,	uint32 level,		 uint32 familyid
				data << uint32(info->Male_DisplayID) << uint32(10) << uint32(info->Family);
			else
				data << uint32(0) << uint32(0) << uint32(0);

			res = CharacterDatabase.Query("SELECT containerslot, slot, entry FROM playeritems WHERE ownerguid=%u", GUID_LOPART(guid));

			memset(items, 0, sizeof(player_item) * 20);
			if(res)
			{
				do 
				{
					containerslot = res->Fetch()[0].GetInt8();
					slot = res->Fetch()[1].GetInt8();
					if( containerslot == -1 && slot < 19 && slot >= 0 )
					{
						proto = ItemPrototypeStorage.LookupEntry(res->Fetch()[2].GetUInt32());
						if(proto)
						{
							// slot0 = head, slot14 = cloak
							if(!(slot == 0 && (flags & (uint32)PLAYER_FLAG_NOHELM) != 0) && !(slot == 14 && (flags & (uint32)PLAYER_FLAG_NOCLOAK) != 0)) {
								items[slot].displayid = proto->DisplayInfoID;
								items[slot].invtype = proto->InventoryType;
							}
						}
					}
				} while(res->NextRow());
				delete res;
			}

			for( i = 0; i < 20; ++i )
			{
				// 2.4.0 added a uint32 here, it's obviously one of the itemtemplate fields,
				// we just gotta figure out which one :P
				data << items[i].displayid << items[i].invtype << uint32(0);
			}

			num++;
		}
		while( result->NextRow() );
	}

	data.put<uint8>(0, num);

	//DEBUG_LOG("Character Enum", "Built in %u ms.", getMSTime() - start_time);
	SendPacket( &data );
}

void WorldSession::HandleCharEnumOpcode( WorldPacket & recv_data )
{	
	if( m_asyncQuery )		// should be enough
	{
		return;
	}

	AsyncQuery * q = new AsyncQuery( new SQLClassCallbackP1<World, uint32>(World::getSingletonPtr(), &World::CharacterEnumProc, GetAccountId()) );
	q->AddQuery("SELECT guid, level, race, class, gender, bytes, bytes2, name, positionX, positionY, positionZ, mapId, zoneId, banned, restState, deathstate, forced_rename_pending, player_flags, guild_data.guildid FROM characters LEFT JOIN guild_data ON characters.guid = guild_data.playerid WHERE acct=%u ORDER BY guid ASC LIMIT 10", GetAccountId());
	m_asyncQuery = true;
	CharacterDatabase.QueueAsyncQuery(q);
	m_lastEnumTime = (uint32)UNIXTIME;
}

void WorldSession::LoadAccountDataProc(QueryResult * result)
{
	size_t len;
	const char * data;
	char * d;

	if(!result)
	{
		CharacterDatabase.Execute("INSERT INTO account_data VALUES(%u, '', '', '', '', '', '', '', '', '')", _accountId);
		return;
	}

	for(uint32 i = 0; i < 7; ++i)
	{
		data = result->Fetch()[1+i].GetString();
		len = data ? strlen(data) : 0;
		if(len > 1)
		{
			d = new char[len+1];
			memcpy(d, data, len+1);
			SetAccountData(i, d, true, (uint32)len);
		}
	}
}

void WorldSession::HandleCharCreateOpcode( WorldPacket & recv_data )
{
	CHECK_PACKET_SIZE(recv_data, 10);
	std::string name;
	uint8 race, class_;

	recv_data >> name >> race >> class_;
	recv_data.rpos(0);

	if(!VerifyName(name.c_str(), name.length()))
	{
		OutPacket(SMSG_CHAR_CREATE, 1, "\x32");
		return;
	}

	if(g_characterNameFilter->Parse(name, false))
	{
		OutPacket(SMSG_CHAR_CREATE, 1, "\x32");
		return;
	}

	if(objmgr.GetPlayerInfoByName(name.c_str()) != 0)
	{
		OutPacket(SMSG_CHAR_CREATE, 1, "\x32");
		return;
	}

	if(!sHookInterface.OnNewCharacter(race, class_, this, name.c_str()))
	{
		OutPacket(SMSG_CHAR_CREATE, 1, "\x32");
		return;
	}

	if( class_ == DEATHKNIGHT && (!HasFlag(ACCOUNT_FLAG_XPACK_02) || !CanCreateDeathKnight() ) )
	{
		OutPacket(SMSG_CHAR_CREATE, 1, "\x3B");
		return;
	}

	QueryResult * result = CharacterDatabase.Query("SELECT COUNT(*) FROM banned_names WHERE name = '%s'", CharacterDatabase.EscapeString(name).c_str());
	if(result)
	{
		if(result->Fetch()[0].GetUInt32() > 0)
		{
			// That name is banned!
			OutPacket(SMSG_CHAR_CREATE, 1, "\x51"); // You cannot use that name
			delete result;
			return;
		}
		delete result;
	}
	// loading characters
	
	//checking number of chars is useless since client will not allow to create more than 10 chars
	//as the 'create' button will not appear (unless we want to decrease maximum number of characters)

	PlayerPointer pNewChar = objmgr.CreatePlayer();
	pNewChar->SetSession(this);
	if(!pNewChar->Create( recv_data ))
	{
		// failed.
		pNewChar->ok_to_remove = true;
		pNewChar->Destructor();
		pNewChar = NULLPLR;
		return;
	}

	//Same Faction limitation only applies to PVP and RPPVP realms :)
	uint32 realmType = sLogonCommHandler.GetRealmType();
	if( !HasGMPermissions() && realmType == REALM_PVP && _side < 0 )
	{
		if( ((pNewChar->GetTeam()== 0) && (_side == 1)) || ((pNewChar->GetTeam()== 1) && (_side == 0)) )
		{
			pNewChar->ok_to_remove = true;
			pNewChar->Destructor();
			pNewChar = NULLPLR; // die!
			WorldPacket data(1);
			data.SetOpcode(SMSG_CHAR_CREATE);
			data << (uint8)ALL_CHARS_ON_PVP_REALM_MUST_AT_SAME_SIDE+1;
			SendPacket( &data );
			return;
		}
	}
	pNewChar->UnSetBanned();
	pNewChar->addSpell(22027);	  // Remove Insignia

	if(pNewChar->getClass() == WARLOCK)
	{
		pNewChar->AddSummonSpell(416, 3110);		// imp fireball
		pNewChar->AddSummonSpell(417, 19505);
		pNewChar->AddSummonSpell(1860, 3716);
		pNewChar->AddSummonSpell(1863, 7814);
	}

	pNewChar->SaveToDB(true);	

	PlayerInfo *pn=new PlayerInfo;
	memset(pn, 0, sizeof(PlayerInfo));
	pn->guid = pNewChar->GetLowGUID();
	pn->name = strdup(pNewChar->GetName());
	pn->cl = pNewChar->getClass();
	pn->race = pNewChar->getRace();
	pn->gender = pNewChar->getGender();
	pn->lastLevel = pNewChar->getLevel();
	pn->lastZone = pNewChar->GetZoneId();
	pn->lastOnline = UNIXTIME;
	pn->team = pNewChar->GetTeam();
	pn->acct = GetAccountId();
#ifdef VOICE_CHAT
	pn->groupVoiceId = -1;
#endif
	objmgr.AddPlayerInfo(pn);

	pNewChar->ok_to_remove = true;
	pNewChar->Destructor();
	pNewChar = NULLPLR;

	// CHAR_CREATE_SUCCESS
	OutPacket(SMSG_CHAR_CREATE, 1, "\x2F");

	sLogonCommHandler.UpdateAccountCount(GetAccountId(), 1);
	m_lastEnumTime = 0;
}

/* Last Checked at patch 3.0.2 Specfic SMSG_CHAR_CREATE Error Codes:

    RESPONSE_SUCCESS = 0,
    RESPONSE_FAILURE = 1,
    RESPONSE_CANCELLED = 2,
    RESPONSE_DISCONNECTED = 3,
    RESPONSE_FAILED_TO_CONNECT = 4,
    RESPONSE_VERSION_MISMATCH = 5,
    CSTATUS_CONNECTING = 6,
    CSTATUS_NEGOTIATING_SECURITY = 7,
    CSTATUS_NEGOTIATION_COMPLETE = 8,
    CSTATUS_NEGOTIATION_FAILED = 9,
    CSTATUS_AUTHENTICATING = 10,
    AUTH_OK = 11,
    AUTH_FAILED = 12,
    AUTH_REJECT = 13,
    AUTH_BAD_SERVER_PROOF = 14,
    AUTH_UNAVAILABLE = 15,
    AUTH_SYSTEM_ERROR = 16,
    AUTH_BILLING_ERROR = 17,
    AUTH_BILLING_EXPIRED = 18,
    AUTH_VERSION_MISMATCH = 19,
    AUTH_UNKNOWN_ACCOUNT = 20,
    AUTH_INCORRECT_PASSWORD = 21,
    AUTH_SESSION_EXPIRED = 22,
    AUTH_SERVER_SHUTTING_DOWN = 23,
    AUTH_ALREADY_LOGGING_IN = 24,
    AUTH_LOGIN_SERVER_NOT_FOUND = 25,
    AUTH_WAIT_QUEUE = 26,
    AUTH_BANNED = 27,
    AUTH_ALREADY_ONLINE = 28,
    AUTH_NO_TIME = 29,
    AUTH_DB_BUSY = 30,
    AUTH_SUSPENDED = 31,
    AUTH_PARENTAL_CONTROL = 32,
    AUTH_LOCKED_ENFORCED = 33,
    REALM_LIST_SUCCESS = 34,
    REALM_LIST_FAILED = 35,
    REALM_LIST_INVALID = 36,
    REALM_LIST_REALM_NOT_FOUND = 37,
    ACCOUNT_CREATE_IN_PROGRESS = 38,
    ACCOUNT_CREATE_SUCCESS = 39,
    ACCOUNT_CREATE_FAILED = 40,
    CHAR_LIST_RETRIEVED = 41,
    CHAR_LIST_FAILED = 42,
    CHAR_CREATE_IN_PROGRESS = 43,
    CHAR_CREATE_SUCCESS = 44,
    CHAR_CREATE_ERROR = 45,
    CHAR_CREATE_FAILED = 46,
    CHAR_CREATE_DISABLED = 47,
    CHAR_CREATE_PVP_TEAMS_VIOLATION = 48,
    CHAR_CREATE_SERVER_LIMIT = 49,
    CHAR_CREATE_ACCOUNT_LIMIT = 50,
    CHAR_CREATE_SERVER_QUEUE = 51,
    CHAR_CREATE_ONLY_EXISTING = 52,
    CHAR_CREATE_EXPANSION = 53,
    CHAR_CREATE_EXPANSION_CLASS = 54,
    CHAR_CREATE_LEVEL_REQUIREMENT = 55,
    CHAR_CREATE_UNIQUE_CLASS_LIMIT = 56,
    CHAR_DELETE_IN_PROGRESS = 57,
    CHAR_DELETE_SUCCESS = 58,0x3A
    CHAR_DELETE_FAILED = 59, 0x3B
    CHAR_DELETE_FAILED_LOCKED_FOR_TRANSFER = 60, 0x3C
    CHAR_DELETE_FAILED_GUILD_LEADER = 61, 0x3D
    CHAR_DELETE_FAILED_ARENA_CAPTAIN = 62, 0x3E
    CHAR_LOGIN_IN_PROGRESS = 63,
    CHAR_LOGIN_SUCCESS = 64,
    CHAR_LOGIN_NO_WORLD = 65,
    CHAR_LOGIN_DUPLICATE_CHARACTER = 66,
    CHAR_LOGIN_NO_INSTANCES = 67,
    CHAR_LOGIN_FAILED = 68,
    CHAR_LOGIN_DISABLED = 69,
    CHAR_LOGIN_NO_CHARACTER = 70,
    CHAR_LOGIN_LOCKED_FOR_TRANSFER = 71,
    CHAR_LOGIN_LOCKED_BY_BILLING = 72,
    CHAR_NAME_SUCCESS = 73,
    CHAR_NAME_FAILURE = 74,
    CHAR_NAME_NO_NAME = 75,
    CHAR_NAME_TOO_SHORT = 76,
    CHAR_NAME_TOO_LONG = 77,
    CHAR_NAME_INVALID_CHARACTER = 78,
    CHAR_NAME_MIXED_LANGUAGES = 79,
    CHAR_NAME_PROFANE = 80,
    CHAR_NAME_RESERVED = 81,
    CHAR_NAME_INVALID_APOSTROPHE = 82,
    CHAR_NAME_MULTIPLE_APOSTROPHES = 83,
    CHAR_NAME_THREE_CONSECUTIVE = 84,
    CHAR_NAME_INVALID_SPACE = 85,
    CHAR_NAME_CONSECUTIVE_SPACES = 86,
    CHAR_NAME_RUSSIAN_CONSECUTIVE_SILENT_CHARACTERS = 87,
    CHAR_NAME_RUSSIAN_SILENT_CHARACTER_AT_BEGINNING_OR_END = 88,
    CHAR_NAME_DECLENSION_DOESNT_MATCH_BASE_NAME = 89,
*/

void WorldSession::HandleCharDeleteOpcode( WorldPacket & recv_data )
{
	CHECK_PACKET_SIZE(recv_data, 8);
	uint8 fail = 0x3E;

	uint64 guid;
	recv_data >> guid;

	if(objmgr.GetPlayer((uint32)guid) != NULL)
	{
		// "Char deletion failed"
		fail = 0x3F;
	}
	else
	{
		fail = DeleteCharacter((uint32)guid);
		OutPacket(SMSG_CHAR_DELETE, 1, &fail);
		m_lastEnumTime = 0;
	}
}

uint8 WorldSession::DeleteCharacter(uint32 guid)
{
	PlayerInfo * inf = objmgr.GetPlayerInfo(guid);
	if( inf != NULL && inf->m_loggedInPlayer == NULL )
	{
		QueryResult * result = CharacterDatabase.Query("SELECT name FROM characters WHERE guid = %u AND acct = %u", (uint32)guid, _accountId);
		if(!result)
			return 0x3F;

		if(inf->guild)
		{
			if(inf->guild->GetGuildLeader()==inf->guid)
				return 0x41;
			else
				inf->guild->RemoveGuildMember(inf,NULL);
		}

		string name = result->Fetch()[0].GetString();
		delete result;

		for(int i = 0; i < NUM_CHARTER_TYPES; ++i)
		{
			if( inf->charterId[i] != 0 )
			{
				Charter *pCharter = objmgr.GetCharter(inf->charterId[i], (CharterTypes)i);
				if( pCharter->LeaderGuid == inf->guid )
					pCharter->Destroy();
				else
					pCharter->RemoveSignature(inf->guid);
			}
		}

		for(int i = 0; i < NUM_ARENA_TEAM_TYPES; ++i)
		{
			if( inf->arenaTeam[i] != NULL )
			{
				if( inf->arenaTeam[i]->m_leader == guid )
					return 0x42;
				else
					inf->arenaTeam[i]->RemoveMember(inf);
			}
		}
			
		/*if( _socket != NULL )
			sPlrLog.write("Account: %s | IP: %s >> Deleted player %s", GetAccountName().c_str(), GetSocket()->GetRemoteIP().c_str(), name.c_str());*/
			
		sPlrLog.writefromsession(this, "deleted character %s (GUID: %u)", name.c_str(), (uint32)guid);

		CharacterDatabase.WaitExecute("DELETE FROM characters WHERE guid = %u", (uint32)guid);

		CorpsePointer c=objmgr.GetCorpseByOwner((uint32)guid);
		if(c)
			CharacterDatabase.Execute("DELETE FROM corpses WHERE guid = %u", c->GetLowGUID());

		CharacterDatabase.Execute("DELETE FROM achievements WHERE player = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM auctions WHERE owner = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM gm_tickets WHERE guid = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM mailbox WHERE player_guid = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM playercooldowns WHERE player_guid = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM playeritems WHERE ownerguid=%u",(uint32)guid);
		CharacterDatabase.Execute("DELETE FROM playerskills WHERE player_guid = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM playerpets WHERE ownerguid = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM playerpetspells WHERE ownerguid = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM playersummonspells WHERE ownerguid = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM questlog WHERE player_guid = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM tutorials WHERE playerId = %u", (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM social_friends WHERE character_guid = %u OR friend_guid = %u", (uint32)guid, (uint32)guid);
		CharacterDatabase.Execute("DELETE FROM social_ignores WHERE character_guid = %u OR ignore_guid = %u", (uint32)guid, (uint32)guid);
 
	 
		/* remove player info */
		objmgr.DeletePlayerInfo((uint32)guid);
		return 0x3E;
	}

	return 0x3F;
}

void WorldSession::HandleCharRenameOpcode(WorldPacket & recv_data)
{
	WorldPacket data(SMSG_CHAR_RENAME, recv_data.size() + 1);

	uint64 guid;
	string name;
	recv_data >> guid >> name;

	PlayerInfo * pi = objmgr.GetPlayerInfo((uint32)guid);
	if(pi == 0) return;

	QueryResult * result = CharacterDatabase.Query("SELECT forced_rename_pending FROM characters WHERE guid = %u AND acct = %u", (uint32)guid, _accountId);
	if(result == 0)
	{
		delete result;
		return;
	}
	delete result;

	// Check name for rule violation.
	const char * szName=name.c_str();
	for(uint32 x=0;x<strlen(szName);x++)
	{
		if((int)szName[x]<65||((int)szName[x]>90&&(int)szName[x]<97)||(int)szName[x]>122)
		{
			data << uint8(0x32);
			data << guid << name;
			SendPacket(&data);
			return;
		}
	}

	QueryResult * result2 = CharacterDatabase.Query("SELECT COUNT(*) FROM banned_names WHERE name = '%s'", CharacterDatabase.EscapeString(name).c_str());
	if(result2)
	{
		if(result2->Fetch()[0].GetUInt32() > 0)
		{
			// That name is banned!
			data << uint8(0x31);
			data << guid << name;
			SendPacket(&data);
		}
		delete result2;
	}

	// Check if name is in use.
	if(objmgr.GetPlayerInfoByName(name.c_str()) != 0)
	{
		data << uint8(0x32);
		data << guid << name;
		SendPacket(&data);
		return;
	}

	// correct capitalization
	CapitalizeString(name);
	objmgr.RenamePlayerInfo(pi, pi->name, name.c_str());

	sPlrLog.writefromsession(this, "a rename was pending. renamed character %s (GUID: %u) to %s.", pi->name, pi->guid, name.c_str());

	// If we're here, the name is okay.
	CharacterDatabase.Query("UPDATE characters SET name = \'%s\',  forced_rename_pending  = 0 WHERE guid = %u AND acct = %u",name.c_str(), (uint32)guid, _accountId);
	free(pi->name);
	pi->name = strdup(name.c_str());

	data << uint8(0) << guid << name;
	SendPacket(&data);
	m_lastEnumTime = 0;
}


void WorldSession::HandlePlayerLoginOpcode( WorldPacket & recv_data )
{
	CHECK_PACKET_SIZE(recv_data, 8);
	uint64 playerGuid = 0;

	Log.Debug( "WorldSession"," Recvd Player Logon Message" );

	recv_data >> playerGuid; // this is the GUID selected by the player
	if(objmgr.GetPlayer((uint32)playerGuid) != NULL || m_loggingInPlayer || _player)
	{
		// A character with that name already exists 0x3E
		uint8 respons = CHAR_LOGIN_DUPLICATE_CHARACTER;
		OutPacket(SMSG_CHARACTER_LOGIN_FAILED, 1, &respons);
		return;
	}

	PlayerPointer plr = PlayerPointer (new Player((uint32)playerGuid));
	plr->Init();
	plr->SetSession(this);
	m_bIsWLevelSet = false;
	
	Log.Debug("WorldSession", "Async loading player %u", (uint32)playerGuid);
	m_loggingInPlayer = plr;
	plr->LoadFromDB((uint32)playerGuid);
}

void WorldSession::FullLogin(PlayerPointer plr)
{
	Log.Debug("WorldSession", "Fully loading player %u", plr->GetLowGUID());
	SetPlayer(plr); 
	m_MoverWoWGuid.Init(plr->GetGUID());

	// copy to movement array
	movement_packet[0] = m_MoverWoWGuid.GetNewGuidMask();
	memcpy(&movement_packet[1], m_MoverWoWGuid.GetNewGuid(), m_MoverWoWGuid.GetNewGuidLen());

	WorldPacket datab(MSG_SET_DUNGEON_DIFFICULTY, 20);
	datab << plr->iInstanceType;
	datab << uint32(0x01);
	datab << uint32(0x00);
	SendPacket(&datab);

	WorldPacket datat(SMSG_MOTD, 50);
	datat << uint32(0x04);
	datat << sWorld.GetMotd();
	SendPacket(&datat);

	/* world preload */
	packetSMSG_LOGIN_VERIFY_WORLD vwpck;
	vwpck.MapId = plr->GetMapId();
	vwpck.O = plr->GetOrientation();
	vwpck.X = plr->GetPositionX();
	vwpck.Y = plr->GetPositionY();
	vwpck.Z = plr->GetPositionZ();
	OutPacket( SMSG_LOGIN_VERIFY_WORLD, sizeof(packetSMSG_LOGIN_VERIFY_WORLD), &vwpck );

	// send voicechat state - active/inactive
	/*
	{SERVER} Packet: (0x03C7) UNKNOWN PacketSize = 2
	|------------------------------------------------|----------------|
	|00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F |0123456789ABCDEF|
	|------------------------------------------------|----------------|
	|02 01							               |..              |
	-------------------------------------------------------------------
	*/

#ifdef VOICE_CHAT
	datab.Initialize(SMSG_FEATURE_SYSTEM_STATUS);
	datab << uint8(2) << uint8(sVoiceChatHandler.CanUseVoiceChat() ? 1 : 0);
	SendPacket(&datab);
#else
	datab.Initialize(SMSG_FEATURE_SYSTEM_STATUS);
	datab << uint8(2) << uint8(0);
#endif

	plr->UpdateAttackSpeed();
	/*if(plr->getLevel()>70)
		plr->SetUInt32Value(UNIT_FIELD_LEVEL,70);*/

	// enable trigger cheat by default
	plr->triggerpass_cheat = HasGMPermissions();

	// Make sure our name exists (for premade system)
	PlayerInfo * info = objmgr.GetPlayerInfo(plr->GetLowGUID());
	if(info == 0)
	{
		info = new PlayerInfo;
		memset(info, 0, sizeof(PlayerInfo));
		info->cl = plr->getClass();
		info->gender = plr->getGender();
		info->guid = plr->GetLowGUID();
		info->name = strdup(plr->GetName());
		info->lastLevel = plr->getLevel();
		info->lastOnline = UNIXTIME;
		info->lastZone = plr->GetZoneId();
		info->race = plr->getRace();
		info->team = plr->GetTeam();
		objmgr.AddPlayerInfo(info);
	}
	plr->m_playerInfo = info;
	if(plr->m_playerInfo->guild)
	{
		plr->m_uint32Values[PLAYER_GUILDID] = plr->m_playerInfo->guild->GetGuildId();
		plr->m_uint32Values[PLAYER_GUILDRANK] = plr->m_playerInfo->guildRank->iId;
	}

	for(uint32 z = 0; z < NUM_ARENA_TEAM_TYPES; ++z)
	{
		if(_player->m_playerInfo->arenaTeam[z] != NULL)
		{
			_player->SetUInt32Value(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + (z*6), _player->m_playerInfo->arenaTeam[z]->m_id);
			if(_player->m_playerInfo->arenaTeam[z]->m_leader == _player->GetLowGUID())
				_player->SetUInt32Value(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + (z*6) + 1, 0);
			else
				_player->SetUInt32Value(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + (z*6) + 1, 1);
		}
	}

	info->m_loggedInPlayer = plr;

	// account data == UI config
	WorldPacket data(SMSG_ACCOUNT_DATA_TIMES, 128);
	MD5Hash md5hash;

	for (int i = 0; i < 8; i++)
	{
		AccountDataEntry* acct_data = GetAccountData(i);

		if (!acct_data->data)
		{
			data << uint64(0) << uint64(0);				// Nothing.
			continue;
		}
		md5hash.Initialize();
		md5hash.UpdateData((const uint8*)acct_data->data, acct_data->sz);
		md5hash.Finalize();

		data.append(md5hash.GetDigest(), MD5_DIGEST_LENGTH);
	}
	SendPacket(&data);

	_player->ResetTitansGrip();

	// Set TIME OF LOGIN
	CharacterDatabase.Execute (
		"UPDATE characters SET online = 1 WHERE guid = %u" , plr->GetLowGUID());

	bool enter_world = true;
#ifndef CLUSTERING
	// Find our transporter and add us if we're on one.
	if(plr->m_TransporterGUID != 0)
	{
		TransporterPointer pTrans = objmgr.GetTransporter(GUID_LOPART(plr->m_TransporterGUID));
		if(pTrans)
		{
			if(plr->isDead())
			{
				plr->RemoteRevive();
			}

			float c_tposx = pTrans->GetPositionX() + plr->m_TransporterX;
			float c_tposy = pTrans->GetPositionY() + plr->m_TransporterY;
			float c_tposz = pTrans->GetPositionZ() + plr->m_TransporterZ;
			if(plr->GetMapId() != pTrans->GetMapId())	   // loaded wrong map
			{
				plr->SetMapId(pTrans->GetMapId());

				WorldPacket dataw(SMSG_NEW_WORLD, 20);
				dataw << pTrans->GetMapId() << c_tposx << c_tposy << c_tposz << plr->GetOrientation();
				SendPacket(&dataw);

				// shit is sent in worldport ack.
				enter_world = false;
			}

			plr->SetPosition(c_tposx, c_tposy, c_tposz, plr->GetOrientation(), false);
			plr->m_CurrentTransporter = pTrans;
			pTrans->AddPlayer(plr);
		}
	}
#endif

	Log.Debug( "WorldSession","Player %s logged in.", plr->GetName());

	if(plr->GetTeam() == 1)
		sWorld.HordePlayers++;
	else
		sWorld.AlliancePlayers++;

	if(plr->m_FirstLogin && !HasGMPermissions())
		OutPacket(SMSG_TRIGGER_CINEMATIC, 4, &plr->myRace->cinematic_id);

	Log.Debug( "WorldSession","Created new player for existing players (%s)", plr->GetName() );

	// Login time, will be used for played time calc
	plr->m_playedtime[2] = (uint32)UNIXTIME;

	//Issue a message telling all guild members that this player has signed on
	if(plr->IsInGuild())
	{
		Guild *pGuild = plr->m_playerInfo->guild;
		if(pGuild)
		{
			WorldPacket data(50);
			data.Initialize(SMSG_GUILD_EVENT);
			data << uint8(GUILD_EVENT_MOTD);
			data << uint8(0x01);
			if(pGuild->GetMOTD())
				data << pGuild->GetMOTD();
			else
				data << uint8(0);
			SendPacket(&data);

			pGuild->LogGuildEvent(GUILD_EVENT_HASCOMEONLINE, 1, plr->GetName());
		}
	}

	// Send online status to people having this char in friendlist
	_player->Social_TellFriendsOnline();
	// send friend list (for ignores)
	_player->Social_SendFriendList(7);

	// Send revision (if enabled)
#ifdef WIN32
	_player->BroadcastMessage("Server: %sHearthstone r%u-TRUNK/%s-Win-%s", MSG_COLOR_WHITE, BUILD_REVISION, CONFIG, ARCH, MSG_COLOR_LIGHTBLUE);
#else
	_player->BroadcastMessage("Server: %sHearthstone r%u-TRUNK/%s-%s", MSG_COLOR_WHITE, BUILD_REVISION, PLATFORM_TEXT, ARCH, MSG_COLOR_LIGHTBLUE);
	//_player->BroadcastMessage("Built at %s on %s by %s@%s", BUILD_TIME, BUILD_DATE, BUILD_USER, BUILD_HOST);
#endif

	if(sWorld.SendStatsOnJoin)
	{
		_player->BroadcastMessage("Online Players: %s%u |rPeak: %s%u|r Accepted Connections: %s%u",
			MSG_COLOR_WHITE, sWorld.GetSessionCount(), MSG_COLOR_WHITE, sWorld.PeakSessionCount, MSG_COLOR_WHITE, sWorld.mAcceptedConnections);
		_player->BroadcastMessage("Server Uptime: |r%s", sWorld.GetUptimeString().c_str());
	}

	// send to gms
	if( HasGMPermissions() )
		sWorld.SendMessageToGMs(this, "GM %s (%s) is now online. (Permissions: [%s])", _player->GetName(), GetAccountNameS(), GetPermissions());

	// Calculate rested experience if there is time between lastlogoff and now
	uint32 currenttime = (uint32)UNIXTIME;
	uint32 timediff = currenttime - plr->m_timeLogoff;

	if(plr->m_timeLogoff > 0 && plr->GetUInt32Value(UNIT_FIELD_LEVEL) < plr->GetUInt32Value(PLAYER_FIELD_MAX_LEVEL))	// if timelogoff = 0 then it's the first login
	{
		if(plr->m_isResting) 
		{
			// We are resting at an inn, calculate XP and add it.
			uint32 RestXP = plr->CalculateRestXP((uint32)timediff);
			plr->AddRestXP(RestXP);
			Log.Debug( "WorldSession","Gained on additional %u XP from resting.", RestXP);
			plr->ApplyPlayerRestState(true);
		}
		else if(timediff > 0)
		{
			// We are resting in the wilderness at a slower rate.
			uint32 RestXP = plr->CalculateRestXP((uint32)timediff);
			RestXP >>= 2;		// divide by 4 because its at 1/4 of the rate
			plr->AddRestXP(RestXP);
		}
	}

#ifdef CLUSTERING
	plr->SetInstanceID(forced_instance_id);
	plr->SetMapId(forced_map_id);
#else
	sHookInterface.OnEnterWorld2(_player);
#endif

	if(info->m_Group)
		info->m_Group->Update();

	//death system checkout
	if(_player->GetUInt32Value(UNIT_FIELD_HEALTH) == 0 || _player->isDead() || _player->HasFlag(PLAYER_FLAGS, PLAYER_FLAG_DEATH_WORLD_ENABLE))
	{
		CorpsePointer corpse = objmgr.GetCorpseByOwner(_player->GetLowGUID());
		if( corpse != NULL )
		{
			MapMgrPointer myMgr = sInstanceMgr.GetInstance(_player);
			if( myMgr == NULL || (myMgr->GetMapInfo()->type != INSTANCE_NULL && myMgr->GetMapInfo()->type != INSTANCE_PVP)
				|| (myMgr->GetMapId() == corpse->GetMapId()) )	
			{
				// instance death, screw it, revive them
				_player->ResurrectPlayer(NULLPLR);
				_player->SpawnCorpseBones();
				//_player->KillPlayer();
				//_player->RepopRequestedPlayer();
			}
			else
			{
				// got a corpse
				_player->setDeathState(CORPSE);
				_player->SetFlag(PLAYER_FLAGS, PLAYER_FLAG_DEATH_WORLD_ENABLE);
				_player->SetUInt32Value(UNIT_FIELD_HEALTH, 1);
			}
		}
		else
		{
			// logged out after death, attempt repop
			// otherwise people can cheat death by relogging
			if( _player->m_uint32Values[UNIT_FIELD_HEALTH] == 0 )
			{
				_player->KillPlayer();
				_player->RepopRequestedPlayer();
			}
			else
			{
				// just revive them
				_player->ResurrectPlayer(NULLPLR);
			}
		}
	}

	// Retroactive: Level achievement
	_player->GetAchievementInterface()->HandleAchievementCriteriaLevelUp( _player->getLevel() );
	// Retroactive: Bank slots: broken atm :(
	//_player->GetAchievementInterface()->HandleAchievementCriteriaBuyBankSlot(true);

	// Send achievement data!
	if( _player->GetAchievementInterface()->HasAchievements() )
		_player->CopyAndSendDelayedPacket(_player->GetAchievementInterface()->BuildAchievementData());

	if(enter_world && !_player->GetMapMgr())
	{
		plr->AddToWorld();
	}

	objmgr.AddPlayer(_player);
}

bool ChatHandler::HandleRenameCommand(const char * args, WorldSession * m_session)
{
	// prevent buffer overflow
	if(strlen(args) > 100)
		return false;

	char name1[100];
	char name2[100];

	if(sscanf(args, "%s %s", name1, name2) != 2)
		return false;

	if(VerifyName(name2, strlen(name2)) == false)
	{
		RedSystemMessage(m_session, "That name is invalid or contains invalid characters.");
		return true;
	}

	string new_name = name2;
	PlayerInfo * pi = objmgr.GetPlayerInfoByName(name1);
	if(pi == 0)
	{
		RedSystemMessage(m_session, "Player not found with this name.");
		return true;
	}

	if( objmgr.GetPlayerInfoByName(new_name.c_str()) != NULL )
	{
		RedSystemMessage(m_session, "Player found with this name in use already.");
		return true;
	}

	objmgr.RenamePlayerInfo(pi, pi->name, new_name.c_str());

	free(pi->name);
	pi->name = strdup(new_name.c_str());

	// look in world for him
	PlayerPointer plr = objmgr.GetPlayer(pi->guid);
	if(plr != 0)
	{
		plr->SetName(new_name);
		BlueSystemMessageToPlr(plr, "%s changed your name to '%s'.", m_session->GetPlayer()->GetName(), new_name.c_str());
		plr->SaveToDB(false);
	}
	else
	{
		CharacterDatabase.WaitExecute("UPDATE characters SET name = '%s' WHERE guid = %u", CharacterDatabase.EscapeString(new_name).c_str(), (uint32)pi->guid);
	}

	GreenSystemMessage(m_session, "Changed name of '%s' to '%s'.", name1, name2);
	sGMLog.writefromsession(m_session, "renamed character %s (GUID: %u) to %s", name1, pi->guid, name2);
	sPlrLog.writefromsession(m_session, "GM renamed character %s (GUID: %u) to %s", name1, pi->guid, name2);
	return true;
}

void WorldSession::HandleAlterAppearance(WorldPacket & recv_data)
{
	/*
	data << uint32(0) // ok
	data << uint32(1) // not enough money
	data << uint32(2) // you must be sitting on the barber's chair
	
	GameObjectPointer barberChair = _player->GetMapMgr()->GetInterface()->GetGameObjectNearestCoords(_player->GetPositionX(), _player->GetPositionY(), _player->GetPositionZ(), BARBERCHAIR_ID);
	if(!barberChair || _player->GetStandState() != STANDSTATE_SIT_MEDIUM_CHAIR)
	{
		data << uint32(2);
		SendPacket(&data);
		return;
	}
	*/
	

	DEBUG_LOG("WORLD: CMSG_ALTER_APPEARANCE");
	CHECK_PACKET_SIZE(recv_data, 12);
	
	uint32 hair, colour, facialhair;
	recv_data >> hair >> colour >> facialhair;
	
	BarberShopStyleEntry * Hair = dbcBarberShopStyle.LookupEntry(hair);
	BarberShopStyleEntry * facialHair = dbcBarberShopStyle.LookupEntry(facialhair);
	if(!facialHair || !Hair)
		return;

	uint8 newHair = Hair->hair_id;
	uint8 newFacialHair = facialHair->hair_id;	
	uint32 level = _player->getLevel();
	float cost = 0;
	uint8 oldHair = _player->GetByte(PLAYER_BYTES, 2);
	uint8 oldColour = _player->GetByte(PLAYER_BYTES, 3);
	uint8 oldFacialHair = _player->GetByte(PLAYER_BYTES_2, 0);

	if(oldHair == newHair && oldColour == (uint8)colour && oldFacialHair == newFacialHair)
		return;

	if(level >= 100)
		level = 100;

	gtFloat *cutcosts = dbcBarberShopPrices.LookupEntry(level - 1);
	if(!cutcosts)
		return;

	WorldPacket data(SMSG_BARBER_SHOP_RESULT, 4);

	if(oldHair != newHair)
		cost += cutcosts->val;

    if((oldColour != colour) && (oldHair == newHair))
        cost += cutcosts->val * 0.5f;

    if(oldFacialHair != newFacialHair)
        cost += cutcosts->val * 0.75f;

	if(_player->GetUInt32Value(PLAYER_FIELD_COINAGE) < cost)
	{		
		data << uint32(1); // not enough money
		SendPacket(&data);
		return;
	}

	data << uint32(0); // ok
	SendPacket(&data);
	_player->ModUnsigned32Value(PLAYER_FIELD_COINAGE, -(int32)cost);
	_player->SetByte(PLAYER_BYTES, 2, newHair);
	_player->SetByte(PLAYER_BYTES, 3, (uint8)colour);
	_player->SetByte(PLAYER_BYTES_2, 0, newFacialHair);
	_player->SetStandState(0);
}

