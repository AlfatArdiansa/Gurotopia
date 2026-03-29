#include "pch.hpp"
#include "tools/string.hpp"
#include "give.hpp"

void give(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (pPeer->role < role::DEVELOPER)
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "`4You don't have permission to use this command.``" });
        return;
    }

    if (text.length() <= sizeof("give ") - 1)
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "Usage: /give `w{player name} {item id} {amount}``" });
        return;
    }
    std::string args{ text.substr(sizeof("give ") - 1) };
    std::vector<std::string> parts = readch(args, ' ');

    if (parts.size() < 3)
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "Usage: /give `w{player name} {item id} {amount}``" });
        return;
    }

    const std::string& target_name = parts[0];
    if (!number(parts[1]) || !number(parts[2]))
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "`4Item ID and amount must be numbers.``" });
        return;
    }

    short item_id = (short)std::stoi(parts[1]);
    short amount  = (short)std::stoi(parts[2]);

    if (item_id < 0 || item_id >= (short)items.size())
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "`4Invalid item ID.``" });
        return;
    }
    if (amount <= 0 || amount > 200)
    {
        packet::create(*event.peer, false, 0, { "OnConsoleMessage", "`4Amount must be between 1 and 200.``" });
        return;
    }

    bool found = false;
    peers("", PEER_ALL, [&](ENetPeer& peer)
    {
        ::peer *pTarget = static_cast<::peer*>(peer.data);
        if (pTarget->ltoken[0] == target_name)
        {
            found = true;

            // @note we need a temporary event to call modify_item_inventory for the target
            ENetEvent target_event{};
            target_event.peer = &peer;

            modify_item_inventory(target_event, ::slot(item_id, amount));
            send_inventory_state(target_event);

            packet::create(peer, false, 0, {
                "OnConsoleMessage",
                std::format("`2You received `w{}`` x`w{}`` from `w{}``!",
                    items[item_id].raw_name, amount, pPeer->ltoken[0]).c_str()
            });
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
        std::format("`2Gave `w{}`` x`w{}`` to `w{}``.",
            items[item_id].raw_name, amount, target_name).c_str()
    });
}
