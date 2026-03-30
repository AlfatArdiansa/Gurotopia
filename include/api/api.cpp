#include "pch.hpp"
#include "api.hpp"
#include "on/SetBux.hpp"
#include "tools/string.hpp"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define SOCKET_T SOCKET
    #define CLOSE_SOCKET closesocket
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #define SOCKET_T int
    #define CLOSE_SOCKET close
#endif

/* ---- simple JSON helpers (no external library) ---- */

static std::string json_get(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return "";
    ++pos;

    // skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;

    if (json[pos] == '"') // string value
    {
        auto start = pos + 1;
        auto end = json.find('"', start);
        if (end == std::string::npos) return "";
        return json.substr(start, end - start);
    }
    else // number or other value
    {
        auto start = pos;
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ' ')
            ++pos;
        return json.substr(start, pos - start);
    }
}

static std::string json_response(const std::string& status, const std::string& message)
{
    return "{\"status\":\"" + status + "\",\"message\":\"" + message + "\"}\n";
}

/* ---- command handlers ---- */

static std::string cmd_gems(const std::string& json)
{
    std::string player = json_get(json, "player");
    std::string mode = json_get(json, "mode");
    std::string amount_str = json_get(json, "amount");

    if (player.empty() || amount_str.empty() || mode.empty())
        return json_response("error", "Missing player, mode or amount");

    int amount = std::stoi(amount_str);
    if (amount == 0)
        return json_response("error", "Amount must not be 0");

    bool found = false;
    peers("", PEER_ALL, [&](ENetPeer& peer)
    {
        ::peer *pTarget = static_cast<::peer*>(peer.data);
        if (pTarget->ltoken[0] == player)
        {
            found = true;
            if (mode == "give") pTarget->gems += amount;
            else pTarget->gems -= amount;
            if (pTarget->gems < 0) pTarget->gems = 0;

            ENetEvent ev{};
            ev.peer = &peer;
            on::SetBux(ev);
        }
    });

    if (!found)
        return json_response("error", "Player not online");

    if (mode == "give")
        return json_response("ok",
            std::format("Added {} gems to {}", amount, player));
    else
        return json_response("ok",
            std::format("Removed {} gems from {}", amount, player));
}

static std::string cmd_give_item(const std::string& json)
{
    std::string player = json_get(json, "player");
    std::string id_str = json_get(json, "item_id");
    std::string amount_str = json_get(json, "amount");

    if (player.empty() || id_str.empty() || amount_str.empty())
        return json_response("error", "Missing player, item_id, or amount");

    short item_id = (short)std::stoi(id_str);
    short amount  = (short)std::stoi(amount_str);

    if (item_id < 0 || item_id >= (short)items.size())
        return json_response("error", "Invalid item_id");
    if (amount <= 0 || amount > 200)
        return json_response("error", "Amount must be 1-200");

    bool found = false;
    peers("", PEER_ALL, [&](ENetPeer& peer)
    {
        ::peer *pTarget = static_cast<::peer*>(peer.data);
        if (pTarget->ltoken[0] == player)
        {
            found = true;

            ENetEvent ev{};
            ev.peer = &peer;

            modify_item_inventory(ev, ::slot(item_id, amount));
            send_inventory_state(ev);

            packet::create(peer, false, 0, {
                "OnConsoleMessage",
                std::format("`2You received `w{}`` x`w{}`` from the store!",
                    items[item_id].raw_name, amount).c_str()
            });
        }
    });

    if (!found)
        return json_response("error", "Player not online");

    return json_response("ok",
        std::format("Gave {} x{} to {}", items[item_id].raw_name, amount, player));
}

static std::string cmd_get_player(const std::string& json)
{
    std::string player = json_get(json, "player");
    if (player.empty())
        return json_response("error", "Missing player");

    bool found = false;
    std::string result;

    peers("", PEER_ALL, [&](ENetPeer& peer)
    {
        ::peer *pTarget = static_cast<::peer*>(peer.data);
        if (pTarget->ltoken[0] == player)
        {
            found = true;
            result = std::format(
                "{{\"status\":\"ok\",\"online\":true,\"gems\":{},\"level\":{},\"xp\":{},\"role\":{},\"world\":\"{}\"}}",
                pTarget->gems, pTarget->level[0], pTarget->level[1], pTarget->role, pTarget->recent_worlds.back()
            );
        }
    });

    if (!found)
    {
        // check if player exists in database
        if (::peer().exists(player))
        {
            ::peer& offline = ::peer().read(player);
            result = std::format(
                "{{\"status\":\"ok\",\"online\":false,\"gems\":{},\"level\":{},\"xp\":{},\"role\":{}}}",
                offline.gems, offline.level[0], offline.level[1], offline.role
            );
        }
        else
            return json_response("error", "Player not found");
    }

    return result + "\n";
}

/* ---- process a single request ---- */

static std::string process_request(const std::string& json)
{
    std::string cmd = json_get(json, "cmd");

    if (cmd == "gems")        return cmd_gems(json);
    if (cmd == "give_item")   return cmd_give_item(json);
    if (cmd == "get_player")  return cmd_get_player(json);

    return json_response("error", "Unknown command: " + cmd);
}

/* ---- TCP listener ---- */

void api::listener()
{
    SOCKET_T server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server < 0)
    {
        puts("[api] failed to create socket");
        return;
    }

    constexpr int enable = 1;
#ifdef SO_REUSEADDR
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable));
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(17092);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr); // @note localhost only

    if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        puts("[api] failed to bind port 17092");
        CLOSE_SOCKET(server);
        return;
    }

    listen(server, 10);
    std::printf("[api] listening on 127.0.0.1:17092\n");

    while (true)
    {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        SOCKET_T fd = accept(server, (struct sockaddr*)&client_addr, &client_len);
        if (fd < 0) continue;

        // read request (max 4KB)
        char buf[4096]{};
        int n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n > 0)
        {
            buf[n] = '\0';
            std::string request(buf, n);
            std::string response = process_request(request);

            send(fd, response.c_str(), response.size(), 0);
        }

        CLOSE_SOCKET(fd);
    }
}

#ifndef _WIN32
    #undef SOCKET_T
    #undef CLOSE_SOCKET
#endif
