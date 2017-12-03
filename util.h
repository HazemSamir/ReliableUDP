#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <algorithm>

template<
    typename T, //real type
    typename = typename std::enable_if<std::is_arithmetic<T>::value, T>::type
> class Range {
public:
    Range(T start, T len) : s(start), f(start + len) {}

    Range<T> intersect(const Range<T>& r) const {
        Range sect;
        sect.s = std::max(s, r.s);
        sect.f = std::min(f, r.f);
        if (sect.s > sect.f) {
            sect.s = sect.f = f;
        }
        return sect;
    }

    Range<T> merge (const Range<T>& r) const {
        return Range(start(), r.end());
    }

    Range<T> move_start_by(T d) const {
        Range<T> r;
        r.s = std::min(s + d, f);
        r.f = f;
        return r;
    }

    inline T start() const { return s; }
    inline T end() const { return f; }
    inline T len() const { return f - s; }

private:
    Range() { }
    T s, f;
};

#endif // UTIL_H_INCLUDED
