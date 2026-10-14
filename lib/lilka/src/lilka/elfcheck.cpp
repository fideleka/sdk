#include "elfcheck.h"
#include <string.h>
namespace {
struct Header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Section {
    uint32_t name, type, flags, addr, offset, size, link, info, alignment, entsize;
};
struct Symbol {
    uint32_t name, value, size;
    uint8_t info, other;
    uint16_t section;
};
struct Relocation {
    uint32_t offset, info;
    int32_t addend;
};
bool span(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}
bool text(const uint8_t* bytes, const Section& strings, uint32_t offset) {
    return offset < strings.size && memchr(bytes + strings.offset + offset, 0, strings.size - offset);
}
bool mapped(const Section* sections, int count, uint64_t loaded, uint32_t address, uint32_t length) {
    for (int i = 0; i < count; ++i) {
        if (!(loaded & (uint64_t(1) << i))) continue;
        if (address >= sections[i].addr && span(address - sections[i].addr, length, sections[i].size)) return true;
    }
    return false;
}
} // namespace
bool validateDriverElf(const uint8_t* bytes, size_t size) {
    if (!bytes || size < sizeof(Header) || size > 65536) return false;
    Header h;
    memcpy(&h, bytes, sizeof(h));
    if (memcmp(h.ident, "\177ELF\1\1\1", 7) || h.type != 3 || h.machine != 94 || h.version != 1 ||
        h.ehsize != sizeof(Header) || h.shentsize != sizeof(Section) || !h.shnum || h.shnum > 64 ||
        h.shstrndx >= h.shnum || h.shoff % 4 || !span(h.shoff, h.shnum * sizeof(Section), size))
        return false;
    Section sections[64];
    memcpy(sections, bytes + h.shoff, h.shnum * sizeof(Section));
    const auto& strings = sections[h.shstrndx];
    if (strings.type != 3 || !span(strings.offset, strings.size, size)) return false;
    uint32_t data = 0, code = 0;
    unsigned loadedNames = 0;
    uint64_t loaded = 0;
    bool dynsymSeen = false;
    for (int i = 0; i < h.shnum; ++i) {
        const auto& s = sections[i];
        if (!text(bytes, strings, s.name) || (s.type != 8 && !span(s.offset, s.size, size)) ||
            (s.alignment && (s.alignment > 16 || (s.alignment & (s.alignment - 1)))))
            return false;
        const char* name = reinterpret_cast<const char*>(bytes + strings.offset + s.name);
        if ((s.flags & 2) && s.size && (s.type == 1 || s.type == 8)) {
            unsigned bit = !strcmp(name, ".text")          ? 1
                           : !strcmp(name, ".data")        ? 2
                           : !strcmp(name, ".rodata")      ? 4
                           : !strcmp(name, ".data.rel.ro") ? 8
                           : !strcmp(name, ".bss")         ? 16
                                                           : 0;
            if (!bit && !strcmp(name, ".got.loc") && s.size <= 256) continue;
            if (!bit || (loadedNames & bit) || s.addr > UINT32_MAX - s.size) return false;
            if (s.addr % 4) return false;
            loaded |= uint64_t(1) << i;
            loadedNames |= bit;
            if (bit == 1) {
                code = s.size;
                if (!(s.flags & 4) || !span(h.entry - s.addr, 1, s.size) || h.entry < s.addr) return false;
            } else {
                if (s.size > 32768 - data || s.size % 4) return false;
                data += s.size;
            }
            for (int j = 0; j < i; ++j) {
                const auto& previous = sections[j];
                if ((previous.flags & 2) && previous.size && (previous.type == 1 || previous.type == 8) &&
                    s.addr < previous.addr + previous.size && previous.addr < s.addr + s.size)
                    return false;
            }
        }
        if (s.type == 2 || s.type == 11) {
            if (s.type == 11) {
                if (dynsymSeen || s.size / sizeof(Symbol) > 16) return false;
                dynsymSeen = true;
            }
            if (s.link >= h.shnum || sections[s.link].type != 3 || s.entsize != sizeof(Symbol) ||
                s.size % sizeof(Symbol) || s.size / sizeof(Symbol) > 256 || s.offset % 4)
                return false;
            const auto& names = sections[s.link];
            if (!span(names.offset, names.size, size)) return false;
            for (uint32_t pos = 0; pos < s.size; pos += sizeof(Symbol)) {
                Symbol symbol;
                memcpy(&symbol, bytes + s.offset + pos, sizeof(symbol));
                if (!text(bytes, names, symbol.name) ||
                    (symbol.section && symbol.section >= h.shnum && symbol.section != 0xfff1))
                    return false;
                if (strnlen(
                        reinterpret_cast<const char*>(bytes + names.offset + symbol.name), names.size - symbol.name
                    ) > 63)
                    return false;
                // ABI-table-only modules have no external imports.
                if (!symbol.section && symbol.name) return false;
            }
        }
    }
    if (!code || code > 8192) return false;
    for (int i = 0; i < h.shnum; ++i) {
        const auto& s = sections[i];
        if (s.type != 4) continue;
        if (s.link >= h.shnum || (sections[s.link].type != 2 && sections[s.link].type != 11) ||
            s.entsize != sizeof(Relocation) || s.size % sizeof(Relocation) || s.offset % 4)
            return false;
        for (uint32_t pos = 0; pos < s.size; pos += sizeof(Relocation)) {
            Relocation r;
            memcpy(&r, bytes + s.offset + pos, sizeof(r));
            if (((r.info & 255) != 0 && (r.info & 255) != 2 && (r.info & 255) != 5) ||
                (r.info >> 8) >= sections[s.link].size / sizeof(Symbol) || r.offset % 4 ||
                !mapped(sections, h.shnum, loaded, r.offset, 4))
                return false;
            if ((r.info & 255) == 5) {
                uint32_t value = 0;
                bool found = false;
                for (unsigned j = 0; j < h.shnum; ++j) {
                    const auto& target = sections[j];
                    if (!(loaded & (uint64_t(1) << j)) || target.type == 8 || r.offset < target.addr ||
                        !span(r.offset - target.addr, 4, target.size))
                        continue;
                    memcpy(&value, bytes + target.offset + r.offset - target.addr, sizeof(value));
                    found = true;
                    break;
                }
                if (!found || !mapped(sections, h.shnum, loaded, value, 1)) return false;
            }
        }
    }
    return true;
}
