#include <not_implemented.h>
#include "../include/allocator_boundary_tags.h"
#include <memory>
#include <new>
#include <stdexcept>

namespace
{
constexpr size_t kAllocatorMetadataSize = sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t) + sizeof(std::mutex) + sizeof(void*);
constexpr size_t kOccupiedBlockMetadataSize = sizeof(size_t) + sizeof(void*) + sizeof(void*) + sizeof(void*);
constexpr size_t parent_off = 0;
constexpr size_t mode_off = parent_off + sizeof(std::pmr::memory_resource*);
constexpr size_t managed_off = mode_off + sizeof(allocator_with_fit_mode::fit_mode);
constexpr size_t mutex_off = managed_off + sizeof(size_t);
constexpr size_t first_occ_off = mutex_off + sizeof(std::mutex);

char* base_ptr(void* trusted)
{
    return reinterpret_cast<char*>(trusted);
}

const char* base_ptr(const void* trusted)
{
    return reinterpret_cast<const char*>(trusted);
}

std::pmr::memory_resource*& parent_ref(char* base)
{
    return *reinterpret_cast<std::pmr::memory_resource**>(base + parent_off);
}

allocator_with_fit_mode::fit_mode& mode_ref(char* base)
{
    return *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(base + mode_off);
}

size_t& managed_ref(char* base)
{
    return *reinterpret_cast<size_t*>(base + managed_off);
}

std::mutex* mutex_ptr(char* base)
{
    return reinterpret_cast<std::mutex*>(base + mutex_off);
}

void*& first_occ_ref(char* base)
{
    return *reinterpret_cast<void**>(base + first_occ_off);
}

size_t& block_payload_size(void* block)
{
    return *reinterpret_cast<size_t*>(reinterpret_cast<char*>(block));
}

void*& block_prev(void* block)
{
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(block) + sizeof(size_t));
}

void*& block_next(void* block)
{
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(block) + sizeof(size_t) + sizeof(void*));
}

void*& block_owner(void* block)
{
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(block) + sizeof(size_t) + sizeof(void*) * 2);
}

char* block_payload(void* block)
{
    return reinterpret_cast<char*>(block) + kOccupiedBlockMetadataSize;
}

const char* block_payload(const void* block)
{
    return reinterpret_cast<const char*>(block) + kOccupiedBlockMetadataSize;
}

char* block_end(void* block)
{
    return block_payload(block) + block_payload_size(block);
}

const char* block_end(const void* block)
{
    auto* mutable_block = const_cast<void*>(block);
    return block_payload(mutable_block) + block_payload_size(mutable_block);
}
}

allocator_boundary_tags::~allocator_boundary_tags()
{
    if (_trusted_memory == nullptr)
    {
        return;
    }
    char* base = base_ptr(_trusted_memory);
    auto* parent = parent_ref(base);
    const size_t managed = managed_ref(base);
    auto* mtx = mutex_ptr(base);
    std::destroy_at(mtx);
    parent->deallocate(_trusted_memory, kAllocatorMetadataSize + managed, alignof(std::max_align_t));
    _trusted_memory = nullptr;
}

allocator_boundary_tags::allocator_boundary_tags(
    allocator_boundary_tags &&other) noexcept
{
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
    allocator_boundary_tags &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    this->~allocator_boundary_tags();
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
    return *this;
}

allocator_boundary_tags::allocator_boundary_tags(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (parent_allocator == nullptr)
    {
        parent_allocator = std::pmr::get_default_resource();
    }
    const size_t total_size = kAllocatorMetadataSize + space_size;
    _trusted_memory = parent_allocator->allocate(total_size, alignof(std::max_align_t));
    char* base = base_ptr(_trusted_memory);
    parent_ref(base) = parent_allocator;
    mode_ref(base) = allocate_fit_mode;
    managed_ref(base) = space_size;
    new (base + mutex_off) std::mutex();
    first_occ_ref(base) = nullptr;
}

