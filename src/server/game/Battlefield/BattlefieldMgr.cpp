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
#include "BattlefieldMgr.h"
#include "Zones/BattlefieldWG.h"
#include "ObjectMgr.h"
#include "Player.h"

bool BattlefieldQueue::HasEnoughSpace(Player* plr)
{
    if(plr->GetTeamId() == TEAM_ALLIANCE)
    {
        if(m_inQueueAlliance.size() <= 120)
            return true;
    }
    else if(plr->GetTeamId() == TEAM_HORDE)
    {
        if(m_inQueueHorde.size() <= 120)
            return true;
    }

    return false;
}

bool BattlefieldQueue::IsPlayerQueued(Player* plr)
{
    if(plr->GetTeamId() == TEAM_ALLIANCE)
    {
        for(PlayerQueue::iterator itr = m_inQueueAlliance.begin(); itr != m_inQueueAlliance.end();++itr)
        {
            if ((*itr) == plr)
                return true;
        }
    }
    else if(plr->GetTeamId() == TEAM_HORDE)
    {
        for(PlayerQueue::iterator itr = m_inQueueHorde.begin(); itr != m_inQueueHorde.end();++itr)
        {
            if ((*itr) == plr)
                return true;
        }
    }

    return false;
}

void BattlefieldQueue::AddPlayerToQueue(Player* plr)
{
    if(plr->GetTeamId() == TEAM_ALLIANCE)
        m_inQueueAlliance.push_back(plr);
    else if(plr->GetTeamId() == TEAM_HORDE)
        m_inQueueHorde.push_back(plr);
}

void BattlefieldQueue::RemovePlayerFromQueue(Player* plr)
{
    if(plr->GetTeamId() == TEAM_ALLIANCE)
        m_inQueueAlliance.remove(plr);
    else if(plr->GetTeamId() == TEAM_HORDE)
        m_inQueueHorde.remove(plr);
}

BattlefieldMgr::BattlefieldMgr()
{
    m_UpdateTimer = 0;
    //sLog->outDebug(LOG_FILTER_BATTLEFIELD, "Instantiating BattlefieldMgr");
}

BattlefieldMgr::~BattlefieldMgr()
{
    //sLog->outDebug(LOG_FILTER_BATTLEFIELD, "Deleting BattlefieldMgr");
    for (BattlefieldSet::iterator itr = m_BattlefieldSet.begin(); itr != m_BattlefieldSet.end(); ++itr)
        delete *itr;
}

void BattlefieldMgr::InitBattlefield()
{
    Battlefield* pBf = new BattlefieldWG;
    // respawn, init variables
    if (!pBf->SetupBattlefield())
    {
        sLog->outString();
        sLog->outString("Battlefield : Wintergrasp init failed.");
        delete pBf;
    }
    else
    {
        m_BattlefieldSet.push_back(pBf);
        BattlefieldQueue* pWgQueue = new BattlefieldQueue(pBf->GetBattleId());
        m_queueMap[pBf->GetGUID()] = pWgQueue;
        sLog->outString("Battlefield : Wintergrasp successfully initiated.");
    }

    /* For Cataclysm: Tol Barad
       pBf = new BattlefieldTB;
       // respawn, init variables
       if(!pBf->SetupBattlefield())
       {
       sLog->outDebug(LOG_FILTER_BATTLEFIELD, "Battlefield : Tol Barad init failed.");
       delete pBf;
       }
       else
       {
       m_BattlefieldSet.push_back(pBf);
       sLog->outDebug(LOG_FILTER_BATTLEFIELD, "Battlefield : Tol Barad successfully initiated.");
       } */
}

void BattlefieldMgr::AddZone(uint32 zoneid, Battlefield *handle)
{
    m_BattlefieldMap[zoneid] = handle;
}

void BattlefieldMgr::HandlePlayerEnterZone(Player * player, uint32 zoneid)
{
    BattlefieldMap::iterator itr = m_BattlefieldMap.find(zoneid);
    if (itr == m_BattlefieldMap.end())
        return;

    if (itr->second->HasPlayer(player) || !itr->second->IsEnabled())
        return;

    itr->second->HandlePlayerEnterZone(player, zoneid);
    sLog->outDebug(LOG_FILTER_NETWORKIO, "Player %u entered outdoorpvp id %u", player->GetGUIDLow(), itr->second->GetTypeId());
}

void BattlefieldMgr::HandlePlayerLeaveZone(Player * player, uint32 zoneid)
{
    BattlefieldMap::iterator itr = m_BattlefieldMap.find(zoneid);
    if (itr == m_BattlefieldMap.end())
        return;

    // teleport: remove once in removefromworld, once in updatezone
    if (!itr->second->HasPlayer(player))
        return;
    itr->second->HandlePlayerLeaveZone(player, zoneid);
    sLog->outDebug(LOG_FILTER_NETWORKIO, "Player %u left outdoorpvp id %u", player->GetGUIDLow(), itr->second->GetTypeId());
}

Battlefield *BattlefieldMgr::GetBattlefieldToZoneId(uint32 zoneid)
{
    BattlefieldMap::iterator itr = m_BattlefieldMap.find(zoneid);
    if (itr == m_BattlefieldMap.end())
    {
        // no handle for this zone, return
        return NULL;
    }
    if (!itr->second->IsEnabled())
        return NULL;
    return itr->second;
}

Battlefield *BattlefieldMgr::GetBattlefieldByBattleId(uint32 battleid)
{
    for (BattlefieldSet::iterator itr = m_BattlefieldSet.begin(); itr != m_BattlefieldSet.end(); ++itr)
    {
        if ((*itr)->GetBattleId() == battleid)
            return (*itr);
    }
    return NULL;
}

Battlefield* BattlefieldMgr::GetBattlefieldByGUID(uint64 guid)
{
    for (BattlefieldSet::iterator itr = m_BattlefieldSet.begin(); itr != m_BattlefieldSet.end(); ++itr)
        if ((*itr)->GetGUID() == guid)
            return (*itr);

    return NULL;
}

void BattlefieldMgr::Update(uint32 diff)
{
    m_UpdateTimer += diff;
    if (m_UpdateTimer > BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL)
    {
        for (BattlefieldSet::iterator itr = m_BattlefieldSet.begin(); itr != m_BattlefieldSet.end(); ++itr)
            if ((*itr)->IsEnabled())
                (*itr)->Update(m_UpdateTimer);
        m_UpdateTimer = 0;
    }
}

ZoneScript *BattlefieldMgr::GetZoneScript(uint32 zoneId)
{
    BattlefieldMap::iterator itr = m_BattlefieldMap.find(zoneId);
    if (itr != m_BattlefieldMap.end())
        return itr->second;
    else
        return NULL;
}