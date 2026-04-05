#include <iostream>
#include <allocator_sorted_list.h>
int main()
{
    try
    {
        allocator_sorted_list resource(2048, nullptr, allocator_with_fit_mode::fit_mode::first_fit);
        std::pmr::memory_resource& mem = resource;
        auto* fit_mode_iface = dynamic_cast<allocator_with_fit_mode*>(&resource);
        int* first = reinterpret_cast<int*>(mem.allocate(sizeof(int) * 6));
        for (int i = 0; i < 6; ++i)
        {
            first[i] = i * 10;
        }
        fit_mode_iface->set_fit_mode(allocator_with_fit_mode::fit_mode::the_best_fit);
        double* second = reinterpret_cast<double*>(mem.allocate(sizeof(double) * 4));
        for (int i = 0; i < 4; ++i)
        {
            second[i] = i + 0.5;
        }
        mem.deallocate(first, 1);
        fit_mode_iface->set_fit_mode(allocator_with_fit_mode::fit_mode::the_worst_fit);
        char* third = reinterpret_cast<char*>(mem.allocate(sizeof(char) * 12));
        for (int i = 0; i < 12; ++i)
        {
            third[i] = static_cast<char>('a' + i);
        }
        for (int i = 0; i < 4; ++i)
        {
            std::cout << second[i] << (i == 3 ? '\n' : ' ');
        }
        for (int i = 0; i < 12; ++i)
        {
            std::cout << third[i] << (i == 11 ? '\n' : ' ');
        }
        mem.deallocate(second, 1);
        mem.deallocate(third, 1);
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
