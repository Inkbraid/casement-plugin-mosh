// mosh:// addresses, and the ssh options string split and quoted for a
// Windows command line.

#include "check.h"

#include "casement/address.h"
#include "mosh/options.h"

using namespace casement;
using namespace casement::mosh;

namespace {

std::optional<Options> options(std::string_view text, std::string_view username = {}) {
    std::string error;
    auto a = parse_address(text, error);
    if (!a) return std::nullopt;
    return parse_options(*a, username, error);
}

std::string error_for(std::string_view text) {
    std::string error;
    auto a = parse_address(text, error);
    if (!a) return error;
    if (parse_options(*a, {}, error)) return "";
    return error;
}

using Args = std::vector<std::string>;

}  // namespace

TEST(reads_all_fields) {
    auto o = options(
        "mosh://ann@server.example.com:2222?udp=60000:61000&server=%2Fusr%2Flocal%2Fbin%2Fmosh-server&"
        "predict=always&ssh_args=-J%20jump.example.com%20-o%20%22SetEnv%3DA%3Db%20c%22");
    REQUIRE(o);
    CHECK_EQ(o->user, std::string("ann"));
    CHECK_EQ(o->host, std::string("server.example.com"));
    REQUIRE(o->ssh_port);
    CHECK_EQ(*o->ssh_port, 2222);
    CHECK_EQ(o->udp, std::string("60000:61000"));
    CHECK_EQ(o->server, std::string("/usr/local/bin/mosh-server"));
    CHECK(o->predict == Predict::always);
    CHECK(o->ssh_args == (Args{"-J", "jump.example.com", "-o", "SetEnv=A=b c"}));
}

TEST(defaults_and_ipv6) {
    auto o = options("mosh://[fd00::5]", "bob");
    REQUIRE(o);
    CHECK_EQ(o->host, std::string("fd00::5"));
    CHECK_EQ(o->user, std::string("bob"));  // from connect's username
    CHECK(!o->ssh_port);
    CHECK(o->udp.empty());
    CHECK_EQ(o->server, std::string("mosh-server"));
    CHECK(o->predict == Predict::adaptive);
    CHECK(o->ssh_args.empty());

    auto never = options("mosh://h?predict=Never&udp=60001&server=");
    REQUIRE(never);
    CHECK(never->predict == Predict::never);
    CHECK_EQ(never->udp, std::string("60001"));
    CHECK_EQ(never->server, std::string("mosh-server"));  // empty means the default
}

TEST(rejects_bad_values) {
    CHECK(!error_for("mosh://h?udp=0").empty());
    CHECK(!error_for("mosh://h?udp=70000").empty());
    CHECK(!error_for("mosh://h?udp=61000:60000").empty());
    CHECK(!error_for("mosh://h?udp=6000a").empty());
    CHECK(!error_for("mosh://h?udp=1:2:3").empty());
    CHECK(!error_for("mosh://h?udp=60000;rm%20-rf").empty());
    CHECK(!error_for("mosh://h?predict=experimental").empty());
    CHECK(!error_for("mosh://h?ssh_args=-o%20%22unclosed").empty());
    CHECK(!error_for("mosh://h?server=a%0Ab").empty());
    CHECK(!error_for("mosh://h/path").empty());
    CHECK(!error_for("ssh://h").empty());
    // A leading '-' would make ssh read the host or user as an option.
    CHECK(!error_for("mosh://-oProxyCommand=calc").empty());
    CHECK(!error_for("mosh://-oProxyCommand=calc@host").empty());
    CHECK(error_for("mosh://ann@h?udp=60000:61000").empty());
}

TEST(splits_like_windows) {
    auto split = [](std::string_view s) { return split_arguments(s); };
    CHECK(split("") == Args{});
    CHECK(split("  \t ") == Args{});
    CHECK(split(R"(-o "SetEnv=A=b c" -i C:\keys\id)") == (Args{"-o", "SetEnv=A=b c", "-i", R"(C:\keys\id)"}));
    CHECK(split(R"(a\\\"b)") == (Args{R"(a\"b)"}));
    CHECK(split(R"("a\\")") == (Args{R"(a\)"}));
    CHECK(split(R"("x""y")") == (Args{R"(x"y)"}));
    CHECK(split(R"("")") == (Args{""}));
    CHECK(split(R"(a"b c"d)") == (Args{"ab cd"}));
    CHECK(split(R"(\\server\share)") == (Args{R"(\\server\share)"}));
    CHECK(!split(R"(-o "open)"));
}

TEST(quotes_for_windows) {
    CHECK_EQ(quote_argument("plain"), std::string("plain"));
    CHECK_EQ(quote_argument(""), std::string("\"\""));
    CHECK_EQ(quote_argument("a b"), std::string("\"a b\""));
    CHECK_EQ(quote_argument(R"(C:\Program Files\x\)"), std::string(R"("C:\Program Files\x\\")"));
    CHECK_EQ(quote_argument(R"(say "hi")"), std::string(R"("say \"hi\"")"));
    // Whatever goes in comes out of the C runtime's parser unchanged.
    for (std::string s : {"", "a b", R"(a\)", R"(a\"b)", "\"", R"(C:\Program Files\x\)", "tab\there", R"(\\"\\)",
                          "SetEnv=A=b c", R"(-oProxyCommand=ssh -W %h:%p "jump")"}) {
        auto back = split_arguments(quote_argument(s));
        REQUIRE(back);
        CHECK(back->size() == 1 && back->front() == s);
    }
}

TEST(checks_ssh_options) {
    std::string error;
    auto ok = [&](Args a) { return check_ssh_args(a, error); };
    CHECK(ok({}));
    CHECK(ok({"-J", "jump.example.com"}));
    CHECK(ok({"-p2222", "-oStrictHostKeyChecking=accept-new", "-46", "-v", "-v"}));
    CHECK(ok({"-L", "8080:localhost:80", "-C", "-i", R"(C:\keys\id)"}));
    CHECK(ok({"-Ck", "-lroot"}));
    CHECK(!ok({"host.example.com"}));
    CHECK(!ok({"-i"}));
    CHECK(!ok({"--"}));
    CHECK(!ok({"-"}));
    CHECK(!ok({"-N"}));
    CHECK(!ok({"-vG"}));
    CHECK(!ok({"-o", "Foo=bar", "extra"}));
}
CHECK_MAIN
