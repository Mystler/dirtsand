/******************************************************************************
 * This file is part of dirtsand.                                             *
 *                                                                            *
 * dirtsand is free software: you can redistribute it and/or modify           *
 * it under the terms of the GNU General Public License as published by       *
 * the Free Software Foundation, either version 3 of the License, or          *
 * (at your option) any later version.                                        *
 *                                                                            *
 * dirtsand is distributed in the hope that it will be useful,                *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with dirtsand.  If not, see <http://www.gnu.org/licenses/>.          *
 ******************************************************************************/

#include "GameServer_Private.h"
#include "errors.h"

hostmap_t s_gameHosts;
pthread_mutex_t s_gameHostMutex;

#define SEND_REPLY(msg, result) \
    msg->m_client->m_channel.putMessage(result)

void dm_game_shutdown(GameHost_Private* host)
{
    pthread_mutex_lock(&host->m_clientMutex);
    std::list<GameClient_Private*>::iterator client_iter;
    for (client_iter = host->m_clients.begin(); client_iter != host->m_clients.end(); ++client_iter)
        DS::CloseSock((*client_iter)->m_sock);
    pthread_mutex_unlock(&host->m_clientMutex);

    bool complete = false;
    for (int i=0; i<50 && !complete; ++i) {
        pthread_mutex_lock(&host->m_clientMutex);
        size_t alive = host->m_clients.size();
        pthread_mutex_unlock(&host->m_clientMutex);
        if (alive == 0)
            complete = true;
        usleep(100000);
    }
    if (!complete)
        fprintf(stderr, "[Game] Clients didn't die after 5 seconds!\n");

    pthread_mutex_lock(&s_gameHostMutex);
    hostmap_t::iterator host_iter = s_gameHosts.begin();
    while (host_iter != s_gameHosts.end()) {
        if (host_iter->second == host)
            s_gameHosts.erase(host_iter);
        else
            ++host_iter;
    }
    pthread_mutex_unlock(&s_gameHostMutex);

    pthread_mutex_destroy(&host->m_clientMutex);
    delete host;
}

void dm_game_cleanup(GameHost_Private* host)
{
    //TODO: shutdown this host if no clients connect within a time limit
}

void* dm_gameHost(void* hostp)
{
    GameHost_Private* host = reinterpret_cast<GameHost_Private*>(hostp);

    for ( ;; ) {
        DS::FifoMessage msg = host->m_channel.getMessage();
        try {
            switch (msg.m_messageType) {
            case e_GameShutdown:
                dm_game_shutdown(host);
                return 0;
            case e_GameCleanup:
                dm_game_cleanup(host);
                break;
            default:
                /* Invalid message...  This shouldn't happen */
                DS_DASSERT(0);
                break;
            }
        } catch (DS::AssertException ex) {
            fprintf(stderr, "[Game] Assertion failed at %s:%ld:  %s\n",
                    ex.m_file, ex.m_line, ex.m_cond);
            if (msg.m_payload) {
                // Keep clients from blocking on a reply
                //SEND_REPLY(reinterpret_cast<Game_ClientMessage*>(msg.m_payload),
                //           DS::e_NetInternalError);
            }
        }
    }

    dm_game_shutdown(host);
    return 0;
}

GameHost_Private* start_game_host(const DS::Uuid& ageId)
{
    GameHost_Private* host = new GameHost_Private();
    pthread_mutex_init(&host->m_clientMutex, 0);
    host->m_instanceId = ageId;

    pthread_t threadh;
    pthread_create(&threadh, 0, &dm_gameHost, reinterpret_cast<void*>(host));
    pthread_detach(threadh);
    return host;
}