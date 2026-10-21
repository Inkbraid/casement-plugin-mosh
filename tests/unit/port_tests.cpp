// The Windows port of mosh's client: AES-OCB, session keys, the protocol
// buffers encoding, UTF-8 and character widths, the random source and the
// clock, and the escape sequences Display writes for known screens.

#include "check.h"

#include <windows.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "ae.h"
#include "completeterminal.h"
#include "crypto.h"
#include "hostinput.pb.h"
#include "parser.h"
#include "prng.h"
#include "terminaldisplay.h"
#include "timestamp.h"
#include "transportinstruction.pb.h"
#include "unicode.h"
#include "userinput.pb.h"

namespace {

std::string unhex(std::string_view hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out += static_cast<char>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16));
    return out;
}

std::string to_hex(const std::string& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}

std::string counting(std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) s += static_cast<char>(i);
    return s;
}

/// Encrypts with mosh's OCB code (ocb_internal.cc on OpenSSL's AES); the
/// tag is appended to the ciphertext as mosh uses it.
std::string ocb_encrypt(const std::string& key, const std::string& nonce, const std::string& ad,
                        const std::string& pt) {
    Crypto::AlignedBuffer ctx(ae_ctx_sizeof());
    REQUIRE(ae_init(reinterpret_cast<ae_ctx*>(ctx.data()), key.data(), 16, 12, 16) == AE_SUCCESS);
    Crypto::AlignedBuffer n(12, nonce.data()), a(ad.size(), ad.data()), p(pt.size(), pt.data()),
        c(pt.size() + 16);
    int len = ae_encrypt(reinterpret_cast<ae_ctx*>(ctx.data()), n.data(), p.data(), static_cast<int>(pt.size()),
                         ad.empty() ? nullptr : a.data(), static_cast<int>(ad.size()), c.data(), nullptr,
                         AE_FINALIZE);
    ae_clear(reinterpret_cast<ae_ctx*>(ctx.data()));
    REQUIRE(len == static_cast<int>(pt.size() + 16));
    return std::string(c.data(), static_cast<std::size_t>(len));
}

/// Decrypts; empty optional if the tag doesn't verify.
std::optional<std::string> ocb_decrypt(const std::string& key, const std::string& nonce, const std::string& ad,
                                       const std::string& ct) {
    Crypto::AlignedBuffer ctx(ae_ctx_sizeof());
    REQUIRE(ae_init(reinterpret_cast<ae_ctx*>(ctx.data()), key.data(), 16, 12, 16) == AE_SUCCESS);
    Crypto::AlignedBuffer n(12, nonce.data()), a(ad.size(), ad.data()), c(ct.size(), ct.data()),
        p(ct.size() + 16);
    int len = ae_decrypt(reinterpret_cast<ae_ctx*>(ctx.data()), n.data(), c.data(), static_cast<int>(ct.size()),
                         ad.empty() ? nullptr : a.data(), static_cast<int>(ad.size()), p.data(), nullptr,
                         AE_FINALIZE);
    ae_clear(reinterpret_cast<ae_ctx*>(ctx.data()));
    if (len < 0) return std::nullopt;
    return std::string(p.data(), static_cast<std::size_t>(len));
}

}  // namespace

