#include <iostream>
#include <allocator_buddies_system.h>

int main()
{
    try
    {
        allocator_buddies_system resource(4096, nullptr, allocator_with_fit_mode::fit_mode::first_fit);
        std::pmr::memory_resource &mem = resource;
        auto *fit_mode_iface = dynamic_cast<allocator_with_fit_mode*>(&resource);

        int *first = reinterpret_cast<int*>(mem.allocate(sizeof(int) * 8));
        for (int i = 0; i < 8; ++i)
        {
            first[i] = i + 1;
        }

        fit_mode_iface->set_fit_mode(allocator_with_fit_mode::fit_mode::the_best_fit);
        double *second = reinterpret_cast<double*>(mem.allocate(sizeof(double) * 3));
        for (int i = 0; i < 3; ++i)
        {
            second[i] = 1.5 * i;
        }

        mem.deallocate(first, 1);

        fit_mode_iface->set_fit_mode(allocator_with_fit_mode::fit_mode::the_worst_fit);
        char *third = reinterpret_cast<char*>(mem.allocate(sizeof(char) * 12));
        for (int i = 0; i < 12; ++i)
        {
            third[i] = static_cast<char>('a' + i);
        }

        for (int i = 0; i < 3; ++i)
        {
            std::cout << second[i] << (i == 2 ? '\n' : ' ');
        }
        for (int i = 0; i < 12; ++i)
        {
            std::cout << third[i] << (i == 11 ? '\n' : ' ');
        }

        mem.deallocate(second, 1);
        mem.deallocate(third, 1);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Demo failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
