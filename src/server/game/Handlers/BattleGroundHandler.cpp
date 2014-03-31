/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2011-2014 ArkCORE <http://www.arkania.net/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "ArenaTeamMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "ArenaTeam.h"
#include "BattlegroundMgr.h"
#include "Battleground.h"
#include "Chat.h"
#include "Language.h"
#include "Log.h"
#include "Player.h"
#include "Object.h"
#include "Opcodes.h"
#include "DisableMgr.h"
#include "Group.h"

void WorldSession::HandleBattlemasterHelloOpcode(WorldPacket& recvData)
{
    uint64 guid;
    recvData >> guid;
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_BATTLEMASTER_HELLO Message from (GUID: %u TypeId:%u)", GUID_LOPART(guid), GuidHigh2TypeId(GUID_HIPART(guid)));

    Creature* unit = GetPlayer()->GetMap()->GetCreature(guid);
    if (!unit)
        return;

    if (!unit->IsBattleMaster())                             // it's not battlemaster
        return;

    // Stop the npc if moving
    unit->StopMoving();

    BattlegroundTypeId bgTypeId = sBattlegroundMgr->GetBattleMasterBG(unit->GetEntry());

    if (!_player->GetBGAccessByLevel(bgTypeId))
    {
                                                            // temp, must be gossip message...
        SendNotification(LANG_YOUR_BG_LEVEL_REQ_ERROR);
        return;
    }

    SendBattleGroundList(guid, bgTypeId);
}

void WorldSession::SendBattleGroundList(uint64 guid, BattlegroundTypeId bgTypeId)
{
    WorldPacket data;
    sBattlegroundMgr->BuildBattlegroundListPacket(&data, guid, _player, bgTypeId);
    SendPacket(&data);
}