TEST(ocb_aes_rfc7253_vectors) {
    // RFC 7253, appendix A (AES-128, 128-bit tags): key 000102...0F, nonce
    // BBAA99887766554433221100 + i, associated data and plaintext 00 01 02 ...
    // of the lengths given. The same values come out of OpenSSL's own OCB
    // (EVP_aes_128_ocb, through Python's cryptography package).
    struct Vector {
        int i;
        std::size_t ad, pt;
        const char* ct;
    };
    const Vector vectors[] = {
        {0, 0, 0, "785407BFFFC8AD9EDCC5520AC9111EE6"},
        {1, 8, 8, "6820B3657B6F615A5725BDA0D3B4EB3A257C9AF1F8F03009"},
        {2, 8, 0, "81017F8203F081277152FADE694A0A00"},
        {3, 0, 8, "45DD69F8F5AAE72414054CD1F35D82760B2CD00D2F99BFA9"},
        {4, 16, 16, "571D535B60B277188BE5147170A9A22C3AD7A4FF3835B8C5701C1CCEC8FC3358"},
        {5, 16, 0, "8CF761B6902EF764462AD86498CA6B97"},
        {6, 0, 16, "5CE88EC2E0692706A915C00AEB8B2396F40E1C743F52436BDF06D8FA1ECA343D"},
        {7, 24, 24, "1CA2207308C87C010756104D8840CE1952F09673A448A122C92C62241051F57356D7F3C90BB0E07F"},
        {8, 24, 0, "6DC225A071FC1B9F7C69F93B0F1E10DE"},
        {9, 0, 24, "221BD0DE7FA6FE993ECCD769460A0AF2D6CDED0C395B1C3CE725F32494B9F914D85C0B1EB38357FF"},
        {10, 32, 32,
         "BD6F6C496201C69296C11EFD138A467ABD3C707924B964DEAFFC40319AF5A48540FBBA186C5553C68AD9F592A79A4240"},
        {11, 32, 0, "FE80690BEE8A485D11F32965BC9D2A32"},
        {12, 0, 32,
         "2942BFC773BDA23CABC6ACFD9BFD5835BD300F0973792EF46040C53F1432BCDFB5E1DDE3BC18A5F840B52E653444D5DF"},
        {13, 40, 40,
         "D5CA91748410C1751FF8A2F618255B68A0A12E093FF454606E59F9C1D0DDC54B65E8628E568BAD7AED07BA06A4A69483A7035490C5769E60"},
        {14, 40, 0, "C5CD9D1850C141E358649994EE701B68"},
        {15, 0, 40,
         "4412923493C57D5DE0D700F753CCE0D1D2D95060122E9F15A5DDBFC5787E50B5CC55EE507BCB084E479AD363AC366B95A98CA5F3000B1479"},
    };
    const std::string key = counting(16);
    for (const Vector& v : vectors) {
        std::string nonce = unhex("BBAA99887766554433221100");
        nonce[11] = static_cast<char>(v.i);
        std::string expected = unhex(v.ct);
        std::string ct = ocb_encrypt(key, nonce, counting(v.ad), counting(v.pt));
        CHECK_EQ(to_hex(ct), to_hex(expected));
        auto pt = ocb_decrypt(key, nonce, counting(v.ad), expected);
        REQUIRE(pt);
        CHECK(*pt == counting(v.pt));
        // Any flipped bit, in the text or the tag, is refused.
        for (std::size_t bit = 0; bit < expected.size() * 8; bit += 7) {
            std::string bad = expected;
            bad[bit / 8] = static_cast<char>(bad[bit / 8] ^ (1 << (bit % 8)));
            CHECK(!ocb_decrypt(key, nonce, counting(v.ad), bad));
        }
    }
}

