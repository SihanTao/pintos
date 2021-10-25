#include "fixed-point.h"
#include <stdio.h>
#include <assert.h>

void test_to_fp_to_intn(void)
{
    printf("%s\n", "Now testing to fp to intn");
    assert( 100 == to_intn(to_fp(100)));
    assert( 23515 == to_intn(to_fp(23515)));
    assert( 1342 + 1423 == to_intn(to_fp(1342 + 1423)));
    assert( 1342 + 14 * 23 == to_intn(to_fp(1342 + 14 * 23)));
    assert(1 << 12 == to_intn(to_fp(1 << 12)));
}

void test_fp_add(void)
{
    printf("%s\n", "Now testing fp add");
    assert(100 + 200 == to_intn(fp_add(to_fp(100), to_fp(200))));
    assert(100 + 200 == to_intn(fp_add(to_fp(200), to_fp(100))));
    assert(31451 + 2351 == to_intn(fp_add(to_fp(31451), to_fp(2351))));
}

void test_fp_mul(void)
{
    printf("%s\n", "Now testing fp add");
    assert(351 + 23 * 51 == to_intn(fp_add(
        to_fp(351), 
        fp_mul(to_fp(23), to_fp(51))
    )));
    assert(2134 * 51 == to_intn( fp_mul(to_fp(2134), to_fp(51))));
}


void test_fp_int_add(void)
{
    printf("%s\n", "Now testing fp int add");
    assert(to_fp(351) + to_fp(23 * 51) == fp_int_add(
        to_fp(351), 
        23 * 51
    ));
    assert(351 + 23 * 51 == to_intn(fp_int_add(
        to_fp(351), 
        23 * 51
    )));
}

int main(void)
{
    test_to_fp_to_intn();
    test_fp_add();
    test_fp_mul();
    test_fp_int_add();
    printf("%s\n", "all tests finished!");
}