#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

class Range {
public:
    Range(int start, int len) : s(start), f(start + len) {}

    Range intersect(const Range& r) const {
        Range sect;
        sect.s = std::max(s, r.s);
        sect.f = std::min(f, r.f);
        if (sect.s > sect.f) {
            sect.s = sect.f = f;
        }
        return sect;
    }

    Range merge (const Range& r) const {
        return Range(start(), r.end());
    }

    Range move_start_by(int d) const {
        Range r;
        r.s = std::min(s + d, f);
        r.f = f;
        return r;
    }

    inline int start() const { return s; }
    inline int end() const { return f; }
    inline int len() const { return f - s; }

private:
    Range() { }
    int s, f;
};

uint32_t find_file_size(FILE* fd) {
    fseek(fd, 0L, SEEK_END);
    uint32_t size = ftell(fd);
    fseek(fd, 0L, SEEK_SET);
    return size;
}

#endif // UTIL_H_INCLUDED
