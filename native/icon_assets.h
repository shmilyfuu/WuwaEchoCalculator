#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace EmbeddedIcons {
inline constexpr char kArrowDown[] = "iVBORw0KGgoAAAANSUhEUgAAABQAAAAUCAYAAACNiR0NAAAACXBIWXMAABYlAAAWJQFJUiTwAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAOdEVYdFNvZnR3YXJlAEZpZ21hnrGWYwAAAFhJREFUeAHt0rENACAIRFFGdAPZxNFRCxMLiJw0FPyECnmVRFXeRITXNALbN2u6tjh1AONzpC0Hgt7YvrUeuVAX5kUh7IV+YRYawgw0hhloDLtQFuB/VsmaLaHks/++dBEAAAAASUVORK5CYII=";
inline constexpr char kStar[] = "iVBORw0KGgoAAAANSUhEUgAAABYAAAAWCAYAAADEtGw7AAAACXBIWXMAABYlAAAWJQFJUiTwAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAOdEVYdFNvZnR3YXJlAEZpZ21hnrGWYwAAAKBJREFUeAHNlNENgCAMRIvxX50ARnD/H1dwBJxANqhtgpH4dfdh5JLTiC8XaBtE/paqJnNE+UFwzeYFhUfB5cGKwuyOEwozwXB9XWwpZhRmg2F1EZwIVoI/fPjlqWGq/+Jrba3ruzmbS+Oj/Q4h5HsqtHmf5klwlcq3GbjsVJsb5ZkaZ4KlggvB9hHsnYebytwVroyCbPPgUeqmedzwf6EL/74sE+9xXKkAAAAASUVORK5CYII=";
inline constexpr char kNormal[] = "iVBORw0KGgoAAAANSUhEUgAAABwAAAAcCAYAAAByDd+UAAAACXBIWXMAABYlAAAWJQFJUiTwAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAOdEVYdFNvZnR3YXJlAEZpZ21hnrGWYwAAAl1JREFUeAHNls9rE1EQx2fWRSQgBOlBRGRFBU+SSo0IgumxXkxvHoRsoHrQtNSbN+vNo9gWPAjZgnfrX9AIPVgbSI6KhwbRWyoLQqWSzTjz2F02u+km2V2hXwjvx7x9n8y8X4Mwhmq7ZgkB7hFACYAM7sq7JhsA22xrs+3D2g2rMWoujDMufjZN0Og5ERgwhnhcBzRtZX2mvjERcLllGr0evedqARJIwI6Ds29uWZ2wTQt3PGavGNZKChMhgqHr1Ko1zXLEFmyoAaQ8y0yEmhkMsQ90wyie5SFb2b0eTnvh9UPqOLT1H2CiPIfXj5oCym4cdycmVOFJs1rxgbL1IUPdPVeGa/nrg539/ooUKIeal3YLMtIDYwFuTt2Gb7+/wOuvL0NWnNXkBoGM5MH2D7vwbu9txC4sjVKct6Ng4tmvv93IGAIq8RpSBJjTc9E1SAlzZcimiRyFhUtL8PDyklr8DGGivDasd2d/W5VzDIyDTghTOlF8VFjm8lSw8+fBd/WxhPXK6avqOpJdlxbGsgV4nytnw5Y4aEKY6JOOgA0asnFEO91tHzDnhvbMyamkMP7T2BYPD7luHjUo7On53IVEMAUk7Zl6LRablb1Rd6l4JZ4mhcmjvF7cuKirVh9fML4e94GE94d4y8A/zgFMLE49pPDfw9puJdUrHyfPO8X1OnUd50FlYZnLlvzGa/jAV9NWBwmfQtZCrAaTqYGbZrVoWTwgK09tQjLXZqzNAf6wkZLfSMqRIgtocx4zPyxNPB6JcBRcvQPYL7tvp/wCqT50eJIGkLa5Wqx/HDXXP1yqOf4QJNS8AAAAAElFTkSuQmCC";
inline constexpr char kAbnormal[] = "iVBORw0KGgoAAAANSUhEUgAAABwAAAAcCAYAAAByDd+UAAAACXBIWXMAABYlAAAWJQFJUiTwAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAOdEVYdFNvZnR3YXJlAEZpZ21hnrGWYwAAAcJJREFUeAG9lktSwkAQhv8Zg1XuOIAWSekBZM1COIF4AuMRPIF4Ez2BeAJw4VoOoJVQXiBrHhm7JxADSpLJ66uCTCWZ/tM9090jkIOlh76UuKZhXwE2XdubRwEEZkJhFoZ4bTmYZtkSaQ9XHlwh8bARyYNPv5HVwTNMBJUHO5R4IaFLFMNfhhicOPoDdpD7Nxbk1Vrio4QYY7fIBkVouP9gx0P9AnmGanGTIY4FOYzsGX43RFUEFN7uNrxxSGnNJjWIMe3jRNS0IO9Gg52I8x5w0UNueD+s5rjlsQ7peg7PRJDWRENGTPBpniM5qU3ESmBHBSSqII3AWlKJUvlmhiIP6a8xQXLO5l1aRyocoi3RMCwYoDkCixLRNy3UndOMvnYI6psWXaYwFPx6RyEENWvJnRoNQXk4LlTauJbyxE8zT6PSxiMV4hH1M+K/eO3Jy7JdPg3tHQ/iPJQhblBPinADHsQ624HgjhziHlUT4i55mNqpNJaDJ1TnKdtwyeY4eTPtmDgp2ifJ6GxBH57rmKgn0ItHvMgUDoG/k1Lgd12a2/1PbPMx2ahvXFGBGCpuZVH/jI/6dMz36d5UJ/UZ3rJs/QBgY5Dnp5tL2QAAAABJRU5ErkJggg==";
inline constexpr char kManual[] = "iVBORw0KGgoAAAANSUhEUgAAACgAAAAYCAYAAACIhL/AAAAACXBIWXMAABYlAAAWJQFJUiTwAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAOdEVYdFNvZnR3YXJlAEZpZ21hnrGWYwAAAtpJREFUeAHNV81uElEUPtDBBETBdCEkGjHRmuhmWDaxCcWNGo28gfAAtmXpyrrRZSt9AOxSu5lulF3pT8KS2ciimjLGJtAFFmyliQyO5xy4pPwNII3Ol0y4nDv3zHe/83NnbICIbBgBXYKkzQDZAPDC/4YNFEmHuDJr02xErj4BWUsQa0dZqkPQjuSWLEiO4NUnIGlHchGwLmQ7WBteqxOEsQjWjsrw47MKpWx66DXVgga7yUU4KWpD3S+ZTVbw4Qc7SptzAjmnq1r82pq7/+EQJHej1nKJBfh1XG7zdfVBFCaDIV63m3zJY6cvAGMRdPkDsJ9a5bFwdlIgYhpMxV7w/IUbMpzHOUGON3KgQe24AmcB26MtwxhlAYWHFMB1MCoycyHQkXjliwoXcWMOt4ftd+aW+X8vmCpYwzDl15bbbKXsZovoaVAIB4XM6b/OESCQ+pTDJTXNz+mHgQRFiFu2o4azTrvIqeK2ArmVhS5fNDedSHNBZebTcPvZEpOlsRnMcxCdht/l22wixJ3200SuoJrfPq7ikQo4fsp2RzNHe1Wvw+0dnSC3EMyVTlQLjcrt1VqInOemzBcR9M08gUk5xEVFKcDrmwRp8yLc0t8QpPaivo71XZiZn+2yyc+TmFtRbk+k1OWZCG8kv/YG/DgmIjTn6VMQIxH0ocMw5lUnNCyaPXxg+H13iM81ldhPvWWFSD2qTrp/D9dNxRaxKDaZ7NgEabe9pBc2V5+KpdQobq9z/qmvolDcWcfWUmYVaS2NRV6aVa/AmZ/FlLcUXnp47WcFbmFDv4dqk5q5lXhLWYLIY5dJe5JgSKQeXuLdD3JI7YbC33mP03+Nf6cTG/AJ2xApSvANCPfQJwk1VNEifHcjppXXC6Tod1SMCof80GlC7UWo2Zfg4y3j0KJv1Ax6o1bBulDseh1iWHGDy+lfw4AycovbU/hpV8OvJySpgBVgsFhp/TcEidsflUdM3Ro6UUsAAAAASUVORK5CYII=";

inline std::vector<std::uint8_t> DecodeBase64(std::string_view text) {
    static constexpr signed char table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
        -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    std::vector<std::uint8_t> output;
    output.reserve(text.size() * 3 / 4);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (unsigned char ch : text) {
        signed char value = ch < 128 ? table[ch] : -1;
        if (value == -2) break;
        if (value < 0) continue;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
        }
    }
    return output;
}
}