void WorldSession::HandleBattlemasterJoinOpcode(WorldPacket & recvData)
{
    uint32 bgTypeId_;
    uint32 instanceId;
    bool asGroup;
    bool isPremade = false;
    Group* grp;
    ObjectGuid guid;

    recvData >> instanceId;                 // Instance Id
    guid[2] = recvData.ReadBit();
    guid[0] = recvData.ReadBit();
    guid[3] = recvData.ReadBit();
    guid[1] = recvData.ReadBit();
    guid[5] = recvData.ReadBit();
    asGroup = recvData.ReadBit();           // As Group
    guid[4] = recvData.ReadBit();
    guid[6] = recvData.ReadBit();
    guid[7] = recvData.ReadBit();

    recvData.ReadByteSeq(guid[2]);
    recvData.ReadByteSeq(guid[6]);
    recvData.ReadByteSeq(guid[4]);
    recvData.ReadByteSeq(guid[3]);
    recvData.ReadByteSeq(guid[7]);
    recvData.ReadByteSeq(guid[0]);
    recvData.ReadByteSeq(guid[5]);
    recvData.ReadByteSeq(guid[1]);

    //extract from guid
    bgTypeId_ = GUID_LOPART(uint64(guid));

    if (!sBattlemasterListStore.LookupEntry(bgTypeId_))
    {
        sLog->outError("Battleground: invalid bgtype (%u) received. possible cheater? player guid %u", bgTypeId_, _player->GetGUIDLow());
        return;
    }

    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, bgTypeId_, NULL))
    {
        ChatHandler(this).PSendSysMessage(LANG_BG_DISABLED);
        return;
    }
    BattlegroundTypeId bgTypeId = BattlegroundTypeId(bgTypeId_);

    //sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_BATTLEMASTER_JOIN Message from (GUID:"UI64FMTD" TypeId:%u)", guid, bgTypeId_);

    // can do this, since it's battleground, not arena
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, 0);
    BattlegroundQueueTypeId bgQueueTypeIdRandom = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_RB, 0);

    // ignore if player is already in BG
    if (_player->InBattleground())
        return;

    // get bg instance or bg template if instance not found
    Battleground* bg = NULL;
    if (instanceId)
        bg = sBattlegroundMgr->GetBattlegroundThroughClientInstance(instanceId, bgTypeId);

    if (!bg)
        bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);

    if (!bg && !sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId))
    {
        sLog->outError("Battleground: no available bg / template found");
        return;
    }

    // expected bracket entry
    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), _player->getLevel());
    if (!bracketEntry)
        return;

    GroupJoinBattlegroundResult err = ERR_BATTLEGROUND_NONE;

    // check queue conditions
    if (!asGroup)
    {
        if (GetPlayer()->isUsingLfg())
        {
            WorldPacket data;
            sBattlegroundMgr->BuildStatusFailedPacket(&data, bg, _player, 0, ERR_LFG_CANT_USE_BATTLEGROUND);
            GetPlayer()->GetSession()->SendPacket(&data);
            return;
        }

        // check Deserter debuff
        if (!_player->CanJoinToBattleground())
        {
            WorldPacket data;
            sBattlegroundMgr->BuildStatusFailedPacket(&data, bg, _player, 0, ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS);
            _player->GetSession()->SendPacket(&data);
            return;
        }

        if (_player->GetBattlegroundQueueIndex(bgQueueTypeIdRandom) < PLAYER_MAX_BATTLEGROUND_QUEUES)
        {
            // player is already in random queue
            WorldPacket data;
            sBattlegroundMgr->BuildStatusFailedPacket(&data, bg, _player, 0, ERR_IN_RANDOM_BG);
            _player->GetSession()->SendPacket(&data);
            return;
        }

        if (_player->InBattlegroundQueue() && bgTypeId == BATTLEGROUND_RB)
        {
            // player is already in queue, can't start random queue
            WorldPacket data;
            sBattlegroundMgr->BuildStatusFailedPacket(&data, bg, _player, 0, ERR_IN_NON_RANDOM_BG);
            _player->GetSession()->SendPacket(&data);
            return;
        }

        // check if already in queue
        if (_player->GetBattlegroundQueueIndex(bgQueueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            // player is already in this queue
            return;

        // check if has free queue slots
        if (!_player->HasFreeBattlegroundQueueId())
        {
            WorldPacket data;
            sBattlegroundMgr->BuildStatusFailedPacket(&data, bg, _player, 0, ERR_BATTLEGROUND_TOO_MANY_QUEUES);
            _player->GetSession()->SendPacket(&data);
            return;
        }

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        GroupQueueInfo* ginfo = bgQueue.AddGroup(_player, NULL, bgTypeId, bracketEntry, 0, false, isPremade, 0, 0);

        uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
        uint32 queueSlot = _player->AddBattlegroundQueueId(bgQueueTypeId);

        // add joined time data
        _player->AddBattlegroundQueueJoinTime(bgTypeId, ginfo->JoinTime);

        WorldPacket data; // send status packet (in queue)
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, _player, queueSlot, STATUS_WAIT_QUEUE, avgTime, ginfo->JoinTime, ginfo->ArenaType);
        SendPacket(&data);

        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: player joined queue for bg queue type %u bg type %u: GUID %u, NAME %s",
                       bgQueueTypeId, bgTypeId, _player->GetGUIDLow(), _player->GetName().c_str());
    }
    else
    {
        grp = _player->GetGroup();

        if (!grp)
            return;

        if (grp->GetLeaderGUID() != _player->GetGUID())
            return;

        err = grp->CanJoinBattlegroundQueue(bg, bgQueueTypeId, 0, bg->GetMaxPlayersPerTeam(), false, 0);
        isPremade = (grp->GetMembersCount() >= bg->GetMinPlayersPerTeam());

        // if we're here, then the conditions to join a bg are met. We can proceed in joining.
        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        GroupQueueInfo* ginfo = NULL;
        uint32 avgTime = 0;

        if (!err)
        {
            sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: the following players are joining as group:");
            ginfo = bgQueue.AddGroup(_player, grp, bgTypeId, bracketEntry, 0, false, isPremade, 0, 0);
            avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
        }

        for (GroupReference* itr = grp->GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue; // this should never happen

            if (err)
            {
                WorldPacket data;
                sBattlegroundMgr->BuildStatusFailedPacket(&data, bg, _player, 0, err);
                member->GetSession()->SendPacket(&data);
                continue;
            }

            // add to queue
            uint32 queueSlot = member->AddBattlegroundQueueId(bgQueueTypeId);

            // add joined time data
            member->AddBattlegroundQueueJoinTime(bgTypeId, ginfo->JoinTime);

            WorldPacket data; // send status packet (in queue)
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, member, queueSlot, STATUS_WAIT_QUEUE, avgTime, ginfo->JoinTime, ginfo->ArenaType);
            member->GetSession()->SendPacket(&data);

            sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: player joined queue for bg queue type %u bg type %u: GUID %u, NAME %s", 
                bgQueueTypeId, bgTypeId, member->GetGUIDLow(), member->GetName().c_str());
        }
        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: group end");
    }

    sBattlegroundMgr->ScheduleQueueUpdate(0, 0, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());
}

