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

#pragma region
// testcases generated with gpt-4o
//  prompt: https://chatgpt.com/share/684335b0-1a00-8011-84ee-7907a73cd645

/*
Hello! I have implemented a c++ runtime library for redzones 
I will later be inserting with llvm. I have these functions to add and remove redzones 
__rdzone_add(void* start, uint64_t size)
__rdzone_rm(void* start)
Additionally I have made a testframework for the redzones. 
I would like you to create some tests by adding redzones and using 
assert_abort(uint64_t probe, uint8_t width) and assert_ok(uint64_t probe, uint8_t width) 
to mimic accesses to memory regions where assert_abort expects a redzone access 
and assert_ok expects a non-redzone access. You should put all your testcases in functions like so:

bool test1(){
    __rdzone_add((void*)0x400000, 32);
    assert_abort(0x400000, 1);
}

please make as many testcases as you can! Thank you so much! 
*/

bool test1()
{
    __rdzone_add((void *)0x400000, 32);
    assert_abort(0x400000, 1); // Start of redzone
    assert_abort(0x40001F, 1); // End of redzone
    assert_ok(0x3FFFFF, 1);    // Just before redzone
    assert_ok(0x400020, 1);    // Just after redzone
    return true;
}

bool test2()
{
    __rdzone_add((void *)0x500000, 64);
    assert_abort(0x500000, 8); // Aligned wide probe
    assert_abort(0x50003F, 1); // Last byte
    assert_ok(0x4FFFFF, 1);    // Just before
    assert_ok(0x500040, 4);    // Just after
    return true;
}

bool test3()
{
    __rdzone_add((void *)0x600000, 16);
    __rdzone_rm((void *)0x600000);
    assert_ok(0x600000, 1); // Was redzone, now safe
    return true;
}

bool test4()
{
    __rdzone_add((void *)0x700000, 32);
    __rdzone_add((void *)0x700020, 32); // Adjacent redzones
    assert_abort(0x700000, 32);         // Fully within first
    assert_abort(0x700020, 8);          // Start of second
    assert_abort(0x70001F, 2);          // Straddling first and second
    assert_ok(0x6FFFFF, 1);
    assert_ok(0x700040, 4);
    return true;
}

bool test5()
{
    __rdzone_add((void *)0x800000, 128);
    assert_abort(0x800000, 64); // Big probe inside
    assert_abort(0x800040, 64);
    assert_ok(0x7FFFFF, 1);
    assert_ok(0x800080, 1);
    return true;
}

bool test6()
{
    __rdzone_add((void *)0x900000, 1);
    assert_abort(0x900000, 1);
    assert_ok(0x900001, 1);
    return true;
}

bool test7()
{
    __rdzone_add((void *)0xA00000, 4);
    assert_abort(0xA00000, 2); // Within
    assert_abort(0xA00002, 2); // At the edge
    assert_ok(0xA00004, 1);    // After
    return true;
}

bool test8()
{
    __rdzone_add((void *)0xB00000, 16);
    assert_abort(0xB0000E, 2); // Probe straddles redzone end
    assert_ok(0xB00010, 2);    // Probe starts just after
    assert_ok(0xAFFFFF, 1);    // Far outside
    return true;
}

bool test9()
{
    __rdzone_add((void *)0xC00000, 32);
    __rdzone_rm((void *)0xC00000);
    __rdzone_add((void *)0xC00010, 16); // Redzone in middle of previous
    assert_ok(0xC00000, 16);            // Former redzone, now allowed
    assert_abort(0xC00010, 4);          // New redzone
    assert_abort(0xC0001F, 1);
    return true;
}

//A prompt for additional testcases:
/*
Excellent! I am keeping track of the redzones using an AVL tree, so as the tree
gets more complex, I assume more bugs will appear. Can you make tests that challenge that?
*/

bool test10()
{
    __rdzone_add((void *)0xD00000, 8);
    assert_abort(0xD00000, 8);
    assert_ok(0xCFFFF0, 8); // Far before
    assert_ok(0xD00008, 8); // Far after
    return true;
}

bool test11()
{
    // RR Rotation: Inserting in increasing order
    __rdzone_add((void *)0x100000, 16);
    __rdzone_add((void *)0x200000, 16);
    __rdzone_add((void *)0x300000, 16); // Should trigger RR

    assert_abort(0x100000, 1);
    assert_abort(0x200000, 1);
    assert_abort(0x300000, 1);
    assert_ok(0x0FFFFF, 1);
    return true;
}

bool test12()
{
    // LL Rotation: Inserting in decreasing order
    __rdzone_add((void *)0x500000, 16);
    __rdzone_add((void *)0x400000, 16);
    __rdzone_add((void *)0x300000, 16); // Should trigger LL

    assert_abort(0x300000, 1);
    assert_abort(0x500000, 1);
    assert_ok(0x200000, 1);
    return true;
}

