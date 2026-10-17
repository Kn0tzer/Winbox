#ifndef BOXEDWINE_XWIRE_KEYBOARD_H
#define BOXEDWINE_XWIRE_KEYBOARD_H
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// Fixed US XKB client map. Wire layouts follow X11/extensions/XKBproto.h.
// Wine 10 uses XkbKeycodeToKeysym to determine Windows physical scan codes;
// core GetKeyboardMapping alone leaves those keys unidentified in Xlib.
template<class Symbols>
std::vector<uint8_t> xwireXkbReply(const uint8_t* req, uint32_t len, uint16_t sequence, Symbols symbols) {
    if (len < 4) return {};
    const uint8_t op = req[1];
    if (op == 1 || op == 2 || op == 3 || op == 5 || op == 7 || op == 9 || op == 11 || op == 14 || op == 16 || op == 18) return {};
    if (len < 8) return {};
    auto get16 = [](const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); };
    std::vector<uint8_t> out(op == 8 ? 40 : op == 6 ? 92 : 32, 0);
    out[0] = 1; out[1] = 3;
    auto put16 = [&](size_t off, uint16_t v) { out[off] = v; out[off+1] = v >> 8; };
    auto put32 = [&](size_t off, uint32_t v) { for (int i=0;i<4;i++) out[off+i]=v>>(i*8); };
    put16(2, sequence);
    if (op == 0) {
        out[1] = get16(req+4) == 1; put16(8,1); put16(10,0);
    } else if (op == 8) {
        if (len < 28) return {};
        const uint16_t full=get16(req+6), partial=get16(req+8), present=(full|partial)&7;
        put16(12,present); out[10]=8; out[11]=255; out[16]=4;
        if (present&1) {
            const unsigned first=full&1?0:std::min<unsigned>(req[10],4);
            const unsigned count=full&1?4:std::min<unsigned>(req[11],4-first);
            out[14]=first; out[15]=count;
            for(unsigned type=first;type<first+count;type++) {
                const bool alphabetic=type==2; const uint8_t mask=type==0?0:alphabetic?3:1;
                const uint8_t entries=type==0?0:alphabetic?3:1;
                size_t at=out.size();out.resize(at+8,0);
                out[at]=out[at+1]=mask;out[at+4]=type==0?1:2;out[at+5]=entries;
                for(unsigned i=0;i<entries;i++) {
                    at=out.size();out.resize(at+8,0);out[at]=1;
                    out[at+1]=out[at+3]=i+1;out[at+2]=alphabetic&&i==2?0:1;
                }
            }
        }
        if (present&2) {
            unsigned first=full&2?8:std::max<unsigned>(8,req[12]);
            unsigned count=full&2?248:std::min<unsigned>(req[13],256-first);
            out[17]=first;put16(18,count*2);out[20]=count;
            for(unsigned key=first;key<first+count;key++) {
                const auto sym=symbols(key);size_t at=out.size();out.resize(at+16,0);
                out[at]=sym[0]>='a'&&sym[0]<='z'?2:1;
                out[at+4]=1;out[at+5]=2;put16(at+6,2);put32(at+8,sym[0]);put32(at+12,sym[1]);
            }
        }
        if (present&4) {
            const unsigned first=full&4?8:std::max<unsigned>(8,req[22]);
            const unsigned count=full&4?248:std::min<unsigned>(req[23],256-first);
            const std::array<std::array<uint8_t,2>,5> modifiers={{{50,1},{62,1},{37,4},{105,4},{64,8}}};
            out[31]=first;out[32]=count;
            for(auto m:modifiers)if(m[0]>=first&&m[0]<first+count){out.push_back(m[0]);out.push_back(m[1]);out[33]++;}
        }
    } else if (op == 17) {
        out[12]=8;out[13]=255; // no optional names are required by the symbol map
    } else if (op == 6) {
        out[9]=1;put16(20,660);put16(22,40);
    } else if (op != 4 && op != 10 && op != 12 && op != 13 && op != 21) {
        return {};
    }
    while(out.size()%4)out.push_back(0);
    put32(4,(out.size()-32)/4);
    return out;
}
#endif
