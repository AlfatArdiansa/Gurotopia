#include "pch.hpp"
#include "tools/string.hpp"
#include "on/SetBux.hpp"
#include "gem.hpp"

void gem(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (pPeer->role < role::DEVELOPER)
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "`4You don't have permission to use this command.``" });
        return;
    }

    if (text.length() <= sizeof("gem ") - 1)
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "Usage: /gem `w{give|take} {player name} {amount}``" });
        return;
    }
    std::string args{ text.substr(sizeof("gem ") - 1) };
    std::vector<std::string> parts = readch(args, ' ');

    if (parts.size() < 3)
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "Usage: /gem `w{give|take} {player name} {amount}``" });
        return;
    }

    const std::string& mode = parts[0];
    const std::string& target_name = parts[1];

    if (mode != "give" && mode != "take")
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "`4First argument must be `wgive`` or `wtake``.``" });
        return;
    }
    if (!number(parts[2]))
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "`4Amount must be a number.``" });
        return;
    }

    int amount = std::stoi(parts[2]);
    if (amount <= 0)
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "`4Amount must be greater than 0.``" });
        return;
    }

    bool found = false;
    peers("", PEER_ALL, [&](ENetPeer& peer)
    {
        ::peer *pTarget = static_cast<::peer*>(peer.data);
        if (pTarget->ltoken[0] == target_name)
        {
            found = true;

            ENetEvent target_event{};
            target_event.peer = &peer;

            if (mode == "give")
            {
                pTarget->gems += amount;

                packet::create(peer, false, 0, {
                    "OnConsoleMessage",
                    std::format("`2You received `w{}`` gems from `w{}``!",
                        amount, pPeer->ltoken[0]).c_str()
                });
            }
            else // take
            {
                pTarget->gems = std::max(0, pTarget->gems - amount);

                packet::create(peer, false, 0, {
                    "OnConsoleMessage",
                    std::format("`4`w{}`` gems were taken by `w{}``.",
                        amount, pPeer->ltoken[0]).c_str()
                });
            }
            on::SetBux(target_event);
        }
    });

    if (!found)
    {
        packet::create(*event.peer, false, 0, {
            "OnConsoleMessage",
            std::format("`4Player `w{}`` is not online.``", target_name).c_str()
        });
        return;
    }

    packet::create(*event.peer, false, 0, {
        "OnConsoleMessage",
        std::format("`2Successfully {} `w{}`` gems {} `w{}``.",
            (mode == "give") ? "gave" : "took", amount,
            (mode == "give") ? "to" : "from", target_name).c_str()
    });
}