void WorldSession::HandleBattlegroundPlayerPositionsOpcode(WorldPacket & /*recvData*/)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_BATTLEGROUND_PLAYER_POSITIONS Message");

    Battleground* bg = _player->GetBattleground();
    if (!bg)                                                 // can't be received if player not in battleground
        return;

    uint32 acount = 0;
    uint32 hcount = 0;
    Player* aplr = NULL;
    Player* hplr = NULL;

    if (uint64 guid = bg->GetFlagPickerGUID(TEAM_ALLIANCE))
    {
        aplr = ObjectAccessor::FindPlayer(guid);
        if (aplr)
            ++acount;
    }

    if (uint64 guid = bg->GetFlagPickerGUID(TEAM_HORDE))
    {
        hplr = ObjectAccessor::FindPlayer(guid);
        if (hplr)
            ++hcount;
    }

    ObjectGuid aguid = aplr ? aplr->GetGUID() : 0;
    ObjectGuid hguid = hplr ? hplr->GetGUID() : 0;

    WorldPacket data(SMSG_BATTLEFIELD_PLAYER_POSITIONS);

    data.WriteBits(acount, 22);
    for (uint8 i = 0; i < acount; i++)
    {
        data.WriteBit(aguid[3]);
        data.WriteBit(aguid[5]);
        data.WriteBit(aguid[1]);
        data.WriteBit(aguid[6]);
        data.WriteBit(aguid[7]);
        data.WriteBit(aguid[0]);
        data.WriteBit(aguid[2]);
        data.WriteBit(aguid[4]);
    }

    data.WriteBits(hcount, 22);
    for (uint8 i = 0; i < hcount; i++)
    {
        data.WriteBit(hguid[6]);
        data.WriteBit(hguid[5]);
        data.WriteBit(hguid[4]);
        data.WriteBit(hguid[7]);
        data.WriteBit(hguid[2]);
        data.WriteBit(hguid[1]);
        data.WriteBit(hguid[0]);
        data.WriteBit(hguid[3]);
    }

    data.FlushBits();

    for (uint8 i = 0; i < hcount; i++)
    {
        data.WriteByteSeq(hguid[2]);
        data.WriteByteSeq(hguid[1]);
        data << float(hplr->GetPositionY());
        data.WriteByteSeq(hguid[5]);
        data.WriteByteSeq(hguid[4]);
        data.WriteByteSeq(hguid[7]);
        data.WriteByteSeq(hguid[0]);
        data.WriteByteSeq(hguid[6]);
        data.WriteByteSeq(hguid[3]);
        data << float(hplr->GetPositionX());
    }

    for (uint8 i = 0; i < acount; i++)
    {
        data.WriteByteSeq(aguid[6]);
        data << float(aplr->GetPositionX());
        data.WriteByteSeq(aguid[5]);
        data.WriteByteSeq(aguid[3]);
        data << float(aplr->GetPositionY());
        data.WriteByteSeq(aguid[1]);
        data.WriteByteSeq(aguid[7]);
        data.WriteByteSeq(aguid[0]);
        data.WriteByteSeq(aguid[2]);
        data.WriteByteSeq(aguid[4]);
    }

    SendPacket(&data);
}

