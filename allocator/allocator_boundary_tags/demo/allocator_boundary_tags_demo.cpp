#include <iostream>
#include <allocator_boundary_tags.h>

int main()
{
    try
    {
        allocator_boundary_tags resource(4096, nullptr, allocator_with_fit_mode::fit_mode::first_fit);
        std::pmr::memory_resource &mem = resource;
        auto *fit_mode_iface = dynamic_cast<allocator_with_fit_mode*>(&resource);

        int *first = reinterpret_cast<int*>(mem.allocate(sizeof(int) * 6));
        for (int i = 0; i < 6; ++i)
        {
            first[i] = i * 3;
        }

        fit_mode_iface->set_fit_mode(allocator_with_fit_mode::fit_mode::the_best_fit);
        double *second = reinterpret_cast<double*>(mem.allocate(sizeof(double) * 4));
        for (int i = 0; i < 4; ++i)
        {
            second[i] = 0.25 * (i + 1);
        }

        mem.deallocate(first, 1);

        fit_mode_iface->set_fit_mode(allocator_with_fit_mode::fit_mode::the_worst_fit);
        char *third = reinterpret_cast<char*>(mem.allocate(sizeof(char) * 10));
        for (int i = 0; i < 10; ++i)
        {
            third[i] = static_cast<char>('A' + i);
        }

        for (int i = 0; i < 4; ++i)
        {
            std::cout << second[i] << (i == 3 ? '\n' : ' ');
        }
        for (int i = 0; i < 10; ++i)
        {
            std::cout << third[i] << (i == 9 ? '\n' : ' ');
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
