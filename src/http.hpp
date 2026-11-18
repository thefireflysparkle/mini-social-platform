#pragma once
// A very small HTTP/1.1 server built on POSIX sockets.
//
// I wrote this by hand instead of pulling in a library so the repo has no
// dependencies beyond SQLite and builds with a plain g++ call. It only does
// what this assignment needs: read one request, match the method and path,
// write one response, close. Single threaded, one connection at a time.
//
// Not production code. No keep-alive, no chunked encoding, no TLS.

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <iostream>
#include <cstring>
#include <exception>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

struct Request {
    std::string method;
    std::string path;                        // path without the query string
    std::string body;
    std::map<std::string, std::string> query;
    std::vector<std::string> segments;       // "/feed/7" becomes {"feed", "7"}
};

struct Response {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
    std::map<std::string, std::string> extra_headers;
};

class HttpServer {
public:
    using Handler = std::function<bool(const Request&, Response&)>;

    void route(Handler h) { handlers_.push_back(h); }

    void listen_and_serve(int port) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { perror("socket"); return; }

        // Without SO_REUSEADDR the port sits in TIME_WAIT for a minute or two
        // after the server stops, and restarting fails with "address in use".
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<uint16_t>(port));

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("bind"); ::close(fd); return;
        }
        if (::listen(fd, 16) < 0) { perror("listen"); ::close(fd); return; }

        std::cout << "listening on http://127.0.0.1:" << port << std::endl;

        while (true) {
            int c = ::accept(fd, nullptr, nullptr);
            if (c < 0) continue;
            handle_connection(c);
            ::close(c);
        }
    }

private:
    std::vector<Handler> handlers_;

    void handle_connection(int c) {
        std::string raw;
        char buf[4096];

        // Read until we have the whole header block.
        while (raw.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = ::recv(c, buf, sizeof(buf), 0);
            if (n <= 0) return;
            raw.append(buf, static_cast<size_t>(n));
            if (raw.size() > 1 << 20) return;   // refuse anything silly
        }

        size_t header_end = raw.find("\r\n\r\n") + 4;
        std::string headers = raw.substr(0, header_end);

        // A POST body arrives after the headers. Keep reading until we have
        // Content-Length bytes of it, because one recv is not guaranteed to
        // deliver the whole thing.
        size_t want = content_length(headers);
        std::string body = raw.substr(header_end);
        while (body.size() < want) {
            ssize_t n = ::recv(c, buf, sizeof(buf), 0);
            if (n <= 0) break;
            body.append(buf, static_cast<size_t>(n));
        }

        Request req = parse(headers, body);
        Response res;
        bool matched = false;

        // A handler that throws must not take the server down with it. I
        // found this the hard way: liking a post id that did not exist threw
        // a foreign key error out of the handler, past main, and the process
        // exited. One bad request should be one bad response.
        try {
            for (auto& h : handlers_) {
                if (h(req, res)) { matched = true; break; }
            }
        } catch (const std::exception& e) {
            std::cerr << "handler error: " << e.what() << std::endl;
            res = Response{};
            res.status = 500;
            res.body = R"({"error":"internal error"})";
            matched = true;
        }

        if (!matched) {
            res.status = 404;
            res.body = R"({"error":"not found"})";
        }
        send_response(c, res);
    }

    static size_t content_length(const std::string& headers) {
        // Header names are case insensitive, so lowercase a copy to search it.
        std::string lower = headers;
        for (auto& ch : lower) ch = static_cast<char>(tolower(ch));
        size_t p = lower.find("content-length:");
        if (p == std::string::npos) return 0;
        return static_cast<size_t>(std::stoul(headers.substr(p + 15)));
    }

    static Request parse(const std::string& headers, const std::string& body) {
        Request r;
        r.body = body;

        size_t sp1 = headers.find(' ');
        size_t sp2 = headers.find(' ', sp1 + 1);
        r.method = headers.substr(0, sp1);
        std::string target = headers.substr(sp1 + 1, sp2 - sp1 - 1);

        size_t q = target.find('?');
        if (q != std::string::npos) {
            std::string qs = target.substr(q + 1);
            target = target.substr(0, q);
            size_t pos = 0;
            while (pos < qs.size()) {
                size_t amp = qs.find('&', pos);
                if (amp == std::string::npos) amp = qs.size();
                std::string pair = qs.substr(pos, amp - pos);
                size_t eq = pair.find('=');
                if (eq != std::string::npos)
                    r.query[pair.substr(0, eq)] = pair.substr(eq + 1);
                pos = amp + 1;
            }
        }
        r.path = target;

        // Split the path on '/' so a handler can read "/feed/7" as segments.
        size_t pos = 0;
        while (pos < target.size()) {
            if (target[pos] == '/') { pos++; continue; }
            size_t slash = target.find('/', pos);
            if (slash == std::string::npos) slash = target.size();
            r.segments.push_back(target.substr(pos, slash - pos));
            pos = slash;
        }
        return r;
    }

    static void send_response(int c, const Response& res) {
        std::string out = "HTTP/1.1 " + std::to_string(res.status) + " "
                        + reason(res.status) + "\r\n";
        out += "Content-Type: " + res.content_type + "\r\n";
        out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
        for (auto& [k, v] : res.extra_headers) out += k + ": " + v + "\r\n";
        out += "Connection: close\r\n\r\n";
        out += res.body;

        // send() can write less than asked for, so loop until it is all out.
        size_t sent = 0;
        while (sent < out.size()) {
            ssize_t n = ::send(c, out.data() + sent, out.size() - sent, 0);
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
    }

    static std::string reason(int code) {
        switch (code) {
            case 200: return "OK";
            case 201: return "Created";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 500: return "Internal Server Error";
            default:  return "OK";
        }
    }
};