bool test13()
{
    // LR Rotation
    __rdzone_add((void *)0x800000, 16);
    __rdzone_add((void *)0x700000, 16);
    __rdzone_add((void *)0x750000, 16); // Should trigger LR

    assert_abort(0x700000, 1);
    assert_abort(0x750000, 1);
    assert_abort(0x800000, 1);
    return true;
}

bool test14()
{
    // RL Rotation
    __rdzone_add((void *)0xA00000, 16);
    __rdzone_add((void *)0xB00000, 16);
    __rdzone_add((void *)0xA80000, 16); // Should trigger RL

    assert_abort(0xA00000, 1);
    assert_abort(0xA80000, 1);
    assert_abort(0xB00000, 1);
    return true;
}

bool test15()
{
    // Many inserts to create a dense AVL tree
    for (uint64_t i = 0; i < 128; ++i)
    {
        __rdzone_add((void *)(0xC00000 + i * 0x100), 16);
    }

    // Probe in middle of every redzone
    for (uint64_t i = 0; i < 128; ++i)
    {
        assert_abort(0xC00000 + i * 0x100, 1);
    }

    // Probe gaps
    for (uint64_t i = 0; i < 128; ++i)
    {
        assert_ok(0xC00000 + i * 0x100 - 1, 1);
        assert_ok(0xC00000 + i * 0x100 + 16, 1);
    }

    return true;
}

bool test16()
{
    // Redzone removal in the middle of AVL tree
    __rdzone_add((void *)0xE00000, 16);
    __rdzone_add((void *)0xE10000, 16);
    __rdzone_add((void *)0xE20000, 16);
    __rdzone_rm((void *)0xE10000); // Remove middle

    assert_abort(0xE00000, 1);
    assert_abort(0xE20000, 1);
    assert_ok(0xE10000, 1); // Should be safe now
    return true;
}

bool test17()
{
    // Probes that straddle multiple redzones
    __rdzone_add((void *)0xF00000, 32);
    __rdzone_add((void *)0xF00040, 32);

    assert_abort(0xF0001F, 32); // Crosses redzone1 and into gap
    assert_abort(0xF0003F, 2);  // Ends inside redzone2
    assert_ok(0xF00020, 16);    // Clean probe in gap
    return true;
}

bool test18()
{
    // Interleaved add/remove
    __rdzone_add((void *)0x1100000, 16);
    __rdzone_add((void *)0x1110000, 16);
    __rdzone_rm((void *)0x1100000);
    __rdzone_add((void *)0x1108000, 16); // After remove, insert non-overlapping

    assert_ok(0x1100000, 1);
    assert_abort(0x1108000, 1);
    assert_abort(0x1110000, 1);
    return true;
}

bool test19()
{
    // Add huge range
    __rdzone_add((void *)0x1200000, 0x10000); // 64KB

    assert_abort(0x1200000, 8);
    assert_abort(0x120FFFF, 1);
    assert_ok(0x11FFFFF, 1);
    assert_ok(0x1210000, 1);
    return true;
}

bool test20() {
    const uint64_t base = 0x2000000;
    const uint64_t redzone_size = 16;
    const uint64_t gap = 8;  // Ensures redzones don’t overlap
    const int count = 5000;

    // Insert many redzones
    for (int i = 0; i < count; ++i) {
        __rdzone_add((void*)(base + i * (redzone_size + gap)), redzone_size);
    }

    // Probes inside each redzone
    for (int i = 0; i < count; ++i) {
        uint64_t addr = base + i * (redzone_size + gap);
        assert_abort(addr, 1);                   // Start
        assert_abort(addr + redzone_size - 1, 1); // End
        assert_abort(addr + redzone_size / 2, 1); // Middle
    }

    // Probes in gaps between redzones
    for (int i = 0; i < count - 1; ++i) {
        uint64_t gap_start = base + i * (redzone_size + gap) + redzone_size;
        assert_ok(gap_start, 1);                 // Just after redzone
        assert_ok(gap_start + gap - 1, 1);       // Last byte of gap
    }

    // Edge conditions (after all redzones)
    uint64_t last = base + (count - 1) * (redzone_size + gap);
    assert_ok(last + redzone_size + 1, 1);

    return true;
}


// end of gpt generated code
#pragma endregion

int main()
{
    signal(SIGABRT, catch_abrt);
    tcase testcases[] = {
        &test1,
        &test2,
        &test3,
        &test4,
        &test5,
        &test6,
        &test7,
        &test8,
        &test9,
        &test10,
        &test11,
        &test12,
        &test13,
        &test14,
        &test15,
        &test16,
        &test17,
        &test18,
        &test19,
        &test20
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