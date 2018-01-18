#include <vector>

class Alloc
{
    typedef unsigned long    EltType;
    static const std::size_t eltSize = sizeof(EltType);
    EltType*                 elts;

public:
    typedef bool value_type;

    Alloc(const std::size_t numBools)
        : elts{new EltType[(numBools+eltSize-1)/eltSize]} {
    }

    EltType* allocate(const std::size_t n) const noexcept {
        return elts;
    }

    void deallocate(EltType* p, const std::size_t n) const noexcept {
    } // May be ignored for the purpose of this question

    template<class U> struct rebind { typedef Alloc other; };
};

int main()
{
    const std::size_t numBools = 4096*8;
    /*
     * 1-argument constructor `vector{Alloc{...}}` results in SIGSEGV when
     * element accessed
     */
    std::vector<bool, Alloc> vector{numBools, false, Alloc{numBools}};
    for (int i = 0; i < numBools; ++i)
        vector[1] = true;
    return 0;
}
