#include <iostream>
#include <cstddef>

#include <allocator_global_heap.h>
#include <pp_allocator.h>

int main()
{
    try
    {
        allocator_global_heap resource;
        pp_allocator<std::byte> allocator(&resource);

        constexpr std::size_t values_count = 5;

        int* int_values = allocator.allocate_object<int>(values_count);
        double* double_values = allocator.allocate_object<double>(values_count);

        for (std::size_t i = 0; i < values_count; ++i)
        {
            int_values[i] = static_cast<int>(i * 10);
            double_values[i] = static_cast<double>(i) / 2.0;
        }

        for (std::size_t i = 0; i < values_count; ++i)
        {
            std::cout << int_values[i] << (i + 1 == values_count ? '\n' : ' ');
        }

        for (std::size_t i = 0; i < values_count; ++i)
        {
            std::cout << double_values[i] << (i + 1 == values_count ? '\n' : ' ');
        }

        allocator.deallocate_object(double_values, values_count);
        allocator.deallocate_object(int_values, values_count);

    }
    catch (std::exception const& ex)
    {
        std::cerr << "Demo failed: " << ex.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "Demo failed: unknown exception\n";
        return 1;
    }

    return 0;
}