void WorldSession::HandlePVPLogDataOpcode(WorldPacket & /*recvData*/)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_PVP_LOG_DATA Message");

    Battleground* bg = _player->GetBattleground();
    if (!bg)
        return;

    // Prevent players from sending BuildPvpLogDataPacket in an arena except for when sent in BattleGround::EndBattleGround.
    if (bg->isArena())
        return;

    WorldPacket data;
    sBattlegroundMgr->BuildPvpLogDataPacket(&data, bg);
    SendPacket(&data);

    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Sent SMSG_PVP_LOG_DATA Message");
}

void WorldSession::HandleBattlefieldListOpcode(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_BATTLEFIELD_LIST Message");

    uint32 bgTypeId;
    recvData >> bgTypeId;                                  // id from DBC

    BattlemasterListEntry const* bl = sBattlemasterListStore.LookupEntry(bgTypeId);
    if (!bl)
    {
        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "BattlegroundHandler: invalid bgtype (%u) with player (Name: %s, GUID: %u) received.", bgTypeId, _player->GetName().c_str(), _player->GetGUIDLow());
        return;
    }

    WorldPacket data;
    sBattlegroundMgr->BuildBattlegroundListPacket(&data, 0, _player, BattlegroundTypeId(bgTypeId));
    SendPacket(&data);
}

