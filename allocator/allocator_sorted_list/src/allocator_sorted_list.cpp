#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"
#include <memory>
#include <stdexcept>

allocator_sorted_list::~allocator_sorted_list()
{
    if (_trusted_memory == nullptr)
    {
        return;
    }
    constexpr size_t parent_off = 0;
    constexpr size_t mode_off = parent_off + sizeof(std::pmr::memory_resource*);
    constexpr size_t managed_off = mode_off + sizeof(allocator_with_fit_mode::fit_mode);
    constexpr size_t mutex_off = managed_off + sizeof(size_t);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    auto* parent =
        *reinterpret_cast<std::pmr::memory_resource**>(base + parent_off);
    size_t managed =
        *reinterpret_cast<size_t*>(base + managed_off);
    auto* mtx =
        reinterpret_cast<std::mutex*>(base + mutex_off);
    std::destroy_at(mtx);
    const size_t total_size =
        allocator_metadata_size + block_metadata_size + managed;
    parent->deallocate(_trusted_memory, total_size);
    _trusted_memory = nullptr;
}

allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list &&other) noexcept
{
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
}

allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    this->~allocator_sorted_list();
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
    return *this;
}

allocator_sorted_list::allocator_sorted_list(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (parent_allocator == nullptr)
    {
        parent_allocator = std::pmr::get_default_resource();
    }
    constexpr size_t parent_off = 0;
    constexpr size_t mode_off = parent_off + sizeof(std::pmr::memory_resource*);
    constexpr size_t managed_off = mode_off + sizeof(allocator_with_fit_mode::fit_mode);
    constexpr size_t mutex_off = managed_off + sizeof(size_t);
    constexpr size_t free_head_off = mutex_off + sizeof(std::mutex);
    const size_t total_size = allocator_metadata_size + block_metadata_size + space_size;
    _trusted_memory = parent_allocator->allocate(total_size);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    *reinterpret_cast<std::pmr::memory_resource**>(base + parent_off) = parent_allocator;
    *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(
        base + mode_off
    ) = allocate_fit_mode;
    *reinterpret_cast<size_t*>(
        base + managed_off
    ) = space_size;
    new (base + mutex_off)
        std::mutex();
    void* first_block = base + allocator_metadata_size;
    *reinterpret_cast<void**>(base + free_head_off) = first_block;
    *reinterpret_cast<void**>(first_block) = nullptr;
    *reinterpret_cast<size_t*>(reinterpret_cast<char*>(first_block) + sizeof(void*)) = space_size;
}

