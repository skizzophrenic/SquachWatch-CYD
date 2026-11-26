// SquachWatch-CM0 -- the Arduino WiFi client, as a real TCP connection.
//
// The LoRa enrichment transport (firmware/src/lora_enrich.cpp) opens a
// plain-HTTP connection through one of these (HTTPClient.h below). On
// this board there is no ESP32 radio stack to stand in for: the app
// lives on the stock OS, whose own wifi network reaches the hosts, so
// the client is a POSIX socket -- connect, read, close. The surface is
// the two calls the transport makes (available(), read()) plus connect
// and stop for the HTTPClient that drives it.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

class WiFiClient {
public:
    WiFiClient() = default;
    ~WiFiClient() { stop(); }
    WiFiClient(const WiFiClient&) = delete;
    WiFiClient& operator=(const WiFiClient&) = delete;

    // Hostname or dotted address, five-second connect budget (the
    // transport's setConnectTimeout value), then an eight-second read
    // budget on the socket (its setTimeout value): a stalled body ends
    // in -1 from read(), which the transport's own deadline also bounds.
    bool connect(const char* host, uint16_t port) {
        stop();
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) != 0
            || !res)
            return false;
        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) { freeaddrinfo(res); return false; }
        // A non-blocking connect, polled: a blackholed host would
        // otherwise stall the caller on the kernel's own ~130 s timeout.
        const int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int r = ::connect(fd, res->ai_addr, res->ai_addrlen);
        bool ok = r == 0;
        if (!ok && r < 0 && errno == EINPROGRESS) {
            struct pollfd pf = { fd, POLLOUT, 0 };
            if (poll(&pf, 1, 5000) > 0 && (pf.revents & POLLOUT)) {
                int err = 0;
                socklen_t ln = sizeof err;
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &ln) == 0
                    && err == 0)
                    ok = true;
            }
        }
        freeaddrinfo(res);
        if (!ok) { ::close(fd); return false; }
        fcntl(fd, F_SETFL, fl);
        struct timeval tv = { 8, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        s_fd = fd;
        return true;
    }
    bool connected() const { return s_fd >= 0; }
    int available() {
        if (s_fd < 0) return 0;
        long n = 0;
        return ioctl(s_fd, FIONREAD, &n) == 0 ? (int)n : 0;
    }
    int read(uint8_t* buf, size_t n) {
        if (s_fd < 0) return -1;
        return (int)::read(s_fd, buf, n);
    }
    int write(const uint8_t* buf, size_t n) {
        if (s_fd < 0) return -1;
        return (int)::write(s_fd, buf, n);
    }
    void stop() {
        if (s_fd >= 0) { ::close(s_fd); s_fd = -1; }
    }

private:
    int s_fd = -1;
};