void WorldSession::HandleBattleFieldPortOpcode(WorldPacket &recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_BATTLEFIELD_PORT Message");

    uint32 time;
    uint32 queueSlot;
    uint32 unk;                         // guessed
    uint8 action;                       // enter battle 0x1, leave queue 0x0
    ObjectGuid guid;

    recvData >> time;
    recvData >> queueSlot;
    recvData >> unk;                    // packet.ReadEntryWithName<Int32>(StoreNameType.Battleground, "BGType"); 

    guid[0] = recvData.ReadBit();
    guid[1] = recvData.ReadBit();
    guid[5] = recvData.ReadBit();
    guid[6] = recvData.ReadBit();
    guid[7] = recvData.ReadBit();
    guid[4] = recvData.ReadBit();
    guid[3] = recvData.ReadBit();
    guid[2] = recvData.ReadBit();

    action = recvData.ReadBit() ? 1 : 0;

    recvData.ReadByteSeq(guid[1]);
    recvData.ReadByteSeq(guid[3]);
    recvData.ReadByteSeq(guid[5]);
    recvData.ReadByteSeq(guid[7]);
    recvData.ReadByteSeq(guid[0]);
    recvData.ReadByteSeq(guid[2]);
    recvData.ReadByteSeq(guid[6]);
    recvData.ReadByteSeq(guid[4]);

    if (!_player->InBattlegroundQueue())
    {
        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "BattlegroundHandler: Invalid CMSG_BATTLEFIELD_PORT received from player (Name: %s, GUID: %u), he is not in bg_queue.", _player->GetName().c_str(), _player->GetGUIDLow());
        return;
    }

    //get GroupQueueInfo from BattlegroundQueue
    BattlegroundQueueTypeId bgQueueTypeId = _player->GetBattlegroundQueueTypeId(queueSlot);

    if (bgQueueTypeId == BATTLEGROUND_QUEUE_NONE)
    {
         sLog->outError("BattlegroundHandler: invalid queueSlot (%u) received.", queueSlot);
         return;
    }

    BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(bgQueueTypeId);
    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);

    //we must use temporary variable, because GroupQueueInfo pointer can be deleted in BattlegroundQueue::RemovePlayer() function
    GroupQueueInfo ginfo;
    if (!bgQueue.GetPlayerGroupInfoData(_player->GetGUID(), &ginfo))
    {
        sLog->outError("BattlegroundHandler: itrplayerstatus not found.");
        return;
    }
    // if action == 1, then instanceId is required
    if (!ginfo.IsInvitedToBGInstanceGUID && action == 1)
    {
        sLog->outError("BattlegroundHandler: instance not found.");
        return;
    }

    // ::BGTemplateId returns BATTLEGROUND_AA when it is arena queue.
    // Do instance id search as there is no AA bg instances.
    Battleground* bg = sBattlegroundMgr->GetBattleground(ginfo.IsInvitedToBGInstanceGUID, bgTypeId == BATTLEGROUND_AA ? BATTLEGROUND_TYPE_NONE : bgTypeId);

    // bg template might and must be used in case of leaving queue, when instance is not created yet
    if (!bg && action == 0)
        bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bg)
    {
        sLog->outError("BattlegroundHandler: bg_template not found for instance id %u type id %u.", ginfo.IsInvitedToBGInstanceGUID, bgTypeId);
        return;
    }

    // get real bg type
    bgTypeId = bg->GetTypeID();

    // expected bracket entry
    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), _player->getLevel());
    if (!bracketEntry)
        return;

    //some checks if player isn't cheating - it is not exactly cheating, but we cannot allow it
    if (action == 1 && ginfo.ArenaType == 0)
    {
        //if player is trying to enter battleground (not arena!) and he has deserter debuff, we must just remove him from queue
        if (!_player->CanJoinToBattleground())
        {
            //send bg command result to show nice message
            WorldPacket data2;
            sBattlegroundMgr->BuildStatusFailedPacket(&data2, bg, _player, 0, ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS);
            _player->GetSession()->SendPacket(&data2);
            action = 0;
            sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: player %s (%u) has a deserter debuff, do not port him to battleground!", _player->GetName().c_str(), _player->GetGUIDLow());
        }
        //if player don't match battleground max level, then do not allow him to enter! (this might happen when player leveled up during his waiting in queue
        if (_player->getLevel() > bg->GetMaxLevel())
        {
            sLog->outError("Battleground: Player %s (%u) has level (%u) higher than maxlevel (%u) of battleground (%u)! Do not port him to battleground!",
                _player->GetName().c_str(), _player->GetGUIDLow(), _player->getLevel(), bg->GetMaxLevel(), bg->GetTypeID());
            action = 0;
        }
    }

    WorldPacket data;
    switch (action)
    {
        case 1:                                         // port to battleground
            if (!_player->IsInvitedForBattlegroundQueueType(bgQueueTypeId))
                return;                                 // cheating?

            if (!_player->InBattleground())
                _player->SetBattlegroundEntryPoint();

            // resurrect the player
            if (!_player->IsAlive())
            {
                _player->ResurrectPlayer(1.0f);
                _player->SpawnCorpseBones();
            }
                // there is here like in MapManager.cpp as CanPlayerEnter. Dismounting/removing all mount an auras
                _player->Dismount();
                _player->RemoveAurasByType(SPELL_AURA_MOUNTED);
                _player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);

            // stop taxi flight at port
            if (_player->IsInFlight())
            {
                _player->GetMotionMaster()->MovementExpired();
                _player->CleanupAfterTaxiFlight();
            }

            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, _player, queueSlot, STATUS_IN_PROGRESS, _player->GetBattlegroundQueueJoinTime(bgTypeId), bg->GetElapsedTime(), bg->GetArenaType());
            _player->GetSession()->SendPacket(&data);
            // remove battleground queue status from BGmgr
            bgQueue.RemovePlayer(_player->GetGUID(), false);
            // this is still needed here if battleground "jumping" shouldn't add deserter debuff
            // also this is required to prevent stuck at old battleground after SetBattlegroundId set to new
            if (Battleground* currentBg = _player->GetBattleground())
                currentBg->RemovePlayerAtLeave(_player->GetGUID(), false, true);

            // set the destination instance id
            _player->SetBattlegroundId(bg->GetInstanceID(), bgTypeId);
            // set the destination team
            _player->SetBGTeam(ginfo.Team);
            // bg->HandleBeforeTeleportToBattleground(_player);
            sBattlegroundMgr->SendToBattleground(_player, ginfo.IsInvitedToBGInstanceGUID, bgTypeId);
            // add only in HandleMoveWorldPortAck()
            // bg->AddPlayer(_player, team);
            sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: player %s (%u) joined battle for bg %u, bgtype %u, queue type %u.", _player->GetName().c_str(), _player->GetGUIDLow(), bg->GetInstanceID(), bg->GetTypeID(), bgQueueTypeId);
            break;
        case 0:                                         // leave queue
            sLog->outError("Battleground port: Leave queue called for queueslot: %u", queueSlot);
            if (bg->isArena() && bg->GetStatus() > STATUS_WAIT_QUEUE) // Prevent exploit.
                return;

            // if player leaves rated arena match before match start, it is counted as he played but he lost
            if (ginfo.IsRated && ginfo.IsInvitedToBGInstanceGUID)
            {
                ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ginfo.Team);
                if (at)
                {
                    sLog->outDebug(LOG_FILTER_BATTLEGROUND, "UPDATING memberLost's personal arena rating for %u by opponents rating: %u, because he has left queue!", GUID_LOPART(_player->GetGUID()), ginfo.OpponentsTeamRating);
                    at->MemberLost(_player, ginfo.OpponentsMatchmakerRating);
                    at->SaveToDB();
                }
            }

            _player->RemoveBattlegroundQueueId(bgQueueTypeId);  // must be called this way, because if you move this call to queue->removeplayer, it causes bugs
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, _player, queueSlot, STATUS_NONE, _player->GetBattlegroundQueueJoinTime(bgTypeId), 0, 0);
            bgQueue.RemovePlayer(_player->GetGUID(), true);
            // player left queue, we should update it - do not update Arena Queue
            if (ginfo.ArenaType == 0)
                sBattlegroundMgr->ScheduleQueueUpdate(ginfo.ArenaMatchmakerRating, ginfo.ArenaType, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());
            SendPacket(&data);
            sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: player %s (%u) left queue for bgtype %u, queue type %u.", _player->GetName().c_str(), _player->GetGUIDLow(), bg->GetTypeID(), bgQueueTypeId);
            break;
        default:
            sLog->outError("Battleground port: unknown action %u", action);
            break;
    }
}