[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
    size_t size)
{
    if (size == 0)
    {
        size = 1;
    }
    constexpr size_t mode_off = sizeof(std::pmr::memory_resource*);
    constexpr size_t mutex_off = mode_off + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t);
    constexpr size_t free_head_off = mutex_off + sizeof(std::mutex);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    auto* mtx = reinterpret_cast<std::mutex*>(base + mutex_off);
    std::lock_guard<std::mutex> lock(*mtx);
    auto mode = *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(base + mode_off);
    void*& free_head = *reinterpret_cast<void**>(base + free_head_off);
    void* chosen = nullptr;
    void* chosen_prev = nullptr;
    void* prev = nullptr;
    void* cur = free_head;
    while (cur != nullptr)
    {
        const size_t cur_sz = *reinterpret_cast<size_t*>(reinterpret_cast<char*>(cur) + sizeof(void*));
        if (cur_sz >= size)
        {
            if (mode == allocator_with_fit_mode::fit_mode::first_fit)
            {
                chosen = cur;
                chosen_prev = prev;
                break;
            }
            if (chosen == nullptr)
            {
                chosen = cur;
                chosen_prev = prev;
            }
            else if (mode == allocator_with_fit_mode::fit_mode::the_best_fit)
            {
                const size_t chosen_sz = *reinterpret_cast<size_t*>(reinterpret_cast<char*>(chosen) + sizeof(void*));
                if (cur_sz < chosen_sz)
                {
                    chosen = cur;
                    chosen_prev = prev;
                }
            }
            else if (mode == allocator_with_fit_mode::fit_mode::the_worst_fit)
            {
                const size_t chosen_sz = *reinterpret_cast<size_t*>(reinterpret_cast<char*>(chosen) + sizeof(void*));
                if (cur_sz > chosen_sz)
                {
                    chosen = cur;
                    chosen_prev = prev;
                }
            }
        }
        prev = cur;
        cur = *reinterpret_cast<void**>(cur);
    }
    if (chosen == nullptr)
    {
        throw std::bad_alloc();
    }
    const size_t free_sz = *reinterpret_cast<size_t*>(reinterpret_cast<char*>(chosen) + sizeof(void*));
    const size_t remainder = free_sz - size;
    constexpr size_t min_payload = 1;
    if (remainder >= block_metadata_size + min_payload)
    {
        void* new_free = reinterpret_cast<char*>(chosen) + block_metadata_size + size;
        *reinterpret_cast<void**>(new_free) = *reinterpret_cast<void**>(chosen);
        *reinterpret_cast<size_t*>(reinterpret_cast<char*>(new_free) + sizeof(void*)) = remainder - block_metadata_size;
        if (chosen_prev == nullptr)
        {
            free_head = new_free;
        }
        else
        {
            *reinterpret_cast<void**>(chosen_prev) = new_free;
        }
        *reinterpret_cast<size_t*>(reinterpret_cast<char*>(chosen) + sizeof(void*)) = size;
    }
    else
    {
        if (chosen_prev == nullptr)
        {
            free_head = *reinterpret_cast<void**>(chosen);
        }
        else
        {
            *reinterpret_cast<void**>(chosen_prev) = *reinterpret_cast<void**>(chosen);
        }
    }
    *reinterpret_cast<void**>(chosen) = _trusted_memory;
    return reinterpret_cast<char*>(chosen) + block_metadata_size;
}

allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list &other)
{
    if (other._trusted_memory == nullptr)
    {
        _trusted_memory = nullptr;
        return;
    }
    constexpr size_t parent_off = 0;
    constexpr size_t mode_off = parent_off + sizeof(std::pmr::memory_resource*);
    constexpr size_t managed_off = mode_off + sizeof(allocator_with_fit_mode::fit_mode);
    char* other_base = reinterpret_cast<char*>(other._trusted_memory);
    auto* other_parent = *reinterpret_cast<std::pmr::memory_resource**>(other_base + parent_off);
    auto other_mode = *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(other_base + mode_off);
    size_t other_managed = *reinterpret_cast<size_t*>(other_base + managed_off);
    if (other_parent == nullptr)
    {
        other_parent = std::pmr::get_default_resource();
    }
    const size_t total_size = allocator_metadata_size + block_metadata_size + other_managed;
    _trusted_memory = other_parent->allocate(total_size);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    *reinterpret_cast<std::pmr::memory_resource**>(base + parent_off) = other_parent;
    *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(base + mode_off) = other_mode;
    *reinterpret_cast<size_t*>(base + managed_off) = other_managed;
    constexpr size_t mutex_off = managed_off + sizeof(size_t);
    constexpr size_t free_head_off = mutex_off + sizeof(std::mutex);
    new (base + mutex_off) std::mutex();
    void* first_block = base + allocator_metadata_size;
    *reinterpret_cast<void**>(base + free_head_off) = first_block;
    *reinterpret_cast<void**>(first_block) = nullptr;
    *reinterpret_cast<size_t*>(reinterpret_cast<char*>(first_block) + sizeof(void*)) = other_managed;
}

allocator_sorted_list &allocator_sorted_list::operator=(const allocator_sorted_list &other)
{
    if (this == &other)
    {
        return *this;
    }
    allocator_sorted_list tmp(other);
    *this = std::move(tmp);
    return *this;
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return dynamic_cast<const allocator_sorted_list*>(&other) != nullptr;
}

