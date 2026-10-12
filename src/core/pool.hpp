#pragma once

// Index-addressed trivially copyable T in its own mapping: indices survive growth, so state copies by memcpy.
// Mappings >= 2 MB are 2 MB aligned, THP-advised, then pre-faulted (MAP_POPULATE would fault 4 KB pages first).
// Growth within one page size moves the pages with mremap instead of copying (100-300 us inline otherwise).

#include <sys/mman.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#ifndef MADV_POPULATE_WRITE
#define MADV_POPULATE_WRITE 23
#endif

namespace hft {

struct PoolStats {
    static inline bool huge_pages = true;
    static inline std::uint64_t maps = 0;  // mappings created, growth included
    static inline std::uint64_t bytes = 0;
};

template <class T>
class Pool {
    static_assert(std::is_trivially_copyable_v<T>);

   public:
    explicit Pool(std::size_t capacity = 0) { reserve(capacity); }
    ~Pool() { unmap(); }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&& o) noexcept { *this = std::move(o); }
    Pool& operator=(Pool&& o) noexcept {
        if (this != &o) {
            unmap();
            base_ = std::exchange(o.base_, nullptr);
            map_len_ = std::exchange(o.map_len_, 0);
            data_ = std::exchange(o.data_, nullptr);
            cap_ = std::exchange(o.cap_, 0);
            huge_ = std::exchange(o.huge_, false);
            split_ = std::exchange(o.split_, false);
        }
        return *this;
    }

    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }
    T* data() { return data_; }
    const T* data() const { return data_; }
    std::size_t capacity() const { return cap_; }

    // Grows to at least n elements, keeping contents. New elements are zero.
    void reserve(std::size_t n) {
        if (n <= cap_) return;
        std::size_t want = cap_ ? cap_ : 64;
        while (want < n) want *= 2;
        const std::size_t len = want * sizeof(T);
        const bool huge = PoolStats::huge_pages && len >= kHuge;
        if (cap_ && (huge == huge_ || huge)) return grow(want, len, huge);
        const std::size_t map_len = huge ? len + kHuge : len;
        void* p =
            ::mmap(nullptr, map_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) throw std::bad_alloc();
        auto* d = static_cast<std::uint8_t*>(p);
        std::size_t keep = map_len;
        if (huge) {
            d += (kHuge - reinterpret_cast<std::uintptr_t>(d) % kHuge) % kHuge;
            // One VMA holding exactly the data, so a later mremap can move it whole.
            if (d > p) ::munmap(p, static_cast<std::size_t>(d - static_cast<std::uint8_t*>(p)));
            keep = page_up(len);
            auto* end = static_cast<std::uint8_t*>(p) + map_len;
            if (d + keep < end) ::munmap(d + keep, static_cast<std::size_t>(end - (d + keep)));
            ::madvise(d, keep, MADV_HUGEPAGE);
        }
        ::madvise(d, len, MADV_POPULATE_WRITE);
        if (cap_) std::memcpy(d, data_, cap_ * sizeof(T));
        unmap();
        base_ = d;
        map_len_ = keep;
        data_ = reinterpret_cast<T*>(d);
        cap_ = want;
        huge_ = huge;
        split_ = false;
        __atomic_fetch_add(&PoolStats::maps, 1, __ATOMIC_RELAXED);  // pools grow on several threads (book_replay, sweeps)
        __atomic_fetch_add(&PoolStats::bytes, map_len, __ATOMIC_RELAXED);
    }

    // Copies the first n elements of `o`, growing this pool if needed.
    void copy_from(const Pool& o, std::size_t n) {
        reserve(n);
        if (n) std::memcpy(data_, o.data_, n * sizeof(T));
    }

   private:
    static constexpr std::size_t kHuge = 2u << 20;

    static std::size_t page_up(std::size_t n) { return (n + 4095) / 4096 * 4096; }

    // The new range starts 2 MB aligned for huge pools, so moved huge pages stay huge. A pool that
    // outgrew 4 KB pages keeps its first 2 MB on them as a separate VMA: under MADV_HUGEPAGE
    // khugepaged would collapse them later, stalling the owner for hundreds of us.
    void grow(std::size_t want, std::size_t len, bool huge) {
        const std::size_t map_len = page_up(len), span = map_len + (huge ? kHuge : 0);
        void* r = ::mmap(nullptr, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (r == MAP_FAILED) throw std::bad_alloc();
        auto* rb = static_cast<std::uint8_t*>(r);
        std::uint8_t* d = rb;
        if (huge) d += (kHuge - reinterpret_cast<std::uintptr_t>(d) % kHuge) % kHuge;
        auto* old = static_cast<std::uint8_t*>(base_);
        bool ok;
        if (!split_) {
            ok = ::mremap(old, map_len_, map_len, MREMAP_MAYMOVE | MREMAP_FIXED, d) != MAP_FAILED;
        } else if (map_len_ > kHuge) {
            ok = ::mremap(old, kHuge, kHuge, MREMAP_MAYMOVE | MREMAP_FIXED, d) != MAP_FAILED &&
                 ::mremap(old + kHuge, map_len_ - kHuge, map_len - kHuge,
                          MREMAP_MAYMOVE | MREMAP_FIXED, d + kHuge) != MAP_FAILED;
        } else {  // crossed at exactly 2 MB: nothing above the first block to move yet
            ok = ::mremap(old, kHuge, kHuge, MREMAP_MAYMOVE | MREMAP_FIXED, d) != MAP_FAILED &&
                 ::mmap(d + kHuge, map_len - kHuge, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED;
        }
        if (!ok) throw std::bad_alloc();
        if (d > rb) ::munmap(rb, static_cast<std::size_t>(d - rb));
        if (d + map_len < rb + span) ::munmap(d + map_len, static_cast<std::size_t>(rb + span - (d + map_len)));
        const std::size_t old_len = cap_ * sizeof(T);
        if (huge && !huge_) {
            ::madvise(d, kHuge, MADV_NOHUGEPAGE);
            ::madvise(d + kHuge, map_len - kHuge, MADV_HUGEPAGE);
            split_ = true;
        } else if (huge) {
            // Whole pages: advising a partial last page would split the VMA and break mremap.
            ::madvise(d + (split_ ? kHuge : 0), map_len - (split_ ? kHuge : 0), MADV_HUGEPAGE);
        }
        if (huge) {
            const std::size_t from = old_len / 4096 * 4096;  // madvise needs a page-aligned start
            ::madvise(d + from, len - from, MADV_POPULATE_WRITE);
        }
        base_ = d;
        map_len_ = map_len;
        data_ = reinterpret_cast<T*>(d);
        cap_ = want;
        huge_ = huge;
        __atomic_fetch_add(&PoolStats::maps, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&PoolStats::bytes, map_len, __ATOMIC_RELAXED);
    }

    void unmap() {
        if (base_) ::munmap(base_, map_len_);
        base_ = nullptr;
    }

    void* base_ = nullptr;
    std::size_t map_len_ = 0;
    T* data_ = nullptr;
    std::size_t cap_ = 0;
    bool huge_ = false;
    bool split_ = false;  // first 2 MB is a separate 4 KB-page VMA
};

}  // namespace hft