[[nodiscard]] void *allocator_boundary_tags::do_allocate_sm(
    size_t size)
{
    char* base = base_ptr(_trusted_memory);
    auto* mtx = mutex_ptr(base);
    std::lock_guard<std::mutex> lock(*mtx);
    const size_t needed = kOccupiedBlockMetadataSize + size;
    const auto mode = mode_ref(base);
    void* head = first_occ_ref(base);
    char* region_begin = base + kAllocatorMetadataSize;
    char* region_end = region_begin + managed_ref(base);
    if (needed > managed_ref(base))
    {
        throw std::bad_alloc();
    }
    void* best_prev = nullptr;
    void* best_next = nullptr;
    char* best_start = nullptr;
    size_t best_gap = 0;
    auto consider_gap = [&](char* gap_start, char* gap_end, void* prev_occ, void* next_occ)
    {
        if (gap_end < gap_start)
        {
            return;
        }
        const size_t gap = static_cast<size_t>(gap_end - gap_start);
        if (gap < needed)
        {
            return;
        }
        if (best_start == nullptr)
        {
            best_start = gap_start;
            best_prev = prev_occ;
            best_next = next_occ;
            best_gap = gap;
            return;
        }
        if (mode == allocator_with_fit_mode::fit_mode::the_best_fit && gap < best_gap)
        {
            best_start = gap_start;
            best_prev = prev_occ;
            best_next = next_occ;
            best_gap = gap;
            return;
        }
        if (mode == allocator_with_fit_mode::fit_mode::the_worst_fit && gap > best_gap)
        {
            best_start = gap_start;
            best_prev = prev_occ;
            best_next = next_occ;
            best_gap = gap;
            return;
        }
    };
    if (mode == allocator_with_fit_mode::fit_mode::first_fit)
    {
        void* prev = nullptr;
        void* cur = head;
        char* cursor = region_begin;
        while (cur != nullptr)
        {
            char* cur_start = reinterpret_cast<char*>(cur);
            const size_t gap = static_cast<size_t>(cur_start - cursor);
            if (gap >= needed)
            {
                best_start = cursor;
                best_prev = prev;
                best_next = cur;
                best_gap = gap;
                break;
            }
            cursor = block_end(cur);
            prev = cur;
            cur = block_next(cur);
        }
        if (best_start == nullptr)
        {
            const size_t gap = static_cast<size_t>(region_end - cursor);
            if (gap >= needed)
            {
                best_start = cursor;
                best_prev = prev;
                best_next = nullptr;
                best_gap = gap;
            }
        }
    }
    else
    {
        void* prev = nullptr;
        void* cur = head;
        char* cursor = region_begin;
        while (cur != nullptr)
        {
            char* cur_start = reinterpret_cast<char*>(cur);
            consider_gap(cursor, cur_start, prev, cur);
            cursor = block_end(cur);
            prev = cur;
            cur = block_next(cur);
        }
        consider_gap(cursor, region_end, prev, nullptr);
    }
    if (best_start == nullptr)
    {
        throw std::bad_alloc();
    }
    void* block = best_start;
    size_t payload_size = size;
    if (best_gap > needed && best_gap - needed < kOccupiedBlockMetadataSize)
    {
        payload_size = best_gap - kOccupiedBlockMetadataSize;
    }
    block_payload_size(block) = payload_size;
    block_prev(block) = best_prev;
    block_next(block) = best_next;
    block_owner(block) = _trusted_memory;
    if (best_prev != nullptr)
    {
        block_next(best_prev) = block;
    }
    else
    {
        first_occ_ref(base) = block;
    }
    if (best_next != nullptr)
    {
        block_prev(best_next) = block;
    }
    return block_payload(block);
}

