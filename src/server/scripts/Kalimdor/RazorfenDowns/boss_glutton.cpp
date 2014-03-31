/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2011-2014 ArkCORE <http://www.arkania.net/>
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

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "razorfen_downs.h"

enum Say
{
    SAY_AGGRO               = 0,
    SAY_SLAY                = 1,
    SAY_HP50                = 2,
    SAY_HP15                = 3
};

enum Spells
{
    SPELL_DISEASE_CLOUD     = 12627,
    SPELL_FRENZY            = 12795
};

class boss_glutton : public CreatureScript
{
public:
    boss_glutton() : CreatureScript("boss_glutton") { }

    struct boss_gluttonAI : public BossAI
    {
        boss_gluttonAI(Creature* creature) : BossAI(creature, DATA_GLUTTON)
        {
            hp15 = false;
        }

        void Reset() 
        {
            _Reset();
            hp50 = false;
            hp15 = false;
        }

        void EnterCombat(Unit* /*who*/) 
        {
            _EnterCombat();
            Talk(SAY_AGGRO);
        }

        void KilledUnit(Unit* /*victim*/) 
        {
            Talk(SAY_SLAY);
        }

        void JustDied(Unit* /*killer*/) 
        {
            _JustDied();
        }

        void UpdateAI(const uint32 /*diff*/) 
        {
            if (!UpdateVictim())
                return;

            if (!hp50 && HealthBelowPct(50))
            {
                Talk(SAY_HP50);
                hp50 = true;
            }

            if (!hp15 && HealthBelowPct(15))
            {
                Talk(SAY_HP15);
                DoCast(me, SPELL_FRENZY);
                hp15 = true;
            }

            DoMeleeAttackIfReady();
        }

    private:
        bool hp50;
        bool hp15;
    };

    CreatureAI* GetAI(Creature* creature) const 
    {
        return new boss_gluttonAI(creature);
    }
};

void AddSC_boss_glutton()
{
    new boss_glutton();
}