void WorldSession::HandleBattlefieldLeaveOpcode(WorldPacket&  /*recvData*/)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_BATTLEFIELD_LEAVE Message");

    // not allow leave battleground in combat
    if (_player->IsInCombat())
        if (Battleground* bg = _player->GetBattleground())
            if (bg->GetStatus() != STATUS_WAIT_LEAVE)
                return;

    _player->LeaveBattleground();
}

void WorldSession::HandleBattlefieldStatusOpcode(WorldPacket & /*recvData*/)
{
    // empty opcode
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_BATTLEFIELD_STATUS Message");

    WorldPacket data;
    // we must update all queues here
    Battleground* bg = NULL;
    for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
    {
        BattlegroundQueueTypeId bgQueueTypeId = _player->GetBattlegroundQueueTypeId(i);
        if (!bgQueueTypeId)
            continue;
        BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(bgQueueTypeId);
        uint8 arenaType = BattlegroundMgr::BGArenaType(bgQueueTypeId);
        if (bgTypeId == _player->GetBattlegroundTypeId())
        {
            bg = _player->GetBattleground();
            //i cannot check any variable from player class because player class doesn't know if player is in 2v2 / 3v3 or 5v5 arena
            //so i must use bg pointer to get that information
            if (bg && bg->GetArenaType() == arenaType)
            {
                // this line is checked, i only don't know if GetElapsedTime is changing itself after bg end!
                // send status in Battleground
                sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, GetPlayer(), i, STATUS_IN_PROGRESS, _player->GetBattlegroundQueueJoinTime(bgTypeId), bg->GetElapsedTime(), arenaType);
                SendPacket(&data);
                continue;
            }
        }
        //we are sending update to player about queue - he can be invited there!
        //get GroupQueueInfo for queue status
        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        GroupQueueInfo ginfo;
        if (!bgQueue.GetPlayerGroupInfoData(_player->GetGUID(), &ginfo))
            continue;
        if (ginfo.IsInvitedToBGInstanceGUID)
        {
            bg = sBattlegroundMgr->GetBattleground(ginfo.IsInvitedToBGInstanceGUID, bgTypeId);
            if (!bg)
                continue;

            // send status invited to Battleground
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, GetPlayer(), i, STATUS_WAIT_JOIN, getMSTimeDiff(getMSTime(), ginfo.RemoveInviteTime), _player->GetBattlegroundQueueJoinTime(bgTypeId), arenaType);
            SendPacket(&data);
        }
        else
        {
            bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
            if (!bg)
                continue;

            // expected bracket entry
            PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), _player->getLevel());
            if (!bracketEntry)
                continue;

            uint32 avgTime = bgQueue.GetAverageQueueWaitTime(&ginfo, bracketEntry->GetBracketId());
            // send status in Battleground Queue
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, GetPlayer(), i, STATUS_WAIT_QUEUE, avgTime, _player->GetBattlegroundQueueJoinTime(bgTypeId), arenaType);
            SendPacket(&data);
        }
    }
}

