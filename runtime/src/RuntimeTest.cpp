#include "Runtime.h"
#include <signal.h>
#include <exception>
#include <stdexcept>
#include <iomanip>

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

typedef bool (*tcase)();
bool aborted;
void catch_abrt(int sig)
{
    aborted = true;
    signal(SIGABRT, catch_abrt);
    return;
}

std::string to_hex(uint64_t probe){
    std::stringstream stream;
    stream << std::hex << probe;
    return stream.str();
}

void assert_abort(uint64_t probe, uint8_t width)
{
    aborted = false;
    __rdzone_check((void *)probe, width);
    if (!aborted)
    {
        throw std::runtime_error("probe " + to_hex(probe) + 
            " redzone access of size " +to_hex(width) + " flew under the radar");
    }
}
void assert_ok(uint64_t probe, uint8_t width)
{
    aborted = false;
    __rdzone_check((void *)probe, width);
    if (aborted)
    {
        throw std::runtime_error("probe " + to_hex(probe) + " incorrectly triggered a redzone");
    }
}

bool test_rm_between(){
    __rdzone_add((void*)0x400000, 32);
    __rdzone_add((void*)0x400100, 32);
    __rdzone_add((void*)0x400200, 32);
    __rdzone_add((void*)0x400300, 32);
    __rdzone_add((void*)0x400400, 32);
    __rdzone_rm_between((void*)0x400180, 0x27f);

    assert_abort(0x400000, 1);
    
    assert_ok(0x400200, 1);
    assert_ok(0x400210, 1);
    assert_ok(0x400300, 1);
    assert_ok(0x400310, 1);
    assert_abort(0x400400, 1);
    assert_abort(0x400410, 1);
    return true;
}

int main()
{
    signal(SIGABRT, catch_abrt);
    tcase testcases[] = {
        &test_rm_between
    };

    //is this cheating?
    for (int i = 0; i < sizeof(testcases) / sizeof(testcases[0]); i++)
    {
        __rdzone_reset();
        bool r = false;
        try
        {
            r = testcases[i]();
        }
        catch (const std::exception &e)
        {
            printf("%s[FAILED]%s %d \n", KRED, KNRM, i + 1);
            printf("%s\n", e.what());
            __rdzone_dbg_print();
            break;
        }
        if (r)
        {
            printf("%s[PASSED]%s %d \n", KGRN, KNRM, i);
        }
    }
}