void allocator_boundary_tags::do_deallocate_sm(
    void *at)
{
    if (at == nullptr)
    {
        return;
    }
    char* base = base_ptr(_trusted_memory);
    auto* mtx = mutex_ptr(base);
    std::lock_guard<std::mutex> lock(*mtx);
    char* region_begin = base + kAllocatorMetadataSize;
    char* region_end = region_begin + managed_ref(base);
    char* payload = reinterpret_cast<char*>(at);
    if (payload < region_begin + kOccupiedBlockMetadataSize || payload > region_end)
    {
        throw std::invalid_argument("");
    }
    void* block = payload - kOccupiedBlockMetadataSize;
    if (block_owner(block) != _trusted_memory)
    {
        throw std::invalid_argument("");
    }
    void* prev = block_prev(block);
    void* next = block_next(block);
    if (prev != nullptr)
    {
        block_next(prev) = next;
    }
    else
    {
        first_occ_ref(base) = next;
    }
    if (next != nullptr)
    {
        block_prev(next) = prev;
    }
    block_owner(block) = nullptr;
    block_prev(block) = nullptr;
    block_next(block) = nullptr;
    block_payload_size(block) = 0;
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    char* base = base_ptr(_trusted_memory);
    auto* mtx = mutex_ptr(base);
    std::lock_guard<std::mutex> lock(*mtx);
    mode_ref(base) = mode;
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const
{
    if (_trusted_memory == nullptr)
    {
        return {};
    }
    char* base = base_ptr(_trusted_memory);
    auto* mtx = mutex_ptr(base);
    std::lock_guard<std::mutex> lock(*mtx);
    return get_blocks_info_inner();
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept
{
    if (_trusted_memory == nullptr)
    {
        return boundary_iterator();
    }
    return boundary_iterator(_trusted_memory);
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept
{
    return boundary_iterator();
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;
    if (_trusted_memory == nullptr)
    {
        return result;
    }
    char* base = base_ptr(_trusted_memory);
    char* region_begin = base + kAllocatorMetadataSize;
    char* region_end = region_begin + managed_ref(base);
    void* cur_occ = first_occ_ref(base);
    char* cursor = region_begin;
    while (cursor < region_end)
    {
        if (cur_occ != nullptr && reinterpret_cast<char*>(cur_occ) == cursor)
        {
            const size_t bytes = kOccupiedBlockMetadataSize + block_payload_size(cur_occ);
            result.push_back({bytes, true});
            cursor += bytes;
            cur_occ = block_next(cur_occ);
            continue;
        }
        char* free_end = region_end;
        if (cur_occ != nullptr)
        {
            free_end = reinterpret_cast<char*>(cur_occ);
        }
        if (free_end <= cursor)
        {
            break;
        }
        const size_t free_bytes = static_cast<size_t>(free_end - cursor);
        result.push_back({free_bytes, false});
        cursor = free_end;
    }
    return result;
}

allocator_boundary_tags::allocator_boundary_tags(const allocator_boundary_tags &other)
{
    if (other._trusted_memory == nullptr)
    {
        _trusted_memory = nullptr;
        return;
    }
    char* other_base = base_ptr(other._trusted_memory);
    auto* parent = parent_ref(other_base);
    const auto mode = mode_ref(other_base);
    const size_t managed = managed_ref(other_base);
    if (parent == nullptr)
    {
        parent = std::pmr::get_default_resource();
    }
    _trusted_memory = parent->allocate(kAllocatorMetadataSize + managed, alignof(std::max_align_t));
    char* base = base_ptr(_trusted_memory);
    parent_ref(base) = parent;
    mode_ref(base) = mode;
    managed_ref(base) = managed;
    new (base + mutex_off) std::mutex();
    first_occ_ref(base) = nullptr;
}

allocator_boundary_tags &allocator_boundary_tags::operator=(const allocator_boundary_tags &other)
{
    if (this == &other)
    {
        return *this;
    }
    allocator_boundary_tags tmp(other);
    *this = std::move(tmp);
    return *this;
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return dynamic_cast<const allocator_boundary_tags*>(&other) != nullptr;
}

bool allocator_boundary_tags::boundary_iterator::operator==(
        const allocator_boundary_tags::boundary_iterator &other) const noexcept
{
    return _occupied_ptr == other._occupied_ptr && _occupied == other._occupied && _trusted_memory == other._trusted_memory;
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
        const allocator_boundary_tags::boundary_iterator & other) const noexcept
{
    return !(*this == other);
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    if (_trusted_memory == nullptr || _occupied_ptr == nullptr)
    {
        return *this;
    }
    char* base = base_ptr(_trusted_memory);
    char* region_begin = base + kAllocatorMetadataSize;
    char* region_end = region_begin + managed_ref(base);
    char* cur_start = reinterpret_cast<char*>(_occupied_ptr);
    if (_occupied)
    {
        char* next_start = const_cast<char*>(block_end(_occupied_ptr));
        if (next_start >= region_end)
        {
            _occupied_ptr = nullptr;
            _occupied = false;
            return *this;
        }
        void* cur = first_occ_ref(base);
        while (cur != nullptr && cur != _occupied_ptr)
        {
            cur = block_next(cur);
        }
        void* occ_next = cur == nullptr ? nullptr : block_next(cur);
        if (occ_next != nullptr && reinterpret_cast<char*>(occ_next) == next_start)
        {
            _occupied_ptr = occ_next;
            _occupied = true;
            return *this;
        }
        _occupied_ptr = next_start;
        _occupied = false;
        return *this;
    }
    void* cur = first_occ_ref(base);
    while (cur != nullptr && reinterpret_cast<char*>(cur) <= cur_start)
    {
        cur = block_next(cur);
    }
    if (cur == nullptr)
    {
        _occupied_ptr = nullptr;
        _occupied = false;
        return *this;
    }
    _occupied_ptr = cur;
    _occupied = true;
    return *this;
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    if (_trusted_memory == nullptr)
    {
        return *this;
    }
    char* base = base_ptr(_trusted_memory);
    char* region_begin = base + kAllocatorMetadataSize;
    char* region_end = region_begin + managed_ref(base);
    void* head = first_occ_ref(base);
    if (_occupied_ptr == nullptr)
    {
        if (region_begin >= region_end)
        {
            return *this;
        }
        if (head == nullptr)
        {
            _occupied_ptr = region_begin;
            _occupied = false;
            return *this;
        }
        void* tail = head;
        while (block_next(tail) != nullptr)
        {
            tail = block_next(tail);
        }
        char* tail_end = const_cast<char*>(block_end(tail));
        if (tail_end < region_end)
        {
            _occupied_ptr = tail_end;
            _occupied = false;
            return *this;
        }
        _occupied_ptr = tail;
        _occupied = true;
        return *this;
    }
    char* cur_start = reinterpret_cast<char*>(_occupied_ptr);
    if (cur_start <= region_begin)
    {
        return *this;
    }
    if (_occupied)
    {
        void* prev_occ = block_prev(_occupied_ptr);
        if (prev_occ == nullptr)
        {
            if (cur_start > region_begin)
            {
                _occupied_ptr = region_begin;
                _occupied = false;
            }
            return *this;
        }
        char* prev_end = const_cast<char*>(block_end(prev_occ));
        if (prev_end == cur_start)
        {
            _occupied_ptr = prev_occ;
            _occupied = true;
            return *this;
        }
        _occupied_ptr = prev_end;
        _occupied = false;
        return *this;
    }
    void* cur = head;
    void* prev_occ = nullptr;
    while (cur != nullptr && reinterpret_cast<char*>(cur) < cur_start)
    {
        prev_occ = cur;
        cur = block_next(cur);
    }
    if (prev_occ == nullptr)
    {
        _occupied_ptr = region_begin;
        _occupied = false;
        return *this;
    }
    _occupied_ptr = prev_occ;
    _occupied = true;
    return *this;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n)
{
    (void)n;
    boundary_iterator tmp(*this);
    ++(*this);
    return tmp;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n)
{
    (void)n;
    boundary_iterator tmp(*this);
    --(*this);
    return tmp;
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    if (_trusted_memory == nullptr || _occupied_ptr == nullptr)
    {
        return 0;
    }
    char* base = base_ptr(_trusted_memory);
    char* region_begin = base + kAllocatorMetadataSize;
    char* region_end = region_begin + managed_ref(base);
    if (_occupied)
    {
        return kOccupiedBlockMetadataSize + block_payload_size(_occupied_ptr);
    }
    char* cur = reinterpret_cast<char*>(_occupied_ptr);
    void* occ = first_occ_ref(base);
    while (occ != nullptr && reinterpret_cast<char*>(occ) <= cur)
    {
        occ = block_next(occ);
    }
    char* end = occ == nullptr ? region_end : reinterpret_cast<char*>(occ);
    if (end <= cur)
    {
        return 0;
    }
    return static_cast<size_t>(end - cur);
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    return _occupied_ptr != nullptr && _occupied;
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    if (_occupied_ptr == nullptr)
    {
        return nullptr;
    }
    if (_occupied)
    {
        return reinterpret_cast<char*>(_occupied_ptr) + kOccupiedBlockMetadataSize;
    }
    return _occupied_ptr;
}

allocator_boundary_tags::boundary_iterator::boundary_iterator()
{
    _occupied_ptr = nullptr;
    _occupied = false;
    _trusted_memory = nullptr;
}

allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted)
{
    _trusted_memory = trusted;
    if (trusted == nullptr)
    {
        _occupied_ptr = nullptr;
        _occupied = false;
        return;
    }
    char* base = base_ptr(trusted);
    char* region_begin = base + kAllocatorMetadataSize;
    char* region_end = region_begin + managed_ref(base);
    if (region_begin >= region_end)
    {
        _occupied_ptr = nullptr;
        _occupied = false;
        return;
    }
    void* head = first_occ_ref(base);
    if (head == nullptr)
    {
        _occupied_ptr = region_begin;
        _occupied = false;
        return;
    }
    if (reinterpret_cast<char*>(head) == region_begin)
    {
        _occupied_ptr = head;
        _occupied = true;
        return;
    }
    _occupied_ptr = region_begin;
    _occupied = false;
}

void *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    return _occupied_ptr;
}