TEST(session_keys_and_messages) {
    // mosh-server prints the key as 22 characters of base64 (16 bytes).
    Crypto::Base64Key key("4NeCCgvZFe2RnPgrcU1PQw");
    CHECK_EQ(key.printable_key(), std::string("4NeCCgvZFe2RnPgrcU1PQw"));
    const unsigned char expected[16] = {0xe0, 0xd7, 0x82, 0x0a, 0x0b, 0xd9, 0x15, 0xed,
                                        0x91, 0x9c, 0xf8, 0x2b, 0x71, 0x4d, 0x4f, 0x43};
    CHECK(std::memcmp(key.data(), expected, 16) == 0);
    auto refused = [](const char* text) {
        try {
            Crypto::Base64Key k(text);
            return false;
        } catch (const Crypto::CryptoException&) {
            return true;
        }
    };
    CHECK(refused("4NeCCgvZFe2RnPgrcU1PQ"));    // 21 characters
    CHECK(refused("4NeCCgvZFe2RnPgrcU1PQww"));  // 23
    CHECK(refused("4NeCCgvZFe2RnPgrcU1P!w"));   // not base64
    CHECK(refused("4NeCCgvZFe2RnPgrcU1PQx"));   // bits beyond the 128

    // A session encrypts with a nonce and refuses tampered packets.
    Crypto::Session sender(key), receiver(key);
    std::string wire = sender.encrypt(Crypto::Message(Crypto::Nonce(0x8000000000000001ull), "hello"));
    CHECK_EQ(wire.size(), std::size_t(8 + 5 + 16));
    Crypto::Message m = receiver.decrypt(wire);
    CHECK_EQ(m.text, std::string("hello"));
    CHECK(m.nonce.val() == 0x8000000000000001ull);
    wire[10] = static_cast<char>(wire[10] ^ 1);
    bool threw = false;
    try {
        receiver.decrypt(wire);
    } catch (const Crypto::CryptoException&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(random_bytes_and_clock) {
    PRNG prng;
    std::uint64_t a = prng.uint64(), b = prng.uint64(), c = prng.uint64();
    CHECK(!(a == b && b == c));
    // A random key is a valid key.
    Crypto::Base64Key random_key;
    CHECK_EQ(Crypto::Base64Key(random_key.printable_key()).printable_key(), random_key.printable_key());

    freeze_timestamp();
    std::uint64_t t0 = frozen_timestamp();
    CHECK_EQ(frozen_timestamp(), t0);  // frozen until the next freeze
    Sleep(50);
    freeze_timestamp();
    std::uint64_t t1 = frozen_timestamp();
    CHECK(t1 >= t0 + 40 && t1 < t0 + 5000);
}

TEST(protobuf_matches_protoc) {
    // The expected bytes are protoc 3.21.12's --encode of the same messages
    // (third_party/mosh/src/protobufs/*.proto).
    TransportBuffers::Instruction inst;
    inst.set_protocol_version(2);
    inst.set_old_num(5);
    inst.set_new_num(6);
    inst.set_ack_num(3);
    inst.set_throwaway_num(1);
    inst.set_diff("abc");
    inst.set_chaff(std::string("\x01\x02", 2));
    CHECK_EQ(to_hex(inst.SerializeAsString()), std::string("0802100518062003280132036162633a020102"));

    TransportBuffers::Instruction shutdown;
    shutdown.set_protocol_version(2);
    shutdown.set_old_num(0);
    shutdown.set_new_num(std::uint64_t(-1));
    shutdown.set_ack_num(300);
    shutdown.set_throwaway_num(0);
    shutdown.set_diff("");
    shutdown.set_chaff("");
    std::string wire = shutdown.SerializeAsString();
    CHECK_EQ(to_hex(wire), std::string("0802100018ffffffffffffffffff0120ac02280032003a00"));
    TransportBuffers::Instruction back;
    REQUIRE(back.ParseFromString(wire));
    CHECK(back.new_num() == std::uint64_t(-1));
    CHECK(back.ack_num() == 300);
    CHECK(back.has_diff() && back.diff().empty());

    ClientBuffers::UserMessage user;
    user.add_instruction()->MutableExtension(ClientBuffers::keystroke)->set_keys("ls\r", 3);
    ClientBuffers::ResizeMessage* r = user.add_instruction()->MutableExtension(ClientBuffers::resize);
    r->set_width(80);
    r->set_height(24);
    CHECK_EQ(to_hex(user.SerializeAsString()), std::string("0a07120522036c730d0a061a0428503018"));

    ClientBuffers::UserMessage odd;
    ClientBuffers::ResizeMessage* r2 = odd.add_instruction()->MutableExtension(ClientBuffers::resize);
    r2->set_width(-1);  // int32: ten bytes on the wire
    r2->set_height(2000);
    CHECK_EQ(to_hex(odd.SerializeAsString()), std::string("0a101a0e28ffffffffffffffffff0130d00f"));

    HostBuffers::HostMessage host;
    host.add_instruction()->MutableExtension(HostBuffers::echoack)->set_echo_ack_num(42);
    HostBuffers::ResizeMessage* hr = host.add_instruction()->MutableExtension(HostBuffers::resize);
    hr->set_width(100);
    hr->set_height(30);
    host.add_instruction()->MutableExtension(HostBuffers::hostbytes)->set_hoststring("\x1b[H\xc3\xa9");
    CHECK_EQ(to_hex(host.SerializeAsString()),
             std::string("0a043a02402a0a061a042864301e0a09120722051b5b48c3a9"));

    // One instruction with two extensions: written in field order.
    HostBuffers::HostMessage two;
    HostBuffers::Instruction* i2 = two.add_instruction();
    i2->MutableExtension(HostBuffers::resize)->set_width(1);
    i2->MutableExtension(HostBuffers::hostbytes)->set_hoststring("");
    CHECK_EQ(to_hex(two.SerializeAsString()), std::string("0a08120222001a022801"));
}

TEST(protobuf_parses) {
    HostBuffers::HostMessage host;
    REQUIRE(host.ParseFromString(unhex("0a043a02402a0a061a042864301e0a09120722051b5b48c3a9")));
    REQUIRE(host.instruction_size() == 3);
    CHECK(host.instruction(0).HasExtension(HostBuffers::echoack));
    CHECK(host.instruction(0).GetExtension(HostBuffers::echoack).echo_ack_num() == 42);
    CHECK(!host.instruction(0).HasExtension(HostBuffers::hostbytes));
    CHECK(host.instruction(1).GetExtension(HostBuffers::resize).width() == 100);
    CHECK(host.instruction(1).GetExtension(HostBuffers::resize).height() == 30);
    CHECK_EQ(host.instruction(2).GetExtension(HostBuffers::hostbytes).hoststring(), std::string("\x1b[H\xc3\xa9"));

    ClientBuffers::UserMessage user;
    REQUIRE(user.ParseFromString(unhex("0a101a0e28ffffffffffffffffff0130d00f")));
    CHECK(user.instruction(0).GetExtension(ClientBuffers::resize).width() == -1);
    CHECK(user.instruction(0).GetExtension(ClientBuffers::resize).height() == 2000);

    // Unknown fields (a varint, bytes and a group) are skipped, as protobuf
    // does, and so is a known field sent with another wire type.
    TransportBuffers::Instruction inst;
    REQUIRE(inst.ParseFromString(unhex("0802" "4807" "52024142" "5b08015c" "15ffffffff" "100a")));
    CHECK(inst.protocol_version() == 2);
    CHECK(inst.old_num() == 10);  // the varint one; the fixed32 field 2 isn't
    CHECK(!inst.has_new_num());
}

TEST(protobuf_refuses_broken_input) {
    TransportBuffers::Instruction inst;
    CHECK(inst.ParseFromString(unhex("3203616263")));                     // diff "abc"
    CHECK(!inst.ParseFromString(unhex("3204616263")));                    // diff one byte short
    CHECK(!inst.has_diff());
    CHECK(!inst.ParseFromString(unhex("08")));                            // varint cut off
    CHECK(!inst.ParseFromString(unhex("08ffffffffffffffffffff01")));      // varint of eleven bytes
    CHECK(!inst.ParseFromString(unhex("0f00")));                          // wire type 7
    CHECK(!inst.ParseFromString(unhex("0001")));                          // field 0
    CHECK(!inst.ParseFromString(unhex("5b0801")));                        // group never ends
    CHECK(!inst.ParseFromString(unhex("5c")));                            // end of no group
    HostBuffers::HostMessage host;
    CHECK(!host.ParseFromString(unhex("0a03120140")));  // hostbytes cut off inside the instruction
    CHECK_EQ(host.instruction_size(), 0);
}

TEST(utf8_like_glibc) {
    char32_t c = 0;
    CHECK_EQ(mosh_utf8_decode(&c, "A", 1), std::size_t(1));
    CHECK(c == U'A');
    CHECK_EQ(mosh_utf8_decode(&c, "\0", 1), std::size_t(0));
    CHECK_EQ(mosh_utf8_decode(&c, "\xc3\xa9", 2), std::size_t(2));
    CHECK(c == 0xE9);
    CHECK_EQ(mosh_utf8_decode(&c, "\xe2\x82\xac", 3), std::size_t(3));
    CHECK(c == 0x20AC);
    CHECK_EQ(mosh_utf8_decode(&c, "\xf0\x9f\x98\x80", 4), std::size_t(4));
    CHECK(c == 0x1F600);
    const std::size_t invalid = std::size_t(-1), incomplete = std::size_t(-2);
    CHECK_EQ(mosh_utf8_decode(&c, "\xe2\x82", 2), incomplete);
    CHECK_EQ(mosh_utf8_decode(&c, "\xf0\x9f", 2), incomplete);
    CHECK_EQ(mosh_utf8_decode(&c, "\x80", 1), invalid);               // continuation byte
    CHECK_EQ(mosh_utf8_decode(&c, "\xc0\x80", 2), invalid);           // overlong
    CHECK_EQ(mosh_utf8_decode(&c, "\xe0\x80\x80", 3), invalid);       // overlong
    CHECK_EQ(mosh_utf8_decode(&c, "\xed\xa0\x80", 3), invalid);       // surrogate
    CHECK_EQ(mosh_utf8_decode(&c, "\xf4\x90\x80\x80", 4), invalid);   // above U+10FFFF
    CHECK_EQ(mosh_utf8_decode(&c, "\xe2\x28\xa1", 3), invalid);       // broken continuation
    CHECK_EQ(mosh_utf8_decode(&c, "\xf8\x88\x80\x80\x80", 5), invalid);

    std::string s;
    for (char32_t ch : {U'a', char32_t(0xE9), char32_t(0x20AC), char32_t(0x1F600), char32_t(0xD800)})
        mosh_utf8_append(s, ch);
    CHECK_EQ(s, std::string("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\xef\xbf\xbd"));
    CHECK(mosh_utf8_to_u32("x\xff" "y") == std::u32string(U"x�y"));

    // The terminal's parser turns bytes into characters across calls, and
    // bad bytes into U+FFFD.
    Parser::UTF8Parser parser;
    Parser::Actions actions;
    for (char b : std::string("\xe2\x82")) parser.input(b, actions);
    CHECK(actions.empty());
    parser.input('\xac', actions);
    REQUIRE(actions.size() == 1);
    CHECK(actions[0]->ch == 0x20AC);
    actions.clear();
    parser.input('\x80', actions);
    REQUIRE(actions.size() == 1);
    CHECK(actions[0]->ch == 0xFFFD);
}

TEST(widths_like_glibc) {
    // glibc 2.39's wcwidth() in C.UTF-8.
    CHECK_EQ(mosh_wcwidth(U'a'), 1);
    CHECK_EQ(mosh_wcwidth(0), 0);
    CHECK_EQ(mosh_wcwidth(0x07), -1);       // BEL
    CHECK_EQ(mosh_wcwidth(0x9B), -1);       // C1 control
    CHECK_EQ(mosh_wcwidth(0xA0), 1);        // no-break space
    CHECK_EQ(mosh_wcwidth(0xAD), 1);        // soft hyphen
    CHECK_EQ(mosh_wcwidth(0x0300), 0);      // combining grave accent
    CHECK_EQ(mosh_wcwidth(0x0378), -1);     // unassigned
    CHECK_EQ(mosh_wcwidth(0x1100), 2);      // Hangul choseong
    CHECK_EQ(mosh_wcwidth(0x1160), 0);      // Hangul jungseong
    CHECK_EQ(mosh_wcwidth(0x200B), 0);      // zero width space
    CHECK_EQ(mosh_wcwidth(0x2028), -1);     // line separator
    CHECK_EQ(mosh_wcwidth(0x231A), 2);      // watch (emoji presentation)
    CHECK_EQ(mosh_wcwidth(0x2764), 1);      // heavy black heart (text presentation)
    CHECK_EQ(mosh_wcwidth(0x3000), 2);      // ideographic space
    CHECK_EQ(mosh_wcwidth(0x4E00), 2);      // CJK
    CHECK_EQ(mosh_wcwidth(0xD800), -1);     // surrogate
    CHECK_EQ(mosh_wcwidth(0xE000), 1);      // private use
    CHECK_EQ(mosh_wcwidth(0xFE0F), 0);      // variation selector 16
    CHECK_EQ(mosh_wcwidth(0xFF21), 2);      // fullwidth A
    CHECK_EQ(mosh_wcwidth(0x1F1E6), 1);     // regional indicator
    CHECK_EQ(mosh_wcwidth(0x1F3FB), 2);     // skin tone modifier
    CHECK_EQ(mosh_wcwidth(0x1F600), 2);     // grinning face
    CHECK_EQ(mosh_wcwidth(0x1FAE8), 2);     // shaking face (Unicode 15.0)
    CHECK_EQ(mosh_wcwidth(0x20000), 2);     // CJK extension B
    CHECK_EQ(mosh_wcwidth(0xE0001), 0);     // language tag
    CHECK_EQ(mosh_wcwidth(0x10FFFD), 1);
    CHECK_EQ(mosh_wcwidth(0x10FFFF), -1);   // noncharacter
    CHECK_EQ(mosh_wcwidth(0x110000), -1);   // not a code point
}

namespace {

/// A screen as the client's copy of it is built: mosh's emulator fed with
/// escape sequences.
Terminal::Complete screen(int cols, int rows, const std::string& bytes) {
    Terminal::Complete t(static_cast<size_t>(cols), static_cast<size_t>(rows));
    t.act(bytes);
    return t;
}

/// The cells' text and widths, row by row, for comparing screens.
std::string text_of(const Terminal::Framebuffer& fb) {
    std::string out;
    for (int y = 0; y < fb.ds.get_height(); ++y) {
        for (int x = 0; x < fb.ds.get_width(); ++x) {
            const Terminal::Cell* c = fb.get_cell(y, x);
            c->print_grapheme(out);
            if (c->get_wide()) ++x;
        }
        out += '\n';
    }
    return out;
}

}  // namespace

TEST(display_writes_xterm_256color) {
    Terminal::Display display(true);
    // Entering and leaving: xterm-256color's alternate screen, application
    // cursor keys, and everything a session may have turned on turned off.
    CHECK_EQ(display.open(), std::string("\x1b[?1049h\x1b[22;0;0t\x1b[?1h"));
    CHECK_EQ(display.close(), std::string("\x1b[?1l\x1b[0m\x1b[?25h\x1b[?1003l\x1b[?1002l\x1b[?1001l\x1b[?1000l"
                                          "\x1b[?1015l\x1b[?1006l\x1b[?1005l\x1b[?1049l\x1b[23;0;0t"));

    // The first frame of a known screen, exactly.
    Terminal::Framebuffer blank(10, 2);
    Terminal::Complete hi = screen(10, 2, "\x1b[1;31mhi\x1b[0m");
    CHECK_EQ(display.new_frame(false, blank, hi.get_fb()),
             std::string("\x1b[?5l"                 // no reverse video
                         "\x1b[r\x1b[0m\x1b[H\x1b[2J"  // reset scrolling region, clear
                         "\x1b[?25l"                // cursor off while drawing
                         "\x1b[0;1;31mhi"           // bold red text
                         "\x1b[0m\x1b[K\r\n"        // rest of the row erased (bce)
                         "\x1b[K"                   // second row
                         "\x1b[1;3H\x1b[?25h"       // cursor after "hi", visible
                         "\x1b[0m"                  // current rendition
                         "\x1b[?2004l"              // no bracketed paste
                         "\x1b[?1003l\x1b[?1002l\x1b[?1001l\x1b[?1000l"  // no mouse reports
                         "\x1b[?1004l"                                    // no focus events
                         "\x1b[?1015l\x1b[?1006l\x1b[?1005l"));          // default mouse encoding

    // After that, only what changed.
    Terminal::Complete ho = screen(10, 2, "\x1b[1;31mhi\x1b[0m\bo");
    CHECK_EQ(display.new_frame(true, hi.get_fb(), ho.get_fb()), std::string("\x1b[?25l\bo\x1b[?25h"));
    CHECK_EQ(display.new_frame(true, ho.get_fb(), ho.get_fb()), std::string());
}

TEST(display_reproduces_screens) {
    // Whatever the screen holds, its frame drawn on an empty terminal gives
    // the same screen back: wide characters, combining marks, emoji, 256
    // and true colours, titles, scrolled text.
    const std::string screens[] = {
        "plain text\r\nsecond line",
        "\x1b[38;5;208m256 colours\x1b[48;2;10;20;30m true colour\x1b[0m",
        "\xe4\xb8\xad\xe6\x96\x87 wide, e\xcc\x81 combining, \xf0\x9f\x98\x80 emoji",
        "\x1b]0;window title\x07title set",
        "\x1b[3;5Hpositioned\x1b[1;1H\x1b[7mreverse\x1b[0m",
        "1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8",  // scrolls a 6-row screen
        "\x1b[?25l\x1b[?2004hhidden cursor, bracketed paste",
        "long line that wraps around the right edge of the screen and on",
    };
    Terminal::Display display(true);
    for (const std::string& bytes : screens) {
        Terminal::Complete original = screen(20, 6, bytes);
        Terminal::Framebuffer blank(20, 6);
        std::string frame = display.new_frame(false, blank, original.get_fb());
        Terminal::Complete copy = screen(20, 6, frame);
        CHECK_EQ(text_of(copy.get_fb()), text_of(original.get_fb()));
        CHECK(!original.compare(copy));  // mosh's own cell-by-cell comparison
    }
}
CHECK_MAIN