void WorldSession::HandleBattlemasterJoinArena(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_BATTLEMASTER_JOIN_ARENA");

    uint8 arenaslot;

    recvData >> arenaslot;

    // ignore if we already in BG or BG queue
    if (_player->InBattleground())
        return;



    ArenaType arenatype = ArenaTeam::GetTypeBySlot(arenaslot);
    sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Received CMSG_BATTLEMASTER_JOIN_ARENA with arenatype : %u", arenatype);

    uint32 arenaRating = 0;
    uint32 matchmakerRating = 0;

    if (!arenatype)
    {
        sLog->outError("Unknown arena type %u at HandleBattlemasterJoinArena()", arenatype);
        return;
    }

    //check existance
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
    if (!bg)
    {
        sLog->outError("Battleground: template bg (all arenas) not found");
        return;
    }

    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, BATTLEGROUND_AA, NULL))
    {
        ChatHandler(this).PSendSysMessage(LANG_ARENA_DISABLED);
        return;
    }

    BattlegroundTypeId bgTypeId = bg->GetTypeID();
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, arenatype);
    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), _player->getLevel());
    if (!bracketEntry)
    {
        sLog->outError("No bracketEntry");
        return;
    }

    Group* grp = _player->GetGroup();
    // no group found, error
    if (!grp)
        return;
    if (grp->GetLeaderGUID() != _player->GetGUID())
        return;

    uint32 ateamId = _player->GetArenaTeamId(arenaslot);
    // check real arenateam existence only here (if it was moved to group->CanJoin .. () then we would have to get it twice)
    ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ateamId);
    if (!at)
    {
        _player->GetSession()->SendNotInArenaTeamPacket(arenatype);
        return;
    }
    // get the team rating for queueing
    arenaRating = at->GetRating();
    matchmakerRating = at->GetAverageMMR(grp);
    // the arenateam id must match for everyone in the group

    if (arenaRating <= 0)
        arenaRating = 1;

    BattlegroundQueue &bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);

    uint32 avgTime = 0;
    GroupQueueInfo* ginfo = NULL;

    GroupJoinBattlegroundResult err = grp->CanJoinBattlegroundQueue(bg, bgQueueTypeId, arenatype, arenatype, true, arenaslot);
    if (!err)
    {
        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: arena join as group start");

        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: arena team id %u, leader %s queued with matchmaker rating %u for type %u", _player->GetArenaTeamId(arenaslot), _player->GetName().c_str(), matchmakerRating, arenatype);

        ginfo = bgQueue.AddGroup(_player, grp, bgTypeId, bracketEntry, arenatype, true, false, arenaRating, matchmakerRating, ateamId);
        avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
    }

    for (GroupReference* itr = grp->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member)
            continue;

        WorldPacket data;

        if (err)
        {
            sBattlegroundMgr->BuildStatusFailedPacket(&data, bg, _player, 0, err);
            member->GetSession()->SendPacket(&data);
            continue;
        }

        // add to queue
        uint32 queueSlot = member->AddBattlegroundQueueId(bgQueueTypeId);

        // send status packet (in queue)
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, member, queueSlot, STATUS_WAIT_QUEUE, avgTime, ginfo->JoinTime, arenatype);
        member->GetSession()->SendPacket(&data);
        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: player joined queue for arena as group bg queue type %u bg type %u: GUID %u, NAME %s", bgQueueTypeId, bgTypeId, member->GetGUIDLow(), member->GetName().c_str());
    }

    sBattlegroundMgr->ScheduleQueueUpdate(matchmakerRating, arenatype, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());
}