void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    if (at == nullptr)
    {
        return;
    }
    constexpr size_t managed_off = sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode);
    constexpr size_t mutex_off = managed_off + sizeof(size_t);
    constexpr size_t free_head_off = mutex_off + sizeof(std::mutex);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    auto* mtx = reinterpret_cast<std::mutex*>(base + mutex_off);
    std::lock_guard<std::mutex> lock(*mtx);
    const size_t managed_size = *reinterpret_cast<size_t*>(base + managed_off);
    char* region_begin = base + allocator_metadata_size;
    char* region_end = region_begin + block_metadata_size + managed_size;
    char* payload = reinterpret_cast<char*>(at);
    if (payload < region_begin + block_metadata_size || payload >= region_end)
    {
        throw std::invalid_argument("");
    }
    void* block = payload - block_metadata_size;
    if (*reinterpret_cast<void**>(block) != _trusted_memory)
    {
        throw std::invalid_argument("");
    }
    auto block_size = [](void* ptr) -> size_t& {
        return *reinterpret_cast<size_t*>(reinterpret_cast<char*>(ptr) + sizeof(void*));
    };
    void*& free_head = *reinterpret_cast<void**>(base + free_head_off);
    void* prev = nullptr;
    void* cur = free_head;
    while (cur != nullptr && cur < block)
    {
        prev = cur;
        cur = *reinterpret_cast<void**>(cur);
    }
    *reinterpret_cast<void**>(block) = cur;
    if (prev == nullptr)
    {
        free_head = block;
    }
    else
    {
        *reinterpret_cast<void**>(prev) = block;
    }
    if (cur != nullptr)
    {
        char* block_end = reinterpret_cast<char*>(block) + block_metadata_size + block_size(block);
        if (block_end == reinterpret_cast<char*>(cur))
        {
            block_size(block) += block_metadata_size + block_size(cur);
            *reinterpret_cast<void**>(block) = *reinterpret_cast<void**>(cur);
            cur = *reinterpret_cast<void**>(block);
        }
    }
    if (prev != nullptr)
    {
        char* prev_end = reinterpret_cast<char*>(prev) + block_metadata_size + block_size(prev);
        if (prev_end == reinterpret_cast<char*>(block))
        {
            block_size(prev) += block_metadata_size + block_size(block);
            *reinterpret_cast<void**>(prev) = *reinterpret_cast<void**>(block);
        }
    }
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    constexpr size_t mode_off = sizeof(std::pmr::memory_resource*);
    constexpr size_t mutex_off = mode_off + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    auto* mtx = reinterpret_cast<std::mutex*>(base + mutex_off);
    std::lock_guard<std::mutex> lock(*mtx);
    *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(base + mode_off) = mode;
}

// ну за что

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    if (_trusted_memory == nullptr)
    {
        return {};
    }
    constexpr size_t managed_off = sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode);
    constexpr size_t mutex_off = managed_off + sizeof(size_t);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    auto* mtx = reinterpret_cast<std::mutex*>(base + mutex_off);
    std::lock_guard<std::mutex> lock(*mtx);
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;
    if (_trusted_memory == nullptr)
    {
        return result;
    }
    constexpr size_t managed_off = sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode);
    constexpr size_t mutex_off = managed_off + sizeof(size_t);
    constexpr size_t free_head_off = mutex_off + sizeof(std::mutex);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    size_t managed_size = *reinterpret_cast<size_t*>(base + managed_off);
    void* free_head = *reinterpret_cast<void**>(base + free_head_off);
    char* block = base + allocator_metadata_size;
    char* end = block + block_metadata_size + managed_size;
    while (block < end)
    {
        size_t block_size = *reinterpret_cast<size_t*>(block + sizeof(void*));
        if (block_size == 0)
        {
            break;
        }
        bool is_free = false;
        void* cur = free_head;
        while (cur != nullptr)
        {
            if (cur == block)
            {
                is_free = true;
                break;
            }
            cur = *reinterpret_cast<void**>(cur);
        }
        result.push_back({block_size, !is_free});
        block += block_metadata_size + block_size;
    }
    return result;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    return sorted_free_iterator(_trusted_memory);
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return sorted_free_iterator();
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    return sorted_iterator(_trusted_memory);
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    return sorted_iterator();
}

bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(
        const allocator_sorted_list::sorted_free_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_free_iterator &allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    if (_free_ptr != nullptr)
    {
        _free_ptr = *reinterpret_cast<void**>(_free_ptr);
    }
    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    (void)n;
    sorted_free_iterator tmp(*this);
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    if (_free_ptr == nullptr)
    {
        return 0;
    }
    return *reinterpret_cast<size_t*>(reinterpret_cast<char*>(_free_ptr) + sizeof(void*));
}

void *allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    if (_free_ptr == nullptr)
    {
        return nullptr;
    }
    return reinterpret_cast<char*>(_free_ptr) + allocator_sorted_list::block_metadata_size;
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator()
{
    _free_ptr = nullptr;
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void *trusted)
{
    if (trusted == nullptr)
    {
        _free_ptr = nullptr;
        return;
    }
    constexpr size_t managed_off = sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode);
    constexpr size_t mutex_off = managed_off + sizeof(size_t);
    constexpr size_t free_head_off = mutex_off + sizeof(std::mutex);
    char* base = reinterpret_cast<char*>(trusted);
    _free_ptr = *reinterpret_cast<void**>(base + free_head_off);
}

bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator & other) const noexcept
{
    return _current_ptr == other._current_ptr;
}

bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    if (_current_ptr == nullptr || _trusted_memory == nullptr)
    {
        return *this;
    }
    constexpr size_t managed_off = sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    size_t managed_size = *reinterpret_cast<size_t*>(base + managed_off);
    char* end = base + allocator_sorted_list::allocator_metadata_size + allocator_sorted_list::block_metadata_size + managed_size;
    size_t cur_size = *reinterpret_cast<size_t*>(reinterpret_cast<char*>(_current_ptr) + sizeof(void*));
    char* next = reinterpret_cast<char*>(_current_ptr) + allocator_sorted_list::block_metadata_size + cur_size;
    if (next >= end)
    {
        _current_ptr = nullptr;
    }
    else
    {
        _current_ptr = next;
    }
    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    (void)n;
    sorted_iterator tmp(*this);
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    if (_current_ptr == nullptr)
    {
        return 0;
    }
    return *reinterpret_cast<size_t*>(reinterpret_cast<char*>(_current_ptr) + sizeof(void*));
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    if (_current_ptr == nullptr)
    {
        return nullptr;
    }
    return reinterpret_cast<char*>(_current_ptr) + allocator_sorted_list::block_metadata_size;
}

allocator_sorted_list::sorted_iterator::sorted_iterator()
{
    _free_ptr = nullptr;
    _current_ptr = nullptr;
    _trusted_memory = nullptr;
}

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted)
{
    _trusted_memory = trusted;
    if (trusted == nullptr)
    {
        _free_ptr = nullptr;
        _current_ptr = nullptr;
        return;
    }
    constexpr size_t managed_off = sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode);
    constexpr size_t mutex_off = managed_off + sizeof(size_t);
    constexpr size_t free_head_off = mutex_off + sizeof(std::mutex);
    char* base = reinterpret_cast<char*>(trusted);
    _free_ptr = *reinterpret_cast<void**>(base + free_head_off);
    _current_ptr = base + allocator_sorted_list::allocator_metadata_size;
}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    if (_current_ptr == nullptr || _trusted_memory == nullptr)
    {
        return false;
    }
    constexpr size_t managed_off = sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode);
    constexpr size_t mutex_off = managed_off + sizeof(size_t);
    constexpr size_t free_head_off = mutex_off + sizeof(std::mutex);
    char* base = reinterpret_cast<char*>(_trusted_memory);
    void* free_head = *reinterpret_cast<void**>(base + free_head_off);
    void* cur = free_head;
    while (cur != nullptr)
    {
        if (cur == _current_ptr)
        {
            return false;
        }
        cur = *reinterpret_cast<void**>(cur);
    }
    return true;
}