void WorldSession::HandleReportPvPAFK(WorldPacket & recvData)
{
    uint64 playerGuid;
    recvData >> playerGuid;
    Player* reportedPlayer = ObjectAccessor::FindPlayer(playerGuid);

    if (!reportedPlayer)
    {
        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "WorldSession::HandleReportPvPAFK: player not found");
        return;
    }

    sLog->outDebug(LOG_FILTER_BATTLEGROUND, "WorldSession::HandleReportPvPAFK: %s reported %s", _player->GetName().c_str(), reportedPlayer->GetName().c_str());

    reportedPlayer->ReportedAfkBy(_player);
}

void WorldSession::RequestPvpOptionsEnabled(WorldPacket& /*recvData*/)
{
  SendPvPOptionsEnable();
}

void WorldSession::SendPvPOptionsEnable()
{
    WorldPacket sendData(SMSG_PVP_OPTIONS_ENABLED,1);
    sendData.WriteBit(true);                    //unk                    
    sendData.WriteBit(true);                    //enable war games
    sendData.WriteBit(true);                    //enable battlegrounds
    sendData.WriteBit(true);                    //enable arenas
    sendData.WriteBit(true);                    //unk
    sendData.FlushBits();

    SendPacket(&sendData);
}

void WorldSession::HandleRequestRatedBGStatsOpcode(WorldPacket& /*recvData*/)
{
    // null packet

    GetPlayer()->SendRatedBGStats();
}

void WorldSession::HandleRequestPvPRewardsOpcode(WorldPacket& /*recvData*/)
{
    // null packet

    GetPlayer()->SendPvPRewards();
}

void WorldSession::HandleRequestRatedBgInfo(WorldPacket& recvData)
{
    uint8 infoUnk;
    recvData >> infoUnk;

    WorldPacket data(SMSG_BATTLEFIELD_RATED_INFO, 29);
    data << uint32(0);  //reward
    data << uint8(3);   //unk
    data << uint32(0);  //unk1
    data << uint32(0);  //unk2
    data << _player->GetCurrencyWeekCap(CURRENCY_TYPE_CONQUEST_META_BG);
    data << uint32(0);  //unk3
    data << uint32(0);  //unk4
    data << _player->GetCurrency(CURRENCY_TYPE_CONQUEST_POINTS);
    SendPacket(&data);
}


void WorldSession::HandleBattlegroundStateQuery(WorldPacket& /*recvData*/)
{
    Battleground* bg = _player->GetBattleground();
    if(bg)
    {
        bg->GetStatus();
    }
}